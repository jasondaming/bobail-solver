#include "retrograde_db.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <rocksdb/write_batch.h>
#include <rocksdb/table.h>
#include <rocksdb/cache.h>

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

    // Block cache for random read performance (2GB - reduced to leave room for bloom filter)
    auto cache = rocksdb::NewLRUCache(2ULL * 1024 * 1024 * 1024);
    rocksdb::BlockBasedTableOptions table_options;
    table_options.block_cache = cache;
    table_options.block_size = 16 * 1024;  // 16KB blocks
    table_options.cache_index_and_filter_blocks = true;
    table_options.pin_l0_filter_and_index_blocks_in_cache = true;

    // Column family options with the table factory
    rocksdb::ColumnFamilyOptions cf_opts;
    cf_opts.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));

    // Column family descriptors
    std::vector<rocksdb::ColumnFamilyDescriptor> cf_descs;
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        rocksdb::kDefaultColumnFamilyName, cf_opts));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        "states", cf_opts));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        "packed_to_id", cf_opts));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        "predecessors", cf_opts));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        "queue", cf_opts));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(
        "metadata", cf_opts));

    std::vector<rocksdb::ColumnFamilyHandle*> cf_handles;
    rocksdb::DB* db_ptr;

    rocksdb::Status status = rocksdb::DB::Open(options, db_path, cf_descs, &cf_handles, &db_ptr);
    if (!status.ok()) {
        std::cerr << "Failed to open database: " << status.ToString() << "\n";
        return false;
    }

    db_.reset(db_ptr);
    // cf_handles order matches cf_descs: default, states, packed_to_id, predecessors, queue, metadata
    cf_states_ = cf_handles[1];
    cf_packed_to_id_ = cf_handles[2];
    cf_predecessors_ = cf_handles[3];
    cf_queue_ = cf_handles[4];
    cf_metadata_ = cf_handles[5];  // metadata CF (not default)
    // Note: cf_handles[0] is the default CF, we don't need it

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
        // Always use streaming approach - it's much faster due to in-memory cache
        build_predecessors_streaming();
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
    std::vector<uint32_t> preds;

    // Use MultiGet for all 17 possible keys (16 thread shards + legacy)
    std::vector<rocksdb::Slice> keys;
    std::vector<std::string> key_storage(17);

    // Thread shard keys (state_id + thread_id byte)
    for (int t = 0; t < 16; ++t) {
        key_storage[t] = std::string(reinterpret_cast<const char*>(&state_id), sizeof(state_id));
        key_storage[t].push_back(static_cast<char>(t));
        keys.push_back(key_storage[t]);
    }

    // Legacy 4-byte key
    key_storage[16] = std::string(reinterpret_cast<const char*>(&state_id), sizeof(state_id));
    keys.push_back(key_storage[16]);

    std::vector<std::string> values(17);

    std::vector<rocksdb::ColumnFamilyHandle*> cfs(17, cf_predecessors_);
    std::vector<rocksdb::Status> statuses = db_->MultiGet(rocksdb::ReadOptions(), cfs, keys, &values);

    for (size_t i = 0; i < 17; ++i) {
        if (statuses[i].ok() && !values[i].empty()) {
            size_t count = values[i].size() / sizeof(uint32_t);
            size_t old_size = preds.size();
            preds.resize(old_size + count);
            std::memcpy(preds.data() + old_size, values[i].data(), values[i].size());
        }
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

// Parallel enumeration using batch processing with bloom filter optimization
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

        // Initialize bloom filter with starting state
        bloom_filter_ = std::make_unique<BloomFilter>(2ULL * 1024 * 1024 * 1024);
        bloom_filter_->add(start_packed);
        bloom_loaded_ = true;
    } else if (!bloom_loaded_ && num_states_ > 0) {
        // DISABLED: Skip bloom filter - 100 min load, ~2.4M/hr without it
        std::cerr << "Skipping bloom filter (instant start mode)\n";
    }

    // Log resume status for debugging
    std::cerr << "Enumeration state: queue_head=" << queue_head_
              << ", queue_tail=" << queue_tail_
              << ", num_states=" << num_states_
              << ", enum_processed=" << enum_processed_ << "\n";

    if (progress_cb_ && queue_head_ > 0) {
        progress_cb_("Resuming parallel enumeration", enum_processed_, num_states_);
    }

    atomic_enum_processed_ = enum_processed_;
    atomic_num_states_ = num_states_;
    stop_workers_ = false;

    // Process queue in batches
    const size_t BATCH_SIZE = 100000;  // Load this many items from disk queue at a time
    uint64_t batch_num = 0;

    while (queue_head_ < queue_tail_) {
        auto batch_start = std::chrono::steady_clock::now();

        // Load a batch of work items into memory
        work_queue_.clear();
        work_queue_head_ = 0;

        size_t batch_end = std::min(queue_head_ + BATCH_SIZE, queue_tail_);

        // TIMING: Queue loading - PARALLEL
        auto t1 = std::chrono::steady_clock::now();

        size_t batch_count = batch_end - queue_head_;
        std::vector<std::vector<uint32_t>> thread_queue_items(num_threads_);
        {
            std::vector<std::thread> load_workers;
            size_t per_thread = (batch_count + num_threads_ - 1) / num_threads_;

            for (int t = 0; t < num_threads_; ++t) {
                size_t start = t * per_thread;
                size_t end = std::min(start + per_thread, batch_count);
                if (start >= batch_count) break;

                load_workers.emplace_back([this, t, start, end, &thread_queue_items]() {
                    std::vector<std::string> key_storage(end - start);
                    std::vector<rocksdb::Slice> keys;
                    keys.reserve(end - start);

                    for (size_t i = start; i < end; ++i) {
                        uint64_t idx = queue_head_ + i;
                        key_storage[i - start] = std::string(reinterpret_cast<char*>(&idx), sizeof(idx));
                        keys.push_back(key_storage[i - start]);
                    }

                    std::vector<std::string> values(end - start);
                    std::vector<rocksdb::ColumnFamilyHandle*> cfs(end - start, cf_queue_);
                    std::vector<rocksdb::Status> statuses = db_->MultiGet(rocksdb::ReadOptions(), cfs, keys, &values);

                    for (size_t i = 0; i < keys.size(); ++i) {
                        if (statuses[i].ok() && values[i].size() == sizeof(uint32_t)) {
                            uint32_t id;
                            std::memcpy(&id, values[i].data(), sizeof(id));
                            thread_queue_items[t].push_back(id);
                        }
                    }
                });
            }

            for (auto& w : load_workers) w.join();
        }

        // Merge queue items (order matters for checkpointing)
        for (const auto& items : thread_queue_items) {
            work_queue_.insert(work_queue_.end(), items.begin(), items.end());
        }

        auto t2 = std::chrono::steady_clock::now();
        auto queue_load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

        if (work_queue_.empty()) {
            queue_head_ = batch_end;
            break;
        }

        // TIMING: State info pre-fetch - PARALLEL
        auto t3 = std::chrono::steady_clock::now();

        std::vector<StateInfoCompact> work_state_info(work_queue_.size());
        {
            std::vector<std::thread> fetch_workers;
            size_t per_thread = (work_queue_.size() + num_threads_ - 1) / num_threads_;

            for (int t = 0; t < num_threads_; ++t) {
                size_t start = t * per_thread;
                size_t end = std::min(start + per_thread, work_queue_.size());
                if (start >= work_queue_.size()) break;

                fetch_workers.emplace_back([this, t, start, end, &work_state_info]() {
                    std::vector<std::string> state_key_storage(end - start);
                    std::vector<rocksdb::Slice> state_keys;
                    state_keys.reserve(end - start);

                    for (size_t i = start; i < end; ++i) {
                        uint32_t id = work_queue_[i];
                        state_key_storage[i - start] = std::string(reinterpret_cast<char*>(&id), sizeof(id));
                        state_keys.push_back(state_key_storage[i - start]);
                    }

                    std::vector<std::string> state_values(end - start);
                    std::vector<rocksdb::ColumnFamilyHandle*> state_cfs(end - start, cf_states_);
                    std::vector<rocksdb::Status> state_statuses = db_->MultiGet(rocksdb::ReadOptions(), state_cfs, state_keys, &state_values);

                    for (size_t i = 0; i < state_keys.size(); ++i) {
                        if (state_statuses[i].ok() && state_values[i].size() == sizeof(StateInfoCompact)) {
                            std::memcpy(&work_state_info[start + i], state_values[i].data(), sizeof(StateInfoCompact));
                        }
                    }
                });
            }

            for (auto& w : fetch_workers) w.join();
        }

        auto t4 = std::chrono::steady_clock::now();
        auto state_fetch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();

        // Structures for collecting new states from all threads
        // Two lists: definitely_new (bloom says not in DB) and maybe_exists (need DB check)
        std::vector<std::vector<uint64_t>> thread_definitely_new(num_threads_);
        std::vector<std::vector<uint64_t>> thread_maybe_exists(num_threads_);
        std::vector<std::vector<std::pair<uint32_t, StateInfoCompact>>> thread_updates(num_threads_);

        // Process work items in parallel - use bloom filter for fast rejection
        std::vector<std::thread> workers;
        for (int t = 0; t < num_threads_; ++t) {
            workers.emplace_back([this, t, &thread_definitely_new, &thread_maybe_exists, &thread_updates, &work_state_info]() {
                auto& definitely_new = thread_definitely_new[t];
                auto& maybe_exists = thread_maybe_exists[t];
                auto& updates = thread_updates[t];

                while (true) {
                    // Grab next work item
                    size_t idx = work_queue_head_.fetch_add(1);
                    if (idx >= work_queue_.size()) break;

                    uint32_t id = work_queue_[idx];

                    // Get pre-fetched state info (no DB read needed!)
                    StateInfoCompact info = work_state_info[idx];
                    if (info.packed == 0) continue;  // Skip if not found

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

                    // Process each successor - use bloom filter for fast rejection
                    for (const auto& move : moves) {
                        State ns = apply_move(s, move);
                        auto [canonical_ns, __] = canonicalize(ns);
                        uint64_t ns_packed = pack_state(canonical_ns);

                        // Bloom filter check:
                        // - If definitely NOT in DB -> add to definitely_new (skip DB lookup)
                        // - If MAYBE in DB -> add to maybe_exists (need DB lookup)
                        if (bloom_filter_ && !bloom_filter_->maybe_contains(ns_packed)) {
                            definitely_new.push_back(ns_packed);
                        } else {
                            maybe_exists.push_back(ns_packed);
                        }
                    }

                    atomic_enum_processed_++;
                }
            });
        }

        // TIMING: Workers
        auto t5 = std::chrono::steady_clock::now();

        // Wait for all workers to complete
        for (auto& w : workers) {
            w.join();
        }

        auto t6 = std::chrono::steady_clock::now();
        auto workers_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t6 - t5).count();

        // TIMING: Merge and DB write
        auto t7 = std::chrono::steady_clock::now();

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

        auto t7a = std::chrono::steady_clock::now();

        // Merge and deduplicate definitely_new states (bloom filter says not in DB)
        std::vector<uint64_t> all_definitely_new;
        for (auto& definitely_new : thread_definitely_new) {
            all_definitely_new.insert(all_definitely_new.end(), definitely_new.begin(), definitely_new.end());
        }
        std::sort(all_definitely_new.begin(), all_definitely_new.end());
        all_definitely_new.erase(std::unique(all_definitely_new.begin(), all_definitely_new.end()), all_definitely_new.end());

        // Merge and deduplicate maybe_exists states (need DB check)
        std::vector<uint64_t> all_maybe_exists;
        for (auto& maybe_exists : thread_maybe_exists) {
            all_maybe_exists.insert(all_maybe_exists.end(), maybe_exists.begin(), maybe_exists.end());
        }
        std::sort(all_maybe_exists.begin(), all_maybe_exists.end());
        all_maybe_exists.erase(std::unique(all_maybe_exists.begin(), all_maybe_exists.end()), all_maybe_exists.end());

        // Remove from maybe_exists any that are already in definitely_new
        std::vector<uint64_t> filtered_maybe_exists;
        std::set_difference(all_maybe_exists.begin(), all_maybe_exists.end(),
                           all_definitely_new.begin(), all_definitely_new.end(),
                           std::back_inserter(filtered_maybe_exists));

        auto t7b = std::chrono::steady_clock::now();

        uint32_t actual_new_count = 0;

        // Helper lambda to add a new state
        auto add_new_state = [&](uint64_t packed) {
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

            // Add to bloom filter
            if (bloom_filter_) {
                bloom_filter_->add(packed);
            }
        };

        // Add all definitely_new states directly - NO DB lookup needed!
        for (uint64_t packed : all_definitely_new) {
            add_new_state(packed);
        }

        auto t7c = std::chrono::steady_clock::now();

        // Only check maybe_exists against DB - PARALLEL MultiGet
        // Split work across threads for faster DB lookups
        std::vector<std::vector<uint64_t>> thread_new_from_check(num_threads_);
        {
            std::vector<std::thread> check_workers;
            size_t per_thread = (filtered_maybe_exists.size() + num_threads_ - 1) / num_threads_;

            for (int t = 0; t < num_threads_; ++t) {
                size_t start = t * per_thread;
                size_t end = std::min(start + per_thread, filtered_maybe_exists.size());
                if (start >= filtered_maybe_exists.size()) break;

                check_workers.emplace_back([this, t, start, end, &filtered_maybe_exists, &thread_new_from_check]() {
                    const size_t MULTIGET_CHUNK = 50000;  // Larger chunks for efficiency
                    auto& new_states = thread_new_from_check[t];

                    for (size_t chunk_start = start; chunk_start < end; chunk_start += MULTIGET_CHUNK) {
                        size_t chunk_end = std::min(chunk_start + MULTIGET_CHUNK, end);

                        std::vector<uint64_t> chunk(filtered_maybe_exists.begin() + chunk_start,
                                                    filtered_maybe_exists.begin() + chunk_end);

                        auto existing_ids = batch_get_state_ids(chunk);

                        for (size_t i = 0; i < chunk.size(); ++i) {
                            if (existing_ids[i] < 0) {  // Not in DB - it's new
                                new_states.push_back(chunk[i]);
                            }
                        }
                    }
                });
            }

            for (auto& w : check_workers) {
                w.join();
            }
        }

        // Add all new states found from checking
        for (const auto& new_states : thread_new_from_check) {
            for (uint64_t packed : new_states) {
                add_new_state(packed);
            }
        }

        auto t7d = std::chrono::steady_clock::now();

        num_states_ += actual_new_count;

        // CRITICAL: Advance queue_head_ BEFORE writing to DB
        // This ensures if we crash after write, we don't re-process the same batch
        queue_head_ = batch_end;

        db_->Write(rocksdb::WriteOptions(), &batch);

        auto t7e = std::chrono::steady_clock::now();

        enum_processed_ = atomic_enum_processed_;

        if (progress_cb_ && enum_processed_ % 100000 < BATCH_SIZE) {
            progress_cb_("Parallel enumeration", enum_processed_, num_states_);
        }

        // CRITICAL: Save metadata every batch to ensure queue_head_ is persisted
        // This is essential for correct resume behavior
        save_metadata();

        auto t8 = std::chrono::steady_clock::now();
        auto merge_write_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t8 - t7).count();
        auto total_batch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t8 - batch_start).count();

        // Granular timing for merge phase
        auto merge_updates_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t7a - t7).count();
        auto sort_dedup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t7b - t7a).count();
        auto add_new_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t7c - t7b).count();
        auto multiget_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t7d - t7c).count();
        auto db_write_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t7e - t7d).count();
        auto save_meta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t8 - t7e).count();

        // Log timing every 10 batches
        batch_num++;
        if (batch_num % 10 == 0) {
            std::cerr << "TIMING batch " << batch_num << ": "
                      << "queue=" << queue_load_ms << "ms, "
                      << "fetch=" << state_fetch_ms << "ms, "
                      << "work=" << workers_ms << "ms | "
                      << "merge_upd=" << merge_updates_ms << "ms, "
                      << "sort=" << sort_dedup_ms << "ms, "
                      << "add=" << add_new_ms << "ms, "
                      << "mget=" << multiget_check_ms << "ms, "
                      << "write=" << db_write_ms << "ms, "
                      << "meta=" << save_meta_ms << "ms | "
                      << "total=" << total_batch_ms << "ms "
                      << "(maybe=" << filtered_maybe_exists.size() << ")\n";
        }

        // Log checkpoint periodically for user visibility
        if (checkpoint_interval_ > 0 && enum_processed_ % checkpoint_interval_ < BATCH_SIZE) {
            std::cerr << "Checkpoint: " << enum_processed_ << " processed, "
                      << num_states_ << " states, queue " << queue_head_ << "/" << queue_tail_ << "\n";
        }
    }

    // Final save and verification
    save_metadata();
    std::cerr << "Enumeration complete: " << num_states_ << " states, "
              << enum_processed_ << " processed, queue " << queue_head_ << "/" << queue_tail_ << "\n";

    // Clean up bloom filter
    bloom_filter_.reset();
    bloom_loaded_ = false;

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
    const uint64_t CHUNK_SIZE = 10000;  // Smaller chunks for more responsive progress

    // Thread-local storage for predecessor relations to minimize lock contention
    std::vector<std::map<uint32_t, std::vector<uint32_t>>> thread_local_preds(num_threads_);
    std::atomic<uint64_t> total_relations{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < num_threads_; ++t) {
        workers.emplace_back([this, t, CHUNK_SIZE, &thread_local_preds, &total_relations]() {
            auto& local_preds = thread_local_preds[t];
            uint64_t local_relations = 0;

            while (true) {
                // Grab next chunk
                uint64_t chunk_start = atomic_enum_processed_.fetch_add(CHUNK_SIZE);
                if (chunk_start >= num_states_) break;

                uint64_t chunk_end = std::min(chunk_start + CHUNK_SIZE, num_states_);

                for (uint64_t id = chunk_start; id < chunk_end; ++id) {
                    StateInfoCompact info;
                    // RocksDB reads are thread-safe, no lock needed
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

                        // RocksDB reads are thread-safe
                        int64_t succ_id = get_state_id(ns_packed);

                        if (succ_id >= 0) {
                            local_preds[static_cast<uint32_t>(succ_id)].push_back(static_cast<uint32_t>(id));
                            ++local_relations;
                        }
                    }
                }

                // Periodically flush to DB if local storage gets large (every ~500k relations)
                if (local_relations > 500000) {
                    std::lock_guard<std::mutex> lock(db_mutex_);
                    rocksdb::WriteBatch batch;
                    for (auto& [succ_id, pred_ids] : local_preds) {
                        std::string key(reinterpret_cast<const char*>(&succ_id), sizeof(uint32_t));
                        std::string existing;
                        db_->Get(rocksdb::ReadOptions(), cf_predecessors_, key, &existing);
                        for (uint32_t pred_id : pred_ids) {
                            existing.append(reinterpret_cast<const char*>(&pred_id), sizeof(pred_id));
                        }
                        batch.Put(cf_predecessors_, key, existing);
                    }
                    db_->Write(rocksdb::WriteOptions(), &batch);
                    total_relations += local_relations;
                    local_preds.clear();
                    local_relations = 0;
                }
            }

            // Final flush of remaining local data
            if (!local_preds.empty()) {
                std::lock_guard<std::mutex> lock(db_mutex_);
                rocksdb::WriteBatch batch;
                for (auto& [succ_id, pred_ids] : local_preds) {
                    std::string key(reinterpret_cast<const char*>(&succ_id), sizeof(uint32_t));
                    std::string existing;
                    db_->Get(rocksdb::ReadOptions(), cf_predecessors_, key, &existing);
                    for (uint32_t pred_id : pred_ids) {
                        existing.append(reinterpret_cast<const char*>(&pred_id), sizeof(pred_id));
                    }
                    batch.Put(cf_predecessors_, key, existing);
                }
                db_->Write(rocksdb::WriteOptions(), &batch);
                total_relations += local_relations;
            }
        });
    }

    // Progress monitoring in main thread
    auto start_time = std::chrono::steady_clock::now();
    uint64_t last_processed = 0;
    while (true) {
        uint64_t processed = atomic_enum_processed_;
        if (processed >= num_states_) break;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        if (progress_cb_) {
            progress_cb_("Building predecessors", processed, num_states_);
        }

        // Log rate every 10 seconds
        if (elapsed > 0 && elapsed % 10 == 0 && processed != last_processed) {
            double rate = static_cast<double>(processed) / elapsed;
            double eta_sec = (num_states_ - processed) / rate;
            std::cerr << "\n  Rate: " << std::fixed << std::setprecision(0) << rate
                      << " states/sec, ETA: " << std::setprecision(1) << eta_sec / 60.0 << " min" << std::flush;
            last_processed = processed;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    for (auto& w : workers) {
        w.join();
    }

    std::cerr << "\n  Total predecessor relations: " << total_relations << "\n";

    if (progress_cb_) {
        progress_cb_("Predecessors complete", num_states_, num_states_);
    }
}

void RetrogradeSolverDB::load_packed_to_id_cache() {
    if (cache_loaded_) return;

    std::cerr << "Loading packed_to_id mapping into memory (~"
              << (num_states_ * 12 / (1024*1024)) << " MB using sorted vector)...\n";

    auto start_time = std::chrono::steady_clock::now();

    // Reserve space upfront - 12 bytes per entry vs ~40 for unordered_map
    packed_to_id_cache_.reserve(num_states_ + 1000000);

    // Use iterator to scan all keys sequentially
    rocksdb::ReadOptions read_opts;
    read_opts.fill_cache = false;  // Don't pollute block cache

    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_opts, cf_packed_to_id_));

    uint64_t count = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        uint64_t packed;
        uint32_t id;
        std::memcpy(&packed, it->key().data(), sizeof(packed));
        std::memcpy(&id, it->value().data(), sizeof(id));
        packed_to_id_cache_.push_back({packed, id});

        if (++count % 10000000 == 0) {
            if (progress_cb_) {
                progress_cb_("Loading cache", count, num_states_);
            }
        }
    }

    auto load_time = std::chrono::steady_clock::now();
    auto load_elapsed = std::chrono::duration_cast<std::chrono::seconds>(load_time - start_time).count();
    std::cerr << "Loaded " << packed_to_id_cache_.size() << " entries in " << load_elapsed << "s, sorting...\n";

    // Sort for binary search
    std::sort(packed_to_id_cache_.begin(), packed_to_id_cache_.end());

    auto end_time = std::chrono::steady_clock::now();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();

    std::cerr << "Cache ready: " << packed_to_id_cache_.size() << " entries, total time "
              << total_elapsed << "s\n";
    cache_loaded_ = true;
}

