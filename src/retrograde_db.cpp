#include "retrograde_db.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <rocksdb/write_batch.h>

namespace bobail {

RetrogradeSolverDB::RetrogradeSolverDB() {}

RetrogradeSolverDB::~RetrogradeSolverDB() {
    close();
}

bool RetrogradeSolverDB::open(const std::string& db_path) {
    rocksdb::Options options;
    options.create_if_missing = true;
    options.create_missing_column_families = true;

    // Optimize for bulk loading
    options.max_background_jobs = 4;
    options.max_write_buffer_number = 4;
    options.write_buffer_size = 64 * 1024 * 1024;  // 64MB
    options.target_file_size_base = 64 * 1024 * 1024;

    // Column family descriptors
    std::vector<rocksdb::ColumnFamilyDescriptor> cf_descs;
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        rocksdb::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions()));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        "states", rocksdb::ColumnFamilyOptions()));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        "packed_to_id", rocksdb::ColumnFamilyOptions()));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        "predecessors", rocksdb::ColumnFamilyOptions()));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        "queue", rocksdb::ColumnFamilyOptions()));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        "metadata", rocksdb::ColumnFamilyOptions()));

    std::vector<rocksdb::ColumnFamilyHandle*> cf_handles;
    rocksdb::DB* db_ptr;

    rocksdb::Status status = rocksdb::DB::Open(options, db_path, cf_descs, &cf_handles, &db_ptr);
    if (!status.ok()) {
        std::cerr << "Failed to open database: " << status.ToString() << "\n";
        return false;
    }

    db_.reset(db_ptr);
    cf_metadata_ = cf_handles[0];  // default
    cf_states_ = cf_handles[1];
    cf_packed_to_id_ = cf_handles[2];
    cf_predecessors_ = cf_handles[3];
    cf_queue_ = cf_handles[4];
    // cf_handles[5] is the second metadata handle, we'll use default

    // Load metadata if exists
    load_metadata();

    return true;
}

void RetrogradeSolverDB::close() {
    if (db_) {
        save_metadata();

        // Delete column family handles
        if (cf_states_) { delete cf_states_; cf_states_ = nullptr; }
        if (cf_packed_to_id_) { delete cf_packed_to_id_; cf_packed_to_id_ = nullptr; }
        if (cf_predecessors_) { delete cf_predecessors_; cf_predecessors_ = nullptr; }
        if (cf_queue_) { delete cf_queue_; cf_queue_ = nullptr; }
        if (cf_metadata_) { delete cf_metadata_; cf_metadata_ = nullptr; }

        db_.reset();
    }
}

void RetrogradeSolverDB::save_metadata() {
    if (!db_) return;

    rocksdb::WriteBatch batch;

    auto put_u64 = [&](const std::string& key, uint64_t val) {
        batch.Put(cf_metadata_, key, std::string(reinterpret_cast<char*>(&val), sizeof(val)));
    };
    auto put_u32 = [&](const std::string& key, uint32_t val) {
        batch.Put(cf_metadata_, key, std::string(reinterpret_cast<char*>(&val), sizeof(val)));
    };

    put_u32("phase", static_cast<uint32_t>(phase_));
    put_u64("num_states", num_states_);
    put_u64("num_wins", num_wins_);
    put_u64("num_losses", num_losses_);
    put_u64("num_draws", num_draws_);
    put_u32("start_id", start_id_);
    put_u64("enum_processed", enum_processed_);
    put_u64("queue_head", queue_head_);
    put_u64("queue_tail", queue_tail_);

    db_->Write(rocksdb::WriteOptions(), &batch);
}

void RetrogradeSolverDB::load_metadata() {
    if (!db_) return;

    auto get_u64 = [&](const std::string& key, uint64_t& val) {
        std::string value;
        if (db_->Get(rocksdb::ReadOptions(), cf_metadata_, key, &value).ok() && value.size() == sizeof(val)) {
            std::memcpy(&val, value.data(), sizeof(val));
        }
    };
    auto get_u32 = [&](const std::string& key, uint32_t& val) {
        std::string value;
        if (db_->Get(rocksdb::ReadOptions(), cf_metadata_, key, &value).ok() && value.size() == sizeof(val)) {
            std::memcpy(&val, value.data(), sizeof(val));
        }
    };

    uint32_t phase_val = 0;
    get_u32("phase", phase_val);
    phase_ = static_cast<SolvePhaseDB>(phase_val);
    get_u64("num_states", num_states_);
    get_u64("num_wins", num_wins_);
    get_u64("num_losses", num_losses_);
    get_u64("num_draws", num_draws_);
    get_u32("start_id", start_id_);
    get_u64("enum_processed", enum_processed_);
    get_u64("queue_head", queue_head_);
    get_u64("queue_tail", queue_tail_);
}

