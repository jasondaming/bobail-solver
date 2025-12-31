#pragma once

#include "board.h"
#include "movegen.h"
#include "tt.h"
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <condition_variable>
#include <unordered_map>
#include <rocksdb/db.h>
#include <rocksdb/options.h>

namespace bobail {

// Simple bloom filter for fast duplicate rejection
// Uses 2GB of memory for ~5 billion entries with ~1% false positive rate
class BloomFilter {
public:
    BloomFilter(size_t size_bytes = 2ULL * 1024 * 1024 * 1024)
        : bits_(size_bytes * 8, false), num_bits_(size_bytes * 8) {}

    void add(uint64_t value) {
        for (int i = 0; i < NUM_HASHES; ++i) {
            size_t idx = hash(value, i) % num_bits_;
            bits_[idx] = true;
        }
    }

    bool maybe_contains(uint64_t value) const {
        for (int i = 0; i < NUM_HASHES; ++i) {
            size_t idx = hash(value, i) % num_bits_;
            if (!bits_[idx]) return false;
        }
        return true;
    }

    void clear() {
        std::fill(bits_.begin(), bits_.end(), false);
    }

    size_t memory_bytes() const { return bits_.size() / 8; }

private:
    static constexpr int NUM_HASHES = 7;

    size_t hash(uint64_t value, int seed) const {
        // MurmurHash3-style mixing
        uint64_t h = value ^ (seed * 0x9e3779b97f4a7c15ULL);
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return static_cast<size_t>(h);
    }

    std::vector<bool> bits_;
    size_t num_bits_;
};

// Phases of retrograde solving
enum class SolvePhaseDB {
    NOT_STARTED = 0,
    ENUMERATING = 1,
    BUILDING_PREDECESSORS = 2,
    MARKING_TERMINALS = 3,
    PROPAGATING = 4,
    COMPLETE = 5
};

// Compact state info stored on disk (fixed size, no variable-length predecessors)
struct StateInfoCompact {
    uint64_t packed;            // Canonical packed state
    uint8_t result;             // WIN/LOSS/DRAW/UNKNOWN
    uint16_t num_successors;    // Number of legal moves
    uint16_t winning_succs;     // Count of successors that are losses (for opponent)
};

// Disk-based retrograde solver using RocksDB
class RetrogradeSolverDB {
public:
    using ProgressCallback = std::function<void(const char* phase, uint64_t current, uint64_t total)>;

    RetrogradeSolverDB();
    ~RetrogradeSolverDB();

    // Initialize the database
    bool open(const std::string& db_path);
    void close();

    // Run the full solve process
    bool solve();

    // Get result for a specific state (after solving)
    Result get_result(const State& s) const;

    // Get optimal move from a position (after solving)
    Move get_best_move(const State& s) const;

    // Statistics
    uint64_t num_states() const { return num_states_; }
    uint64_t num_wins() const { return num_wins_; }
    uint64_t num_losses() const { return num_losses_; }
    uint64_t num_draws() const { return num_draws_; }

    // Set progress callback
    void set_progress_callback(ProgressCallback cb) { progress_cb_ = cb; }

    // Get the result for the starting position
    Result starting_result() const;

    // Set checkpoint interval
    void set_checkpoint_interval(uint64_t interval) { checkpoint_interval_ = interval; }

    // Get current phase
    SolvePhaseDB current_phase() const { return phase_; }

    // Set number of threads for parallel processing
    void set_num_threads(int num_threads) { num_threads_ = num_threads; }

    // Import from old checkpoint format
    bool import_checkpoint(const std::string& checkpoint_file);

private:
    // Phase 1: Enumerate all reachable states via BFS (parallel version)
    void enumerate_states();
    void enumerate_states_parallel();
    void enumeration_worker(int thread_id);

    // Phase 2: Build predecessor graph (stored in separate column family)
    void build_predecessors();
    void build_predecessors_parallel();
    void build_predecessors_streaming();  // New optimized version

    // Helper: Load packed_to_id mapping into memory for fast lookups
    void load_packed_to_id_cache();