void RetrogradeSolverDB::load_bloom_filter() {
    if (bloom_loaded_) return;

    std::cerr << "Loading bloom filter for " << num_states_ << " states (2GB)...\n";
    auto start_time = std::chrono::steady_clock::now();

    // Allocate 2GB bloom filter
    bloom_filter_ = std::make_unique<BloomFilter>(2ULL * 1024 * 1024 * 1024);

    // Scan all packed states and add to bloom filter
    rocksdb::ReadOptions read_opts;
    read_opts.fill_cache = false;

    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_opts, cf_packed_to_id_));

    uint64_t count = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        uint64_t packed;
        std::memcpy(&packed, it->key().data(), sizeof(packed));
        bloom_filter_->add(packed);

        if (++count % 10000000 == 0) {
            std::cerr << "  Bloom filter: loaded " << count << " / " << num_states_ << "\n";
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();

    std::cerr << "Bloom filter ready: " << count << " entries, " << elapsed << "s\n";
    bloom_loaded_ = true;
}

std::vector<int64_t> RetrogradeSolverDB::batch_get_state_ids(const std::vector<uint64_t>& packed_states) const {
    std::vector<int64_t> results(packed_states.size(), -1);

    if (packed_states.empty()) return results;

    // Prepare keys for MultiGet
    std::vector<rocksdb::Slice> keys;
    std::vector<std::string> key_storage(packed_states.size());

    for (size_t i = 0; i < packed_states.size(); ++i) {
        key_storage[i] = std::string(reinterpret_cast<const char*>(&packed_states[i]), sizeof(uint64_t));
        keys.push_back(key_storage[i]);
    }

    // Do batch lookup
    std::vector<std::string> values(packed_states.size());
    std::vector<rocksdb::ColumnFamilyHandle*> cfs(packed_states.size(), cf_packed_to_id_);
    std::vector<rocksdb::Status> statuses = db_->MultiGet(rocksdb::ReadOptions(), cfs, keys, &values);

    // Extract results
    for (size_t i = 0; i < packed_states.size(); ++i) {
        if (statuses[i].ok() && values[i].size() == sizeof(uint32_t)) {
            uint32_t id;
            std::memcpy(&id, values[i].data(), sizeof(id));
            results[i] = id;
        }
    }

    return results;
}