bool RetrogradeSolverDB::solve() {
    if (!db_) {
        std::cerr << "Database not open\n";
        return false;
    }

    bool use_parallel = (num_threads_ > 1);
    if (use_parallel) {
        std::cerr << "Using " << num_threads_ << " threads for parallel processing\n";
    }

    // Resume from current phase or start fresh
    if (phase_ == SolvePhaseDB::NOT_STARTED || phase_ == SolvePhaseDB::ENUMERATING) {
        if (progress_cb_) progress_cb_("Enumerating states", 0, 0);
        phase_ = SolvePhaseDB::ENUMERATING;
        if (use_parallel) {
            enumerate_states_parallel();
        } else {
            enumerate_states();
        }
        phase_ = SolvePhaseDB::BUILDING_PREDECESSORS;
        save_metadata();
    }

    if (phase_ == SolvePhaseDB::BUILDING_PREDECESSORS) {
        if (progress_cb_) progress_cb_("Building predecessors", 0, num_states_);
        if (use_parallel) {
            build_predecessors_parallel();
        } else {
            build_predecessors();
        }
        phase_ = SolvePhaseDB::MARKING_TERMINALS;
        save_metadata();
    }

    if (phase_ == SolvePhaseDB::MARKING_TERMINALS) {
        if (progress_cb_) progress_cb_("Marking terminals", 0, num_states_);
        if (use_parallel) {
            mark_terminals_parallel();
        } else {
            mark_terminals();
        }
        phase_ = SolvePhaseDB::PROPAGATING;
        save_metadata();
    }

    if (phase_ == SolvePhaseDB::PROPAGATING) {
        if (progress_cb_) progress_cb_("Propagating", 0, num_states_);
        propagate();
        phase_ = SolvePhaseDB::COMPLETE;
        save_metadata();
    }

    return true;
}

uint32_t RetrogradeSolverDB::get_or_create_state(uint64_t packed) {
    // Check if exists
    std::string key(reinterpret_cast<char*>(&packed), sizeof(packed));
    std::string value;

    if (db_->Get(rocksdb::ReadOptions(), cf_packed_to_id_, key, &value).ok()) {
        uint32_t id;
        std::memcpy(&id, value.data(), sizeof(id));
        return id;
    }

    // Create new state
    uint32_t id = num_states_++;

    StateInfoCompact info;
    info.packed = packed;
    info.result = static_cast<uint8_t>(Result::UNKNOWN);
    info.num_successors = 0;
    info.winning_succs = 0;

    // Store state info
    std::string id_key(reinterpret_cast<char*>(&id), sizeof(id));
    std::string info_val(reinterpret_cast<char*>(&info), sizeof(info));

    rocksdb::WriteBatch batch;
    batch.Put(cf_states_, id_key, info_val);
    batch.Put(cf_packed_to_id_, key, std::string(reinterpret_cast<char*>(&id), sizeof(id)));
    db_->Write(rocksdb::WriteOptions(), &batch);

    return id;
}

int64_t RetrogradeSolverDB::get_state_id(uint64_t packed) const {
    std::string key(reinterpret_cast<char*>(&packed), sizeof(packed));
    std::string value;

    if (db_->Get(rocksdb::ReadOptions(), cf_packed_to_id_, key, &value).ok()) {
        uint32_t id;
        std::memcpy(&id, value.data(), sizeof(id));
        return id;
    }
    return -1;
}

bool RetrogradeSolverDB::get_state_info(uint32_t id, StateInfoCompact& info) const {
    std::string key(reinterpret_cast<char*>(&id), sizeof(id));
    std::string value;

    if (db_->Get(rocksdb::ReadOptions(), cf_states_, key, &value).ok()) {
        std::memcpy(&info, value.data(), sizeof(info));
        return true;
    }
    return false;
}

bool RetrogradeSolverDB::put_state_info(uint32_t id, const StateInfoCompact& info) {
    std::string key(reinterpret_cast<char*>(&id), sizeof(id));
    std::string value(reinterpret_cast<const char*>(&info), sizeof(info));
    return db_->Put(rocksdb::WriteOptions(), cf_states_, key, value).ok();
}

void RetrogradeSolverDB::add_predecessor(uint32_t state_id, uint32_t pred_id) {
    std::string key(reinterpret_cast<char*>(&state_id), sizeof(state_id));
    std::string value;

    // Get existing predecessors
    db_->Get(rocksdb::ReadOptions(), cf_predecessors_, key, &value);

    // Append new predecessor
    value.append(reinterpret_cast<char*>(&pred_id), sizeof(pred_id));

    db_->Put(rocksdb::WriteOptions(), cf_predecessors_, key, value);
}

std::vector<uint32_t> RetrogradeSolverDB::get_predecessors(uint32_t state_id) const {
    std::string key(reinterpret_cast<char*>(&state_id), sizeof(state_id));
    std::string value;
    std::vector<uint32_t> preds;

    if (db_->Get(rocksdb::ReadOptions(), cf_predecessors_, key, &value).ok()) {
        size_t count = value.size() / sizeof(uint32_t);
        preds.resize(count);
        std::memcpy(preds.data(), value.data(), value.size());
    }
    return preds;
}

