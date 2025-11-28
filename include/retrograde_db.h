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
#include <rocksdb/db.h>
#include <rocksdb/options.h>

namespace bobail {

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
};

} // namespace bobail