void RetrogradeSolverDB::build_predecessors_streaming() {
    // Step 1: Load packed_to_id into memory for O(1) lookups
    load_packed_to_id_cache();

    // Multi-threaded with SIZE-based flushing (not count-based)
    // Each thread flushes when its buffer exceeds MAX_BUFFER_ENTRIES
    std::cerr << "Building predecessors (multi-threaded, memory-bounded)...\n";

    atomic_enum_processed_ = 0;
    std::atomic<uint64_t> total_relations{0};

    auto start_time = std::chrono::steady_clock::now();

    // Each thread flushes when buffer exceeds this many predecessor entries
    // 1M entries * 8 bytes = 8MB per thread * 8 threads = ~64MB max for buffers
    // Plus 5GB for cache = ~5.1GB total, well under 29GB RAM
    const size_t MAX_BUFFER_ENTRIES = 1000000;

    std::mutex db_write_mutex;

    // Work queue
    struct WorkItem {
        uint32_t id;
        uint64_t packed;
    };

    const size_t QUEUE_SIZE = 100000;
    std::vector<WorkItem> work_queue(QUEUE_SIZE);
    std::atomic<size_t> queue_write{0};
    std::atomic<size_t> queue_read{0};
    std::atomic<bool> producer_done{false};

    // Worker threads
    std::vector<std::thread> workers;
    for (int t = 0; t < num_threads_; ++t) {
        workers.emplace_back([this, t, &work_queue, &queue_write, &queue_read, &producer_done,
                              &total_relations, QUEUE_SIZE, &db_write_mutex, MAX_BUFFER_ENTRIES]() {
            std::unordered_map<uint32_t, std::vector<uint32_t>> local_preds;
            size_t local_pred_count = 0;  // Count of predecessor ENTRIES (not states)

            auto flush_buffer = [&]() {
                if (local_preds.empty()) return;

                // Write-only approach: no reads, just append
                // Use compound key: (succ_id << 4) | thread_id to avoid conflicts
                // Later reading will merge all thread entries for same succ_id
                std::lock_guard<std::mutex> lock(db_write_mutex);
                rocksdb::WriteBatch batch;
                uint64_t flushed = 0;
                for (auto& [succ_id, pred_ids] : local_preds) {
                    // Compound key: succ_id (4 bytes) + thread_id (1 byte)
                    std::string key(reinterpret_cast<const char*>(&succ_id), sizeof(succ_id));
                    key.push_back(static_cast<char>(t));  // Thread ID suffix

                    std::string value;
                    for (uint32_t pred_id : pred_ids) {
                        value.append(reinterpret_cast<const char*>(&pred_id), sizeof(pred_id));
                        ++flushed;
                    }
                    batch.Put(cf_predecessors_, key, value);
                }
                db_->Write(rocksdb::WriteOptions(), &batch);
                total_relations += flushed;
                local_preds.clear();
                local_pred_count = 0;
            };

            while (true) {
                // Try to get work
                size_t my_read = queue_read.load(std::memory_order_relaxed);
                size_t write_pos = queue_write.load(std::memory_order_acquire);

                if (my_read >= write_pos) {
                    if (producer_done.load(std::memory_order_acquire)) {
                        write_pos = queue_write.load(std::memory_order_acquire);
                        if (my_read >= write_pos) break;
                    }
                    std::this_thread::yield();
                    continue;
                }

                if (!queue_read.compare_exchange_weak(my_read, my_read + 1,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    continue;
                }

                const WorkItem& item = work_queue[my_read % QUEUE_SIZE];
                State s = unpack_state(item.packed);

                if (check_terminal(s) == GameResult::ONGOING) {
                    auto moves = generate_moves(s);
                    for (const auto& move : moves) {
                        State ns = apply_move(s, move);
                        auto [canonical_ns, _] = canonicalize(ns);
                        uint64_t ns_packed = pack_state(canonical_ns);

                        PackedIdPair search_key{ns_packed, 0};
                        auto cache_it = std::lower_bound(packed_to_id_cache_.begin(),
                                                         packed_to_id_cache_.end(), search_key);
                        if (cache_it != packed_to_id_cache_.end() && cache_it->packed == ns_packed) {
                            local_preds[cache_it->id].push_back(item.id);
                            ++local_pred_count;
                        }
                    }
                }

                ++atomic_enum_processed_;

                // Flush when buffer gets too large (SIZE-based, not count-based)
                if (local_pred_count >= MAX_BUFFER_ENTRIES) {
                    flush_buffer();
                }
            }

            // Final flush
            flush_buffer();
        });
    }

    // Producer thread
    std::thread producer([this, &work_queue, &queue_write, &queue_read, &producer_done, QUEUE_SIZE]() {
        rocksdb::ReadOptions read_opts;
        read_opts.fill_cache = false;

        std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_opts, cf_states_));

        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            StateInfoCompact info;
            std::memcpy(&info, it->value().data(), sizeof(info));

            uint32_t id;
            std::memcpy(&id, it->key().data(), sizeof(id));

            size_t write_pos;
            while (true) {
                write_pos = queue_write.load(std::memory_order_relaxed);
                size_t read_pos = queue_read.load(std::memory_order_acquire);
                if (write_pos - read_pos < QUEUE_SIZE) break;
                std::this_thread::yield();
            }

            work_queue[write_pos % QUEUE_SIZE] = {id, info.packed};
            queue_write.store(write_pos + 1, std::memory_order_release);
        }

        producer_done.store(true, std::memory_order_release);
    });

    // Progress monitoring
    while (true) {
        uint64_t processed = atomic_enum_processed_;
        if (processed >= num_states_) break;

        bool all_done = producer_done.load() && (queue_read.load() >= queue_write.load());
        if (all_done) break;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        if (progress_cb_) {
            progress_cb_("Building predecessors", processed, num_states_);
        }

        if (elapsed > 0 && processed > 0) {
            double rate = static_cast<double>(processed) / elapsed;
            double eta_sec = (num_states_ - processed) / rate;
            std::cerr << "\n  [" << processed << "/" << num_states_ << "] Rate: "
                      << std::fixed << std::setprecision(0) << rate
                      << " states/sec, ETA: " << std::setprecision(1) << eta_sec / 60.0 << " min" << std::flush;
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    producer.join();
    for (auto& w : workers) {
        w.join();
    }

    auto compute_end = std::chrono::steady_clock::now();
    auto compute_sec = std::chrono::duration_cast<std::chrono::seconds>(compute_end - start_time).count();
    std::cerr << "\n\nPredecessor building done in " << compute_sec << "s\n";
    std::cerr << "Total predecessor relations: " << total_relations << "\n";

    auto end_time = std::chrono::steady_clock::now();
    auto total_sec = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();

    std::cerr << "\nPredecessors complete!\n";
    std::cerr << "  Total relations: " << total_relations << "\n";
    std::cerr << "  Time: " << total_sec << " seconds\n";
    std::cerr << "  Avg rate: " << (num_states_ / std::max(1L, (long)total_sec)) << " states/sec\n";

    packed_to_id_cache_.clear();
    cache_loaded_ = false;

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
            // num_successors wasn't set during enumeration (resume bug) - fix it now
            auto moves = generate_moves(s);
            if (moves.empty()) {
                info.result = static_cast<uint8_t>(Result::LOSS);
                ++num_losses_;
            } else {
                // Set the missing num_successors count
                info.num_successors = moves.size();
            }
            changed = true;
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
    // Optimized: Use sequential iterator scan instead of random reads
    // Single-threaded scan is faster than parallel random I/O
    // Plus checkpointing every 1M states

    const uint64_t CHECKPOINT_INTERVAL = 1000000;
    const uint64_t BATCH_SIZE = 10000;

    // Load checkpoint if exists
    uint64_t start_id = 0;
    std::string ckpt_key = "terminal_checkpoint";
    std::string ckpt_value;
    if (db_->Get(rocksdb::ReadOptions(), cf_metadata_, ckpt_key, &ckpt_value).ok() && ckpt_value.size() >= 8) {
        std::memcpy(&start_id, ckpt_value.data(), sizeof(start_id));
        std::cerr << "Resuming terminal marking from checkpoint: " << start_id << std::endl;
    }

    uint64_t local_wins = 0;
    uint64_t local_losses = 0;
    uint64_t processed = start_id;

    // Use iterator for sequential scan - MUCH faster than random reads
    rocksdb::ReadOptions read_opts;
    read_opts.fill_cache = false;  // Don't pollute cache with sequential scan
    read_opts.readahead_size = 2 * 1024 * 1024;  // 2MB readahead

    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_opts, cf_states_));

    // Seek to start position
    uint32_t start_key = static_cast<uint32_t>(start_id);
    it->Seek(rocksdb::Slice(reinterpret_cast<char*>(&start_key), sizeof(start_key)));

    rocksdb::WriteBatch batch;
    uint64_t batch_count = 0;
    auto last_checkpoint = std::chrono::steady_clock::now();

    while (it->Valid()) {
        // Extract state ID from key
        if (it->key().size() != sizeof(uint32_t)) {
            it->Next();
            continue;
        }
        uint32_t id;
        std::memcpy(&id, it->key().data(), sizeof(id));

        // Extract state info from value
        if (it->value().size() != sizeof(StateInfoCompact)) {
            it->Next();
            continue;
        }
        StateInfoCompact info;
        std::memcpy(&info, it->value().data(), sizeof(info));

        // Check terminal status
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
            // num_successors wasn't set during enumeration (resume bug) - fix it now
            auto moves = generate_moves(s);
            if (moves.empty()) {
                info.result = static_cast<uint8_t>(Result::LOSS);
                local_losses++;
            } else {
                // Set the missing num_successors count
                info.num_successors = moves.size();
            }
            changed = true;
        }

        if (changed) {
            std::string key(reinterpret_cast<char*>(&id), sizeof(id));
            std::string value(reinterpret_cast<char*>(&info), sizeof(info));
            batch.Put(cf_states_, key, value);
            batch_count++;
        }

        processed++;

        // Write batch when full
        if (batch_count >= BATCH_SIZE) {
            db_->Write(rocksdb::WriteOptions(), &batch);
            batch.Clear();
            batch_count = 0;
        }

        // Checkpoint every CHECKPOINT_INTERVAL states or every 60 seconds
        auto now = std::chrono::steady_clock::now();
        bool time_for_checkpoint = std::chrono::duration_cast<std::chrono::seconds>(now - last_checkpoint).count() >= 60;

        if ((processed % CHECKPOINT_INTERVAL == 0) || time_for_checkpoint) {
            // Flush any pending batch
            if (batch_count > 0) {
                db_->Write(rocksdb::WriteOptions(), &batch);
                batch.Clear();
                batch_count = 0;
            }

            // Save checkpoint
            std::string ckpt_val(reinterpret_cast<char*>(&processed), sizeof(processed));
            db_->Put(rocksdb::WriteOptions(), cf_metadata_, ckpt_key, ckpt_val);

            last_checkpoint = now;

            if (progress_cb_) {
                progress_cb_("Marking terminals", processed, num_states_);
            }
        }

        // Progress update every 10k
        if (processed % 10000 == 0 && progress_cb_) {
            progress_cb_("Marking terminals", processed, num_states_);
        }

        it->Next();
    }

    // Write any remaining batch
    if (batch_count > 0) {
        db_->Write(rocksdb::WriteOptions(), &batch);
    }

    // Clear checkpoint (phase complete)
    db_->Delete(rocksdb::WriteOptions(), cf_metadata_, ckpt_key);

    num_wins_ = local_wins;
    num_losses_ = local_losses;

    std::cerr << "Terminal marking complete: " << local_wins << " wins, " << local_losses << " losses" << std::endl;

    if (progress_cb_) {
        progress_cb_("Terminals marked", num_states_, num_states_);
    }
}