void RetrogradeSolverDB::enumerate_states() {
    // Check if resuming
    if (queue_tail_ == 0 && queue_head_ == 0 && num_states_ == 0) {
        // Fresh start
        State start = State::starting_position();
        auto [canonical_start, _] = canonicalize(start);
        uint64_t start_packed = pack_state(canonical_start);

        start_id_ = get_or_create_state(start_packed);

        // Add to queue
        std::string queue_key(reinterpret_cast<char*>(&queue_tail_), sizeof(queue_tail_));
        std::string queue_val(reinterpret_cast<char*>(&start_id_), sizeof(start_id_));
        db_->Put(rocksdb::WriteOptions(), cf_queue_, queue_key, queue_val);
        queue_tail_++;
        enum_processed_ = 0;
    }

    if (progress_cb_ && queue_head_ > 0) {
        progress_cb_("Resuming enumeration", enum_processed_, num_states_);
    }

    uint64_t last_save = enum_processed_;
    rocksdb::WriteBatch batch;
    int batch_count = 0;
    const int BATCH_SIZE = 1000;

    while (queue_head_ < queue_tail_) {
        // Pop from queue
        std::string queue_key(reinterpret_cast<char*>(&queue_head_), sizeof(queue_head_));
        std::string queue_val;

        if (!db_->Get(rocksdb::ReadOptions(), cf_queue_, queue_key, &queue_val).ok()) {
            break;
        }

        uint32_t id;
        std::memcpy(&id, queue_val.data(), sizeof(id));
        queue_head_++;

        // Delete from queue to save space
        batch.Delete(cf_queue_, queue_key);

        // Get state info
        StateInfoCompact info;
        if (!get_state_info(id, info)) continue;

        State s = unpack_state(info.packed);

        // Check if terminal
        GameResult gr = check_terminal(s);
        if (gr != GameResult::ONGOING) {
            info.num_successors = 0;
            put_state_info(id, info);
            ++enum_processed_;
            continue;
        }

        // Generate all moves
        auto moves = generate_moves(s);
        info.num_successors = moves.size();
        put_state_info(id, info);

        if (moves.empty()) {
            ++enum_processed_;
            continue;
        }

        // Process each successor
        for (const auto& move : moves) {
            State ns = apply_move(s, move);
            auto [canonical_ns, __] = canonicalize(ns);
            uint64_t ns_packed = pack_state(canonical_ns);

            int64_t existing_id = get_state_id(ns_packed);
            if (existing_id < 0) {
                // New state
                uint32_t new_id = get_or_create_state(ns_packed);

                // Add to queue
                std::string new_queue_key(reinterpret_cast<char*>(&queue_tail_), sizeof(queue_tail_));
                std::string new_queue_val(reinterpret_cast<char*>(&new_id), sizeof(new_id));
                batch.Put(cf_queue_, new_queue_key, new_queue_val);
                queue_tail_++;
                batch_count++;
            }
        }

        ++enum_processed_;

        // Flush batch periodically
        if (batch_count >= BATCH_SIZE) {
            db_->Write(rocksdb::WriteOptions(), &batch);
            batch.Clear();
            batch_count = 0;
        }

        if (progress_cb_ && enum_processed_ % 100000 == 0) {
            progress_cb_("Enumerating states", enum_processed_, num_states_);
        }

        // Save metadata periodically
        if (checkpoint_interval_ > 0 && enum_processed_ - last_save >= checkpoint_interval_) {
            db_->Write(rocksdb::WriteOptions(), &batch);
            batch.Clear();
            batch_count = 0;
            save_metadata();
            last_save = enum_processed_;
            std::cerr << "Checkpoint saved: " << num_states_ << " states, phase 1\n";
        }
    }

    // Flush remaining batch
    if (batch_count > 0) {
        db_->Write(rocksdb::WriteOptions(), &batch);
    }

    if (progress_cb_) {
        progress_cb_("Enumeration complete", num_states_, num_states_);
    }
}