    // Phase 3: Mark terminal states
    void mark_terminals();
    void mark_terminals_parallel();

    // Phase 4: Retrograde propagation
    void propagate();

    // Helper: Get or create state ID for a canonical packed state
    uint32_t get_or_create_state(uint64_t packed);

    // Helper: Get state ID (returns -1 if not found)
    int64_t get_state_id(uint64_t packed) const;

    // Helper: Get state info by ID
    bool get_state_info(uint32_t id, StateInfoCompact& info) const;

    // Helper: Update state info
    bool put_state_info(uint32_t id, const StateInfoCompact& info);

    // Helper: Add predecessor relationship
    void add_predecessor(uint32_t state_id, uint32_t pred_id);

    // Helper: Get predecessors for a state
    std::vector<uint32_t> get_predecessors(uint32_t state_id) const;

    // Save/load metadata
    void save_metadata();
    void load_metadata();

    // RocksDB instance
    std::unique_ptr<rocksdb::DB> db_;
    rocksdb::ColumnFamilyHandle* cf_states_ = nullptr;      // state_id -> StateInfoCompact
    rocksdb::ColumnFamilyHandle* cf_packed_to_id_ = nullptr; // packed -> state_id
    rocksdb::ColumnFamilyHandle* cf_predecessors_ = nullptr; // state_id -> list of pred_ids
    rocksdb::ColumnFamilyHandle* cf_queue_ = nullptr;        // BFS queue on disk
    rocksdb::ColumnFamilyHandle* cf_metadata_ = nullptr;     // solver metadata

    // Statistics
    uint64_t num_states_ = 0;
    uint64_t num_wins_ = 0;
    uint64_t num_losses_ = 0;
    uint64_t num_draws_ = 0;

    // Starting state ID
    uint32_t start_id_ = 0;

    // Current solve phase
    SolvePhaseDB phase_ = SolvePhaseDB::NOT_STARTED;

    // Checkpoint settings
    uint64_t checkpoint_interval_ = 1000000;

    // For resumable enumeration
    uint64_t enum_processed_ = 0;
    uint64_t queue_head_ = 0;
    uint64_t queue_tail_ = 0;

    ProgressCallback progress_cb_;

    // Write options (fast vs durable)
    rocksdb::WriteOptions fast_write_options_;
    rocksdb::WriteOptions metadata_write_options_;

    // Parallelization settings
    int num_threads_ = 1;

    // Thread synchronization for parallel enumeration
    std::mutex db_mutex_;                    // Protects database writes
    std::mutex queue_mutex_;                 // Protects in-memory queue
    std::atomic<uint64_t> atomic_enum_processed_{0};
    std::atomic<uint64_t> atomic_num_states_{0};
    std::atomic<bool> stop_workers_{false};

    // In-memory work queue for parallel processing
    std::vector<uint32_t> work_queue_;
    std::atomic<size_t> work_queue_head_{0};
    std::condition_variable work_cv_;

    // Thread-safe state creation with concurrent hash map
    std::mutex state_create_mutex_;

    // In-memory cache of packed_to_id for fast lookups during predecessor building
    // Using sorted vector + binary search instead of unordered_map to save memory
    // unordered_map: ~40 bytes/entry = 17GB for 430M entries
    // sorted vector: 12 bytes/entry = 5GB for 430M entries
    struct PackedIdPair {
        uint64_t packed;
        uint32_t id;
        bool operator<(const PackedIdPair& other) const { return packed < other.packed; }
    };
    std::vector<PackedIdPair> packed_to_id_cache_;
    bool cache_loaded_ = false;

    // Bloom filter for fast duplicate rejection during enumeration
    std::unique_ptr<BloomFilter> bloom_filter_;
    bool bloom_loaded_ = false;

    // Load bloom filter from existing states
    void load_bloom_filter();

    // Batch lookup helper using MultiGet
    std::vector<int64_t> batch_get_state_ids(const std::vector<uint64_t>& packed_states) const;
};

} // namespace bobail