void RetrogradeSolverDB::propagate() {
    // Optimized propagation with iterator-based initialization and batching

    uint64_t prop_head = 0;
    uint64_t prop_tail = 0;

    // Check for checkpoint
    std::string ckpt_key = "prop_checkpoint";
    std::string ckpt_value;
    bool resumed = false;
    if (db_->Get(rocksdb::ReadOptions(), cf_metadata_, ckpt_key, &ckpt_value).ok() && ckpt_value.size() >= 24) {
        std::memcpy(&prop_head, ckpt_value.data(), sizeof(prop_head));
        std::memcpy(&prop_tail, ckpt_value.data() + 8, sizeof(prop_tail));
        uint64_t propagated_ckpt;
        std::memcpy(&propagated_ckpt, ckpt_value.data() + 16, sizeof(propagated_ckpt));
        std::cerr << "Resuming propagation from checkpoint: head=" << prop_head
                  << ", tail=" << prop_tail << ", propagated=" << propagated_ckpt << std::endl;
        resumed = true;
    }

    // Phase 1: Initialize queue with solved states (using iterator for speed)
    if (!resumed) {
        if (progress_cb_) progress_cb_("Building propagation queue", 0, num_states_);

        rocksdb::ReadOptions read_opts;
        read_opts.fill_cache = false;
        read_opts.readahead_size = 2 * 1024 * 1024;

        std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_opts, cf_states_));

        rocksdb::WriteBatch batch;
        uint64_t batch_count = 0;
        const uint64_t BATCH_SIZE = 10000;
        uint64_t scanned = 0;

        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            if (it->value().size() != sizeof(StateInfoCompact)) continue;

            StateInfoCompact info;
            std::memcpy(&info, it->value().data(), sizeof(info));

            if (static_cast<Result>(info.result) != Result::UNKNOWN) {
                // Add to queue
                uint32_t id;
                std::memcpy(&id, it->key().data(), sizeof(id));

                std::string queue_key(reinterpret_cast<char*>(&prop_tail), sizeof(prop_tail));
                std::string queue_val(reinterpret_cast<char*>(&id), sizeof(id));
                batch.Put(cf_queue_, queue_key, queue_val);
                prop_tail++;
                batch_count++;

                if (batch_count >= BATCH_SIZE) {
                    db_->Write(rocksdb::WriteOptions(), &batch);
                    batch.Clear();
                    batch_count = 0;
                }
            }

            scanned++;
            if (progress_cb_ && scanned % 1000000 == 0) {
                progress_cb_("Building propagation queue", scanned, num_states_);
            }
        }

        // Write remaining batch
        if (batch_count > 0) {
            db_->Write(rocksdb::WriteOptions(), &batch);
        }

        std::cerr << "Propagation queue built: " << prop_tail << " solved states to process" << std::endl;
    }

    // Phase 2: Propagate results - parallel approach
    std::atomic<uint64_t> atomic_prop_head(prop_head);
    std::atomic<uint64_t> atomic_prop_tail(prop_tail);
    std::atomic<uint64_t> atomic_propagated(0);
    std::atomic<uint64_t> atomic_wins(0);
    std::atomic<uint64_t> atomic_losses(0);

    auto start_time = std::chrono::steady_clock::now();
    std::atomic<uint64_t> last_checkpoint_time(0);
    std::mutex checkpoint_mutex;

    // Per-state locks to handle concurrent updates to the same predecessor
    const size_t NUM_LOCKS = 65536;
    std::vector<std::mutex> state_locks(NUM_LOCKS);

    auto worker = [&](int thread_id) {
        rocksdb::WriteBatch local_batch;
        uint64_t local_batch_count = 0;
        const uint64_t LOCAL_BATCH_SIZE = 1000;

        while (true) {
            // Get next queue position atomically
            uint64_t my_pos = atomic_prop_head.fetch_add(1);
            uint64_t current_tail = atomic_prop_tail.load();

            if (my_pos >= current_tail) {
                // Check again in case tail grew
                current_tail = atomic_prop_tail.load();
                if (my_pos >= current_tail) {
                    // Flush remaining batch
                    if (local_batch_count > 0) {
                        db_->Write(rocksdb::WriteOptions(), &local_batch);
                    }
                    break;
                }
            }

            // Read queue item
            std::string queue_key(reinterpret_cast<char*>(&my_pos), sizeof(my_pos));
            std::string queue_value;
            auto status = db_->Get(rocksdb::ReadOptions(), cf_queue_, queue_key, &queue_value);

            if (!status.ok() || queue_value.size() != sizeof(uint32_t)) {
                atomic_propagated.fetch_add(1);
                continue;
            }

            uint32_t id;
            std::memcpy(&id, queue_value.data(), sizeof(id));

            // Read state info
            std::string state_key(reinterpret_cast<char*>(&id), sizeof(id));
            std::string state_value;
            status = db_->Get(rocksdb::ReadOptions(), cf_states_, state_key, &state_value);

            if (!status.ok() || state_value.size() != sizeof(StateInfoCompact)) {
                atomic_propagated.fetch_add(1);
                continue;
            }

            StateInfoCompact info;
            std::memcpy(&info, state_value.data(), sizeof(info));
            Result child_result = static_cast<Result>(info.result);

            // Get predecessors
            auto preds = get_predecessors(id);

            for (uint32_t pred_id : preds) {
                // Lock this predecessor's slot
                size_t lock_idx = pred_id % NUM_LOCKS;
                std::lock_guard<std::mutex> lock(state_locks[lock_idx]);

                // Read predecessor state info (while holding lock)
                std::string pred_key(reinterpret_cast<char*>(&pred_id), sizeof(pred_id));
                std::string pred_value;
                status = db_->Get(rocksdb::ReadOptions(), cf_states_, pred_key, &pred_value);

                if (!status.ok() || pred_value.size() != sizeof(StateInfoCompact)) {
                    continue;
                }

                StateInfoCompact pred_info;
                std::memcpy(&pred_info, pred_value.data(), sizeof(pred_info));

                // Skip if already solved
                if (static_cast<Result>(pred_info.result) != Result::UNKNOWN) {
                    continue;
                }

                bool changed = false;

                if (child_result == Result::LOSS) {
                    // Child is LOSS = WIN for us
                    pred_info.result = static_cast<uint8_t>(Result::WIN);
                    atomic_wins.fetch_add(1);
                    changed = true;

                    // Add to queue
                    uint64_t new_tail = atomic_prop_tail.fetch_add(1);
                    std::string new_queue_key(reinterpret_cast<char*>(&new_tail), sizeof(new_tail));
                    std::string new_queue_val(reinterpret_cast<char*>(&pred_id), sizeof(pred_id));
                    local_batch.Put(cf_queue_, new_queue_key, new_queue_val);
                    local_batch_count++;
                } else if (child_result == Result::WIN) {
                    pred_info.winning_succs++;

                    if (pred_info.winning_succs >= pred_info.num_successors) {
                        pred_info.result = static_cast<uint8_t>(Result::LOSS);
                        atomic_losses.fetch_add(1);
                        changed = true;

                        // Add to queue
                        uint64_t new_tail = atomic_prop_tail.fetch_add(1);
                        std::string new_queue_key(reinterpret_cast<char*>(&new_tail), sizeof(new_tail));
                        std::string new_queue_val(reinterpret_cast<char*>(&pred_id), sizeof(pred_id));
                        local_batch.Put(cf_queue_, new_queue_key, new_queue_val);
                        local_batch_count++;
                    }
                }

                if (changed || pred_info.winning_succs > 0) {
                    std::string key(reinterpret_cast<char*>(&pred_id), sizeof(pred_id));
                    std::string value(reinterpret_cast<char*>(&pred_info), sizeof(pred_info));
                    local_batch.Put(cf_states_, key, value);
                    local_batch_count++;
                }
            }

            atomic_propagated.fetch_add(1);

            // Flush local batch periodically
            if (local_batch_count >= LOCAL_BATCH_SIZE) {
                db_->Write(rocksdb::WriteOptions(), &local_batch);
                local_batch.Clear();
                local_batch_count = 0;
            }
        }
    };

    // Progress reporting thread
    std::atomic<bool> done(false);
    std::thread progress_thread([&]() {
        while (!done.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));

            uint64_t current_propagated = atomic_propagated.load();
            uint64_t current_tail = atomic_prop_tail.load();

            if (progress_cb_) {
                progress_cb_("Propagating", current_propagated, current_tail);
            }

            // Checkpoint every 60 seconds
            auto now = std::chrono::steady_clock::now();
            uint64_t elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            uint64_t last_ckpt = last_checkpoint_time.load();

            if (elapsed >= last_ckpt + 60) {
                std::lock_guard<std::mutex> lock(checkpoint_mutex);
                // Double-check after acquiring lock
                if (elapsed >= last_checkpoint_time.load() + 60) {
                    uint64_t current_head = atomic_prop_head.load();
                    std::string ckpt_val(24, '\0');
                    std::memcpy(&ckpt_val[0], &current_head, sizeof(current_head));
                    std::memcpy(&ckpt_val[8], &current_tail, sizeof(current_tail));
                    std::memcpy(&ckpt_val[16], &current_propagated, sizeof(current_propagated));
                    db_->Put(rocksdb::WriteOptions(), cf_metadata_, ckpt_key, ckpt_val);
                    last_checkpoint_time.store(elapsed);
                }
            }
        }
    });

    // Launch worker threads
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads_; ++t) {
        threads.emplace_back(worker, t);
    }

    // Wait for all workers to complete
    for (auto& t : threads) {
        t.join();
    }

    done.store(true);
    progress_thread.join();

    // Update counters
    num_wins_ += atomic_wins.load();
    num_losses_ += atomic_losses.load();
    prop_head = atomic_prop_head.load();
    prop_tail = atomic_prop_tail.load();
    uint64_t propagated = atomic_propagated.load();

    // Final checkpoint
    {
        std::string ckpt_val(24, '\0');
        std::memcpy(&ckpt_val[0], &prop_head, sizeof(prop_head));
        std::memcpy(&ckpt_val[8], &prop_tail, sizeof(prop_tail));
        std::memcpy(&ckpt_val[16], &propagated, sizeof(propagated));
        db_->Put(rocksdb::WriteOptions(), cf_metadata_, ckpt_key, ckpt_val);
    }

    std::cerr << "Propagation complete: processed " << propagated << " states" << std::endl;

    // Phase 3: Mark remaining UNKNOWN as DRAW (using iterator)
    if (progress_cb_) progress_cb_("Marking draws", 0, num_states_);

    rocksdb::ReadOptions read_opts;
    read_opts.fill_cache = false;
    read_opts.readahead_size = 2 * 1024 * 1024;

    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_opts, cf_states_));

    rocksdb::WriteBatch batch;
    uint64_t batch_count = 0;
    uint64_t draws_marked = 0;
    uint64_t scanned = 0;

    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        if (it->value().size() != sizeof(StateInfoCompact)) continue;

        StateInfoCompact info;
        std::memcpy(&info, it->value().data(), sizeof(info));

        if (static_cast<Result>(info.result) == Result::UNKNOWN) {
            info.result = static_cast<uint8_t>(Result::DRAW);

            std::string key(it->key().data(), it->key().size());
            std::string value(reinterpret_cast<char*>(&info), sizeof(info));
            batch.Put(cf_states_, key, value);
            batch_count++;
            draws_marked++;
            ++num_draws_;

            if (batch_count >= 10000) {
                db_->Write(rocksdb::WriteOptions(), &batch);
                batch.Clear();
                batch_count = 0;
            }
        }

        scanned++;
        if (progress_cb_ && scanned % 1000000 == 0) {
            progress_cb_("Marking draws", scanned, num_states_);
        }
    }

    if (batch_count > 0) {
        db_->Write(rocksdb::WriteOptions(), &batch);
    }

    // Clear checkpoint
    db_->Delete(rocksdb::WriteOptions(), cf_metadata_, ckpt_key);

    std::cerr << "Marked " << draws_marked << " states as draws" << std::endl;

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