// Parallel enumeration using batch processing
void RetrogradeSolverDB::enumerate_states_parallel() {
    // Check if resuming
    if (queue_tail_ == 0 && queue_head_ == 0 && num_states_ == 0) {
        // Fresh start
        State start = State::starting_position();
        auto [canonical_start, _] = canonicalize(start);
        uint64_t start_packed = pack_state(canonical_start);

        start_id_ = get_or_create_state(start_packed);

        // Add to queue
        std::string queue_key(reinterpret_cast<char*>(&queue_tail_), sizeof(queue_tail_));
        std::string queue_val(reinterpret_cast<char*>(&start_id_), sizeof(start_id_));
        db_->Put(rocksdb::WriteOptions(), cf_queue_, queue_key, queue_val);
        queue_tail_++;
        enum_processed_ = 0;
    }

    if (progress_cb_ && queue_head_ > 0) {
        progress_cb_("Resuming parallel enumeration", enum_processed_, num_states_);
    }

    atomic_enum_processed_ = enum_processed_;
    atomic_num_states_ = num_states_;
    stop_workers_ = false;

    // Process queue in batches
    const size_t BATCH_SIZE = 10000;  // Load this many items from disk queue at a time

    while (queue_head_ < queue_tail_) {
        // Load a batch of work items into memory
        work_queue_.clear();
        work_queue_head_ = 0;

        size_t batch_end = std::min(queue_head_ + BATCH_SIZE, queue_tail_);
        for (uint64_t i = queue_head_; i < batch_end; ++i) {
            std::string queue_key(reinterpret_cast<char*>(&i), sizeof(i));
            std::string queue_val;

            if (db_->Get(rocksdb::ReadOptions(), cf_queue_, queue_key, &queue_val).ok()) {
                uint32_t id;
                std::memcpy(&id, queue_val.data(), sizeof(id));
                work_queue_.push_back(id);
            }
            // Delete from disk queue
            db_->Delete(rocksdb::WriteOptions(), cf_queue_, queue_key);
        }
        queue_head_ = batch_end;

        if (work_queue_.empty()) break;

        // Structures for collecting new states from all threads
        std::vector<std::vector<std::pair<uint64_t, uint32_t>>> thread_new_states(num_threads_);
        std::vector<std::vector<std::pair<uint32_t, StateInfoCompact>>> thread_updates(num_threads_);

        // Process work items in parallel
        std::vector<std::thread> workers;
        for (int t = 0; t < num_threads_; ++t) {
            workers.emplace_back([this, t, &thread_new_states, &thread_updates]() {
                auto& new_states = thread_new_states[t];
                auto& updates = thread_updates[t];

                while (true) {
                    // Grab next work item
                    size_t idx = work_queue_head_.fetch_add(1);
                    if (idx >= work_queue_.size()) break;

                    uint32_t id = work_queue_[idx];

                    // Get state info
                    StateInfoCompact info;
                    {
                        std::lock_guard<std::mutex> lock(db_mutex_);
                        if (!get_state_info(id, info)) continue;
                    }

                    State s = unpack_state(info.packed);

                    // Check if terminal
                    GameResult gr = check_terminal(s);
                    if (gr != GameResult::ONGOING) {
                        info.num_successors = 0;
                        updates.push_back({id, info});
                        atomic_enum_processed_++;
                        continue;
                    }

                    // Generate all moves
                    auto moves = generate_moves(s);
                    info.num_successors = moves.size();
                    updates.push_back({id, info});

                    if (moves.empty()) {
                        atomic_enum_processed_++;
                        continue;
                    }

                    // Process each successor
                    for (const auto& move : moves) {
                        State ns = apply_move(s, move);
                        auto [canonical_ns, __] = canonicalize(ns);
                        uint64_t ns_packed = pack_state(canonical_ns);

                        // Check if already exists (read-only check first)
                        int64_t existing_id;
                        {
                            std::lock_guard<std::mutex> lock(db_mutex_);
                            existing_id = get_state_id(ns_packed);
                        }

                        if (existing_id < 0) {
                            // Mark for creation with a placeholder
                            // Use atomic counter for new ID
                            uint32_t new_id = atomic_num_states_.fetch_add(1);
                            new_states.push_back({ns_packed, new_id});
                        }
                    }

                    atomic_enum_processed_++;
                }
            });
        }

        // Wait for all workers to complete
        for (auto& w : workers) {
            w.join();
        }

        // Merge results - state updates
        rocksdb::WriteBatch batch;
        for (const auto& updates : thread_updates) {
            for (const auto& [id, info] : updates) {
                uint32_t id_copy = id;
                std::string id_key(reinterpret_cast<char*>(&id_copy), sizeof(id_copy));
                std::string info_val(reinterpret_cast<const char*>(&info), sizeof(info));
                batch.Put(cf_states_, id_key, info_val);
            }
        }

        // Merge new states - deduplicate and create
        std::vector<std::pair<uint64_t, uint32_t>> all_new_states;
        for (auto& new_states : thread_new_states) {
            all_new_states.insert(all_new_states.end(), new_states.begin(), new_states.end());
        }

        // Sort by packed value to deduplicate
        std::sort(all_new_states.begin(), all_new_states.end());

        uint32_t actual_new_count = 0;
        for (size_t i = 0; i < all_new_states.size(); ++i) {
            uint64_t packed = all_new_states[i].first;

            // Skip duplicates
            if (i > 0 && all_new_states[i - 1].first == packed) continue;

            // Check if already exists in DB
            int64_t existing = get_state_id(packed);
            if (existing >= 0) continue;

            // Create the state
            uint32_t id = num_states_ + actual_new_count;
            actual_new_count++;

            StateInfoCompact info;
            info.packed = packed;
            info.result = static_cast<uint8_t>(Result::UNKNOWN);
            info.num_successors = 0;
            info.winning_succs = 0;

            std::string id_key(reinterpret_cast<char*>(&id), sizeof(id));
            std::string info_val(reinterpret_cast<char*>(&info), sizeof(info));
            std::string packed_key(reinterpret_cast<char*>(&packed), sizeof(packed));
            std::string id_val(reinterpret_cast<char*>(&id), sizeof(id));

            batch.Put(cf_states_, id_key, info_val);
            batch.Put(cf_packed_to_id_, packed_key, id_val);

            // Add to queue for future processing
            std::string queue_key(reinterpret_cast<char*>(&queue_tail_), sizeof(queue_tail_));
            std::string queue_val(reinterpret_cast<char*>(&id), sizeof(id));
            batch.Put(cf_queue_, queue_key, queue_val);
            queue_tail_++;
        }

        num_states_ += actual_new_count;
        db_->Write(rocksdb::WriteOptions(), &batch);

        enum_processed_ = atomic_enum_processed_;

        if (progress_cb_ && enum_processed_ % 100000 < BATCH_SIZE) {
            progress_cb_("Parallel enumeration", enum_processed_, num_states_);
        }

        // Checkpoint periodically
        if (checkpoint_interval_ > 0 && enum_processed_ % checkpoint_interval_ < BATCH_SIZE) {
            save_metadata();
            std::cerr << "Checkpoint saved: " << num_states_ << " states, phase 1 (parallel)\n";
        }
    }

    if (progress_cb_) {
        progress_cb_("Enumeration complete", num_states_, num_states_);
    }
}

