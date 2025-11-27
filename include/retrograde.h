#pragma once

#include "board.h"
#include "movegen.h"
#include "tt.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace bobail {

// State info for retrograde analysis
struct StateInfo {
    uint64_t packed;            // Canonical packed state
    Result result;              // WIN/LOSS/DRAW/UNKNOWN
    uint16_t num_successors;    // Number of legal moves
    uint16_t winning_succs;     // Count of successors that are losses (for opponent)
    std::vector<uint32_t> predecessors;  // State IDs that can reach this state
};

// Retrograde solver for strong solution
class RetrogradeSolver {
public:
    using ProgressCallback = std::function<void(const char* phase, uint64_t current, uint64_t total)>;

    RetrogradeSolver();

    // Run the full solve process
    // Returns true if successful
    bool solve();

    // Get result for a specific state (after solving)
    Result get_result(const State& s) const;

    // Get optimal move from a position (after solving)
    // Returns empty move if no legal moves or not solved
    Move get_best_move(const State& s) const;

    // Statistics
    uint64_t num_states() const { return states_.size(); }
    uint64_t num_wins() const { return num_wins_; }
    uint64_t num_losses() const { return num_losses_; }
    uint64_t num_draws() const { return num_draws_; }

    // Set progress callback
    void set_progress_callback(ProgressCallback cb) { progress_cb_ = cb; }

    // Get the result for the starting position
    Result starting_result() const;

private:
    // Phase 1: Enumerate all reachable states via BFS
    void enumerate_states();

    // Phase 2: Build predecessor graph
    void build_predecessors();

    // Phase 3: Mark terminal states
    void mark_terminals();

    // Phase 4: Retrograde propagation
    void propagate();

    // Helper: Get or create state ID for a canonical packed state
    uint32_t get_or_create_state(uint64_t packed);

    // Helper: Get state ID (returns -1 if not found)
    int64_t get_state_id(uint64_t packed) const;

    // All enumerated states
    std::vector<StateInfo> states_;

    // Map from packed state to state ID
    std::unordered_map<uint64_t, uint32_t> state_to_id_;

    // Statistics
    uint64_t num_wins_ = 0;
    uint64_t num_losses_ = 0;
    uint64_t num_draws_ = 0;

    // Starting state ID
    uint32_t start_id_ = 0;

    ProgressCallback progress_cb_;
};

} // namespace bobail