void RetrogradeSolverDB::build_predecessors() {
    rocksdb::WriteBatch batch;
    int batch_count = 0;
    const int BATCH_SIZE = 1000;

    for (uint32_t id = 0; id < num_states_; ++id) {
        StateInfoCompact info;
        if (!get_state_info(id, info)) continue;

        State s = unpack_state(info.packed);

        // Skip terminals
        if (check_terminal(s) != GameResult::ONGOING) {
            continue;
        }

        auto moves = generate_moves(s);
        if (moves.empty()) continue;

        for (const auto& move : moves) {
            State ns = apply_move(s, move);
            auto [canonical_ns, _] = canonicalize(ns);
            uint64_t ns_packed = pack_state(canonical_ns);

            int64_t succ_id = get_state_id(ns_packed);
            if (succ_id >= 0) {
                // Add predecessor using batch
                std::string key(reinterpret_cast<char*>(&succ_id), sizeof(uint32_t));
                std::string existing;
                db_->Get(rocksdb::ReadOptions(), cf_predecessors_, key, &existing);
                existing.append(reinterpret_cast<char*>(&id), sizeof(id));
                batch.Put(cf_predecessors_, key, existing);
                batch_count++;
            }
        }

        if (batch_count >= BATCH_SIZE) {
            db_->Write(rocksdb::WriteOptions(), &batch);
            batch.Clear();
            batch_count = 0;
        }

        if (progress_cb_ && id % 100000 == 0) {
            progress_cb_("Building predecessors", id, num_states_);
        }
    }

    if (batch_count > 0) {
        db_->Write(rocksdb::WriteOptions(), &batch);
    }

    if (progress_cb_) {
        progress_cb_("Predecessors complete", num_states_, num_states_);
    }
}

void RetrogradeSolverDB::build_predecessors_parallel() {
    atomic_enum_processed_ = 0;
    const uint64_t CHUNK_SIZE = 10000;

    std::vector<std::thread> workers;
    for (int t = 0; t < num_threads_; ++t) {
        workers.emplace_back([this, t, CHUNK_SIZE]() {
            while (true) {
                // Grab next chunk
                uint64_t chunk_start = atomic_enum_processed_.fetch_add(CHUNK_SIZE);
                if (chunk_start >= num_states_) break;

                uint64_t chunk_end = std::min(chunk_start + CHUNK_SIZE, num_states_);

                // Collect all predecessor relations for this chunk
                std::vector<std::pair<uint32_t, uint32_t>> pred_relations; // (succ_id, pred_id)

                for (uint64_t id = chunk_start; id < chunk_end; ++id) {
                    StateInfoCompact info;
                    {
                        std::lock_guard<std::mutex> lock(db_mutex_);
                        if (!get_state_info(id, info)) continue;
                    }

                    State s = unpack_state(info.packed);

                    // Skip terminals
                    if (check_terminal(s) != GameResult::ONGOING) {
                        continue;
                    }

                    auto moves = generate_moves(s);
                    if (moves.empty()) continue;

                    for (const auto& move : moves) {
                        State ns = apply_move(s, move);
                        auto [canonical_ns, _] = canonicalize(ns);
                        uint64_t ns_packed = pack_state(canonical_ns);

                        int64_t succ_id;
                        {
                            std::lock_guard<std::mutex> lock(db_mutex_);
                            succ_id = get_state_id(ns_packed);
                        }

                        if (succ_id >= 0) {
                            pred_relations.push_back({static_cast<uint32_t>(succ_id), static_cast<uint32_t>(id)});
                        }
                    }
                }

                // Write predecessor relations under lock
                {
                    std::lock_guard<std::mutex> lock(db_mutex_);
                    for (const auto& [succ_id, pred_id] : pred_relations) {
                        std::string key(reinterpret_cast<const char*>(&succ_id), sizeof(uint32_t));
                        std::string existing;
                        db_->Get(rocksdb::ReadOptions(), cf_predecessors_, key, &existing);
                        existing.append(reinterpret_cast<const char*>(&pred_id), sizeof(pred_id));
                        db_->Put(rocksdb::WriteOptions(), cf_predecessors_, key, existing);
                    }
                }
            }
        });
    }

    // Progress monitoring in main thread
    while (true) {
        uint64_t processed = atomic_enum_processed_;
        if (processed >= num_states_) break;

        if (progress_cb_) {
            progress_cb_("Building predecessors (parallel)", processed, num_states_);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    for (auto& w : workers) {
        w.join();
    }

    if (progress_cb_) {
        progress_cb_("Predecessors complete", num_states_, num_states_);
    }
}

void RetrogradeSolverDB::mark_terminals() {
    for (uint32_t id = 0; id < num_states_; ++id) {
        StateInfoCompact info;
        if (!get_state_info(id, info)) continue;

        State s = unpack_state(info.packed);
        GameResult gr = check_terminal(s);

        bool changed = false;

        if (gr == GameResult::WHITE_WINS) {
            if (s.white_to_move) {
                info.result = static_cast<uint8_t>(Result::WIN);
                ++num_wins_;
            } else {
                info.result = static_cast<uint8_t>(Result::LOSS);
                ++num_losses_;
            }
            changed = true;
        } else if (gr == GameResult::BLACK_WINS) {
            if (!s.white_to_move) {
                info.result = static_cast<uint8_t>(Result::WIN);
                ++num_wins_;
            } else {
                info.result = static_cast<uint8_t>(Result::LOSS);
                ++num_losses_;
            }
            changed = true;
        } else if (info.num_successors == 0) {
            auto moves = generate_moves(s);
            if (moves.empty()) {
                info.result = static_cast<uint8_t>(Result::LOSS);
                ++num_losses_;
                changed = true;
            }
        }

        if (changed) {
            put_state_info(id, info);
        }

        if (progress_cb_ && id % 100000 == 0) {
            progress_cb_("Marking terminals", id, num_states_);
        }
    }

    if (progress_cb_) {
        progress_cb_("Terminals marked", num_states_, num_states_);
    }
}

void RetrogradeSolverDB::mark_terminals_parallel() {
    atomic_enum_processed_ = 0;
    std::atomic<uint64_t> atomic_wins{0};
    std::atomic<uint64_t> atomic_losses{0};
    const uint64_t CHUNK_SIZE = 10000;

    std::vector<std::thread> workers;
    for (int t = 0; t < num_threads_; ++t) {
        workers.emplace_back([this, &atomic_wins, &atomic_losses, CHUNK_SIZE]() {
            uint64_t local_wins = 0;
            uint64_t local_losses = 0;

            while (true) {
                // Grab next chunk
                uint64_t chunk_start = atomic_enum_processed_.fetch_add(CHUNK_SIZE);
                if (chunk_start >= num_states_) break;

                uint64_t chunk_end = std::min(chunk_start + CHUNK_SIZE, num_states_);

                std::vector<std::pair<uint32_t, StateInfoCompact>> updates;

                for (uint64_t id = chunk_start; id < chunk_end; ++id) {
                    StateInfoCompact info;
                    {
                        std::lock_guard<std::mutex> lock(db_mutex_);
                        if (!get_state_info(id, info)) continue;
                    }

                    State s = unpack_state(info.packed);
                    GameResult gr = check_terminal(s);

                    bool changed = false;

                    if (gr == GameResult::WHITE_WINS) {
                        if (s.white_to_move) {
                            info.result = static_cast<uint8_t>(Result::WIN);
                            local_wins++;
                        } else {
                            info.result = static_cast<uint8_t>(Result::LOSS);
                            local_losses++;
                        }
                        changed = true;
                    } else if (gr == GameResult::BLACK_WINS) {
                        if (!s.white_to_move) {
                            info.result = static_cast<uint8_t>(Result::WIN);
                            local_wins++;
                        } else {
                            info.result = static_cast<uint8_t>(Result::LOSS);
                            local_losses++;
                        }
                        changed = true;
                    } else if (info.num_successors == 0) {
                        auto moves = generate_moves(s);
                        if (moves.empty()) {
                            info.result = static_cast<uint8_t>(Result::LOSS);
                            local_losses++;
                            changed = true;
                        }
                    }

                    if (changed) {
                        updates.push_back({static_cast<uint32_t>(id), info});
                    }
                }

                // Write updates under lock
                {
                    std::lock_guard<std::mutex> lock(db_mutex_);
                    for (const auto& [id, info] : updates) {
                        put_state_info(id, info);
                    }
                }
            }

            atomic_wins += local_wins;
            atomic_losses += local_losses;
        });
    }

    // Progress monitoring in main thread
    while (true) {
        uint64_t processed = atomic_enum_processed_;
        if (processed >= num_states_) break;

        if (progress_cb_) {
            progress_cb_("Marking terminals (parallel)", processed, num_states_);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    for (auto& w : workers) {
        w.join();
    }

    num_wins_ = atomic_wins;
    num_losses_ = atomic_losses;

    if (progress_cb_) {
        progress_cb_("Terminals marked", num_states_, num_states_);
    }
}

void RetrogradeSolverDB::propagate() {
    // Use queue on disk for propagation too
    uint64_t prop_head = 0;
    uint64_t prop_tail = 0;

    // Initialize queue with all solved states
    for (uint32_t id = 0; id < num_states_; ++id) {
        StateInfoCompact info;
        if (!get_state_info(id, info)) continue;

        if (static_cast<Result>(info.result) != Result::UNKNOWN) {
            std::string queue_key(reinterpret_cast<char*>(&prop_tail), sizeof(prop_tail));
            std::string queue_val(reinterpret_cast<char*>(&id), sizeof(id));
            db_->Put(rocksdb::WriteOptions(), cf_queue_, queue_key, queue_val);
            prop_tail++;
        }
    }

    uint64_t propagated = 0;

    while (prop_head < prop_tail) {
        // Pop from queue
        std::string queue_key(reinterpret_cast<char*>(&prop_head), sizeof(prop_head));
        std::string queue_val;

        if (!db_->Get(rocksdb::ReadOptions(), cf_queue_, queue_key, &queue_val).ok()) {
            break;
        }

        uint32_t id;
        std::memcpy(&id, queue_val.data(), sizeof(id));
        prop_head++;

        // Delete from queue
        db_->Delete(rocksdb::WriteOptions(), cf_queue_, queue_key);

        StateInfoCompact info;
        if (!get_state_info(id, info)) continue;

        Result child_result = static_cast<Result>(info.result);

        // Get predecessors
        auto preds = get_predecessors(id);

        for (uint32_t pred_id : preds) {
            StateInfoCompact pred_info;
            if (!get_state_info(pred_id, pred_info)) continue;

            // Skip if already solved
            if (static_cast<Result>(pred_info.result) != Result::UNKNOWN) {
                continue;
            }

            bool changed = false;

            if (child_result == Result::LOSS) {
                // Child is LOSS = WIN for us
                pred_info.result = static_cast<uint8_t>(Result::WIN);
                ++num_wins_;
                changed = true;

                // Add to queue
                std::string new_queue_key(reinterpret_cast<char*>(&prop_tail), sizeof(prop_tail));
                std::string new_queue_val(reinterpret_cast<char*>(&pred_id), sizeof(pred_id));
                db_->Put(rocksdb::WriteOptions(), cf_queue_, new_queue_key, new_queue_val);
                prop_tail++;
            } else if (child_result == Result::WIN) {
                pred_info.winning_succs++;

                if (pred_info.winning_succs >= pred_info.num_successors) {
                    pred_info.result = static_cast<uint8_t>(Result::LOSS);
                    ++num_losses_;
                    changed = true;

                    // Add to queue
                    std::string new_queue_key(reinterpret_cast<char*>(&prop_tail), sizeof(prop_tail));
                    std::string new_queue_val(reinterpret_cast<char*>(&pred_id), sizeof(pred_id));
                    db_->Put(rocksdb::WriteOptions(), cf_queue_, new_queue_key, new_queue_val);
                    prop_tail++;
                }
            }

            if (changed || pred_info.winning_succs > 0) {
                put_state_info(pred_id, pred_info);
            }
        }

        ++propagated;
        if (progress_cb_ && propagated % 100000 == 0) {
            progress_cb_("Propagating", propagated, num_states_);
        }
    }

    // Mark remaining UNKNOWN as DRAW
    for (uint32_t id = 0; id < num_states_; ++id) {
        StateInfoCompact info;
        if (!get_state_info(id, info)) continue;

        if (static_cast<Result>(info.result) == Result::UNKNOWN) {
            info.result = static_cast<uint8_t>(Result::DRAW);
            put_state_info(id, info);
            ++num_draws_;
        }
    }

    if (progress_cb_) {
        progress_cb_("Propagation complete", num_states_, num_states_);
    }
}

Result RetrogradeSolverDB::get_result(const State& s) const {
    auto [canonical, _] = canonicalize(s);
    uint64_t packed = pack_state(canonical);
    int64_t id = get_state_id(packed);
    if (id >= 0) {
        StateInfoCompact info;
        if (get_state_info(id, info)) {
            return static_cast<Result>(info.result);
        }
    }
    return Result::UNKNOWN;
}

Move RetrogradeSolverDB::get_best_move(const State& s) const {
    Result my_result = get_result(s);

    auto moves = generate_moves(s);
    if (moves.empty()) {
        return Move{};
    }

    for (const auto& move : moves) {
        State ns = apply_move(s, move);
        Result opp_result = get_result(ns);

        if (my_result == Result::WIN && opp_result == Result::LOSS) {
            return move;
        }
        if (my_result == Result::DRAW && opp_result == Result::DRAW) {
            return move;
        }
        if (my_result == Result::LOSS) {
            if (opp_result == Result::DRAW) return move;
            if (opp_result == Result::WIN) return move;
        }
    }

    return moves[0];
}

Result RetrogradeSolverDB::starting_result() const {
    StateInfoCompact info;
    if (get_state_info(start_id_, info)) {
        return static_cast<Result>(info.result);
    }
    return Result::UNKNOWN;
}

bool RetrogradeSolverDB::import_checkpoint(const std::string& checkpoint_file) {
    std::ifstream in(checkpoint_file, std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open checkpoint file: " << checkpoint_file << "\n";
        return false;
    }

    // Read and verify header
    char magic[4];
    in.read(magic, 4);
    if (std::memcmp(magic, "BBCK", 4) != 0) {
        std::cerr << "Invalid checkpoint file (bad magic)\n";
        return false;
    }

    uint32_t version;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) {
        std::cerr << "Unsupported checkpoint version: " << version << "\n";
        return false;
    }

    // Read phase
    uint32_t phase_val;
    in.read(reinterpret_cast<char*>(&phase_val), sizeof(phase_val));
    phase_ = static_cast<SolvePhaseDB>(phase_val);

    // Read statistics
    in.read(reinterpret_cast<char*>(&num_wins_), sizeof(num_wins_));
    in.read(reinterpret_cast<char*>(&num_losses_), sizeof(num_losses_));
    in.read(reinterpret_cast<char*>(&num_draws_), sizeof(num_draws_));
    in.read(reinterpret_cast<char*>(&start_id_), sizeof(start_id_));
    in.read(reinterpret_cast<char*>(&enum_processed_), sizeof(enum_processed_));

    // Read number of states
    uint64_t file_num_states;
    in.read(reinterpret_cast<char*>(&file_num_states), sizeof(file_num_states));

    std::cerr << "Importing " << file_num_states << " states from checkpoint...\n";

    // Read and write states in batches
    const int BATCH_SIZE = 10000;
    rocksdb::WriteBatch batch;

    for (uint64_t i = 0; i < file_num_states; ++i) {
        uint64_t packed;
        uint8_t result_val;
        uint16_t num_successors;
        uint16_t winning_succs;

        in.read(reinterpret_cast<char*>(&packed), sizeof(packed));
        in.read(reinterpret_cast<char*>(&result_val), sizeof(result_val));
        in.read(reinterpret_cast<char*>(&num_successors), sizeof(num_successors));
        in.read(reinterpret_cast<char*>(&winning_succs), sizeof(winning_succs));

        StateInfoCompact info;
        info.packed = packed;
        info.result = result_val;
        info.num_successors = num_successors;
        info.winning_succs = winning_succs;

        uint32_t id = i;

        std::string id_key(reinterpret_cast<char*>(&id), sizeof(id));
        std::string info_val(reinterpret_cast<char*>(&info), sizeof(info));
        std::string packed_key(reinterpret_cast<char*>(&packed), sizeof(packed));
        std::string id_val(reinterpret_cast<char*>(&id), sizeof(id));

        batch.Put(cf_states_, id_key, info_val);
        batch.Put(cf_packed_to_id_, packed_key, id_val);

        if ((i + 1) % BATCH_SIZE == 0) {
            db_->Write(rocksdb::WriteOptions(), &batch);
            batch.Clear();

            if ((i + 1) % 1000000 == 0) {
                double pct = 100.0 * (i + 1) / file_num_states;
                std::cerr << "\rImporting states: " << (i + 1) << " / " << file_num_states
                          << " (" << std::fixed << std::setprecision(1) << pct << "%)" << std::flush;
            }
        }
    }

    // Write remaining batch
    db_->Write(rocksdb::WriteOptions(), &batch);
    std::cerr << "\nStates import complete.\n";

    num_states_ = file_num_states;

    // Read enum queue
    uint64_t queue_size;
    in.read(reinterpret_cast<char*>(&queue_size), sizeof(queue_size));

    std::cerr << "Importing queue of " << queue_size << " items...\n";

    batch.Clear();
    queue_head_ = 0;
    queue_tail_ = 0;

    for (uint64_t i = 0; i < queue_size; ++i) {
        uint32_t id;
        in.read(reinterpret_cast<char*>(&id), sizeof(id));

        std::string queue_key(reinterpret_cast<char*>(&queue_tail_), sizeof(queue_tail_));
        std::string queue_val(reinterpret_cast<char*>(&id), sizeof(id));
        batch.Put(cf_queue_, queue_key, queue_val);
        queue_tail_++;

        if ((i + 1) % BATCH_SIZE == 0) {
            db_->Write(rocksdb::WriteOptions(), &batch);
            batch.Clear();

            if ((i + 1) % 10000000 == 0) {
                double pct = 100.0 * (i + 1) / queue_size;
                std::cerr << "\rImporting queue: " << (i + 1) << " / " << queue_size
                          << " (" << std::fixed << std::setprecision(1) << pct << "%)" << std::flush;
            }
        }
    }

    db_->Write(rocksdb::WriteOptions(), &batch);

    in.close();
    save_metadata();

    std::cerr << "Import complete: " << num_states_ << " states, queue " << queue_tail_ << " items\n";
    return true;
}

} // namespace bobail
