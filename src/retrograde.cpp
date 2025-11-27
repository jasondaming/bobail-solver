#include "retrograde.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <queue>
#include <iostream>
#include <fstream>
#include <cstring>

namespace bobail {

// Checkpoint file format version
static const uint32_t CHECKPOINT_VERSION = 1;
static const char CHECKPOINT_MAGIC[] = "BBCK";

RetrogradeSolver::RetrogradeSolver() {}

bool RetrogradeSolver::solve() {
    // Resume from current phase or start fresh
    if (phase_ == SolvePhase::NOT_STARTED || phase_ == SolvePhase::ENUMERATING) {
        // Phase 1: Enumerate all reachable states
        if (progress_cb_) progress_cb_("Enumerating states", 0, 0);
        phase_ = SolvePhase::ENUMERATING;
        enumerate_states();
        phase_ = SolvePhase::BUILDING_PREDECESSORS;
        if (!checkpoint_file_.empty()) save_checkpoint(checkpoint_file_);
    }

    if (phase_ == SolvePhase::BUILDING_PREDECESSORS) {
        // Phase 2: Build predecessor graph
        if (progress_cb_) progress_cb_("Building predecessors", 0, states_.size());
        build_predecessors();
        phase_ = SolvePhase::MARKING_TERMINALS;
        if (!checkpoint_file_.empty()) save_checkpoint(checkpoint_file_);
    }

    if (phase_ == SolvePhase::MARKING_TERMINALS) {
        // Phase 3: Mark terminal states
        if (progress_cb_) progress_cb_("Marking terminals", 0, states_.size());
        mark_terminals();
        phase_ = SolvePhase::PROPAGATING;
        if (!checkpoint_file_.empty()) save_checkpoint(checkpoint_file_);
    }

    if (phase_ == SolvePhase::PROPAGATING) {
        // Phase 4: Retrograde propagation
        if (progress_cb_) progress_cb_("Propagating", 0, states_.size());
        propagate();
        phase_ = SolvePhase::COMPLETE;
        if (!checkpoint_file_.empty()) save_checkpoint(checkpoint_file_);
    }

    return true;
}

void RetrogradeSolver::enumerate_states() {
    std::queue<uint32_t> queue;

    // Check if resuming from checkpoint
    if (!enum_queue_.empty()) {
        // Restore queue from checkpoint
        for (uint32_t id : enum_queue_) {
            queue.push(id);
        }
        enum_queue_.clear();
        if (progress_cb_) {
            progress_cb_("Resuming enumeration", enum_processed_, states_.size());
        }
    } else {
        // Fresh start - BFS from starting position
        State start = State::starting_position();
        auto [canonical_start, _] = canonicalize(start);
        uint64_t start_packed = pack_state(canonical_start);

        start_id_ = get_or_create_state(start_packed);
        queue.push(start_id_);
        enum_processed_ = 0;
    }

    uint64_t last_checkpoint = enum_processed_;

    while (!queue.empty()) {
        uint32_t id = queue.front();
        queue.pop();

        State s = unpack_state(states_[id].packed);

        // Check if terminal (skip move generation for terminals)
        GameResult gr = check_terminal(s);
        if (gr != GameResult::ONGOING) {
            states_[id].num_successors = 0;
            ++enum_processed_;
            continue;
        }

        // Generate all moves
        auto moves = generate_moves(s);
        states_[id].num_successors = moves.size();

        if (moves.empty()) {
            // No moves = loss for current player (this is a terminal too)
            ++enum_processed_;
            continue;
        }

        // Process each successor
        for (const auto& move : moves) {
            State ns = apply_move(s, move);
            auto [canonical_ns, __] = canonicalize(ns);
            uint64_t ns_packed = pack_state(canonical_ns);

            auto it = state_to_id_.find(ns_packed);
            if (it == state_to_id_.end()) {
                // New state - add to queue
                uint32_t new_id = get_or_create_state(ns_packed);
                queue.push(new_id);
            }
        }

        ++enum_processed_;
        if (progress_cb_ && enum_processed_ % 100000 == 0) {
            progress_cb_("Enumerating states", enum_processed_, states_.size());
        }

        // Auto-checkpoint during enumeration
        if (!checkpoint_file_.empty() &&
            checkpoint_interval_ > 0 &&
            enum_processed_ - last_checkpoint >= checkpoint_interval_) {
            // Save queue to enum_queue_ for checkpoint
            std::vector<uint32_t> queue_copy;
            std::queue<uint32_t> temp = queue;
            while (!temp.empty()) {
                queue_copy.push_back(temp.front());
                temp.pop();
            }
            enum_queue_ = std::move(queue_copy);
            save_checkpoint(checkpoint_file_);
            enum_queue_.clear();
            last_checkpoint = enum_processed_;
        }
    }

    if (progress_cb_) {
        progress_cb_("Enumeration complete", states_.size(), states_.size());
    }
}

void RetrogradeSolver::build_predecessors() {
    // For each state, find its successors and add self as predecessor to them
    for (uint32_t id = 0; id < states_.size(); ++id) {
        State s = unpack_state(states_[id].packed);

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
                states_[succ_id].predecessors.push_back(id);
            }
        }

        if (progress_cb_ && id % 100000 == 0) {
            progress_cb_("Building predecessors", id, states_.size());
        }
    }

    if (progress_cb_) {
        progress_cb_("Predecessors complete", states_.size(), states_.size());
    }
}

void RetrogradeSolver::mark_terminals() {
    // Mark terminal states and initialize queue for propagation
    for (uint32_t id = 0; id < states_.size(); ++id) {
        State s = unpack_state(states_[id].packed);
        GameResult gr = check_terminal(s);

        if (gr == GameResult::WHITE_WINS) {
            // If white just won, it's current player's perspective
            // After a move, side switches. So if Bobail is on white's home row:
            // - It's now the next player's turn
            // - White wins means the PREVIOUS player (who just moved) won
            // - From current player's view: they face a won position = LOSS for them
            if (s.white_to_move) {
                // White to move, Bobail on row 0 = White wins = WIN for current player
                states_[id].result = Result::WIN;
                ++num_wins_;
            } else {
                // Black to move, Bobail on row 0 = White wins = LOSS for current player
                states_[id].result = Result::LOSS;
                ++num_losses_;
            }
        } else if (gr == GameResult::BLACK_WINS) {
            if (!s.white_to_move) {
                // Black to move, Bobail on row 4 = Black wins = WIN for current player
                states_[id].result = Result::WIN;
                ++num_wins_;
            } else {
                // White to move, Bobail on row 4 = Black wins = LOSS for current player
                states_[id].result = Result::LOSS;
                ++num_losses_;
            }
        } else if (states_[id].num_successors == 0) {
            // No legal moves = loss for current player
            auto moves = generate_moves(s);
            if (moves.empty()) {
                states_[id].result = Result::LOSS;
                ++num_losses_;
            }
        }

        if (progress_cb_ && id % 100000 == 0) {
            progress_cb_("Marking terminals", id, states_.size());
        }
    }

    if (progress_cb_) {
        progress_cb_("Terminals marked", states_.size(), states_.size());
    }
}

void RetrogradeSolver::propagate() {
    // Initialize queue with all solved states
    std::queue<uint32_t> queue;
    for (uint32_t id = 0; id < states_.size(); ++id) {
        if (states_[id].result != Result::UNKNOWN) {
            queue.push(id);
        }
    }

    uint64_t propagated = 0;

    while (!queue.empty()) {
        uint32_t id = queue.front();
        queue.pop();

        Result child_result = states_[id].result;

        // Update all predecessors
        for (uint32_t pred_id : states_[id].predecessors) {
            StateInfo& pred = states_[pred_id];

            // Skip if already solved
            if (pred.result != Result::UNKNOWN) {
                continue;
            }

            if (child_result == Result::LOSS) {
                // Child is LOSS for child = WIN for us (we can move there)
                pred.result = Result::WIN;
                ++num_wins_;
                queue.push(pred_id);
            } else if (child_result == Result::WIN) {
                // Child is WIN for child = we need to check all our moves
                ++pred.winning_succs;

                // If ALL successors are wins for opponent, we lose
                if (pred.winning_succs >= pred.num_successors) {
                    pred.result = Result::LOSS;
                    ++num_losses_;
                    queue.push(pred_id);
                }
            }
            // DRAW children don't immediately resolve parent
        }

        ++propagated;
        if (progress_cb_ && propagated % 100000 == 0) {
            progress_cb_("Propagating", propagated, states_.size());
        }
    }

    // Any remaining UNKNOWN states are draws (cycles)
    for (uint32_t id = 0; id < states_.size(); ++id) {
        if (states_[id].result == Result::UNKNOWN) {
            states_[id].result = Result::DRAW;
            ++num_draws_;
        }
    }

    if (progress_cb_) {
        progress_cb_("Propagation complete", states_.size(), states_.size());
    }
}

uint32_t RetrogradeSolver::get_or_create_state(uint64_t packed) {
    auto it = state_to_id_.find(packed);
    if (it != state_to_id_.end()) {
        return it->second;
    }

    uint32_t id = states_.size();
    states_.push_back(StateInfo{packed, Result::UNKNOWN, 0, 0, {}});
    state_to_id_[packed] = id;
    return id;
}

int64_t RetrogradeSolver::get_state_id(uint64_t packed) const {
    auto it = state_to_id_.find(packed);
    if (it != state_to_id_.end()) {
        return it->second;
    }
    return -1;
}

Result RetrogradeSolver::get_result(const State& s) const {
    auto [canonical, _] = canonicalize(s);
    uint64_t packed = pack_state(canonical);
    int64_t id = get_state_id(packed);
    if (id >= 0) {
        return states_[id].result;
    }
    return Result::UNKNOWN;
}

Move RetrogradeSolver::get_best_move(const State& s) const {
    Result my_result = get_result(s);

    auto moves = generate_moves(s);
    if (moves.empty()) {
        return Move{};
    }

    // Find a move that leads to the best outcome
    for (const auto& move : moves) {
        State ns = apply_move(s, move);
        Result opp_result = get_result(ns);

        // If we're winning, find a move to a losing position for opponent
        if (my_result == Result::WIN && opp_result == Result::LOSS) {
            return move;
        }
        // If we're drawing, find a move to a draw for opponent
        if (my_result == Result::DRAW && opp_result == Result::DRAW) {
            return move;
        }
        // If we're losing, any move is equally bad, but prefer draws if possible
        if (my_result == Result::LOSS) {
            if (opp_result == Result::DRAW) return move;
            if (opp_result == Result::WIN) return move;  // All losing anyway
        }
    }

    // Fallback: return first move
    return moves[0];
}

Result RetrogradeSolver::starting_result() const {
    if (start_id_ < states_.size()) {
        return states_[start_id_].result;
    }
    return Result::UNKNOWN;
}

bool RetrogradeSolver::save_checkpoint(const std::string& filename) const {
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open checkpoint file for writing: " << filename << "\n";
        return false;
    }

    // Write header
    out.write(CHECKPOINT_MAGIC, 4);
    out.write(reinterpret_cast<const char*>(&CHECKPOINT_VERSION), sizeof(CHECKPOINT_VERSION));

    // Write phase
    uint32_t phase_val = static_cast<uint32_t>(phase_);
    out.write(reinterpret_cast<const char*>(&phase_val), sizeof(phase_val));

    // Write statistics
    out.write(reinterpret_cast<const char*>(&num_wins_), sizeof(num_wins_));
    out.write(reinterpret_cast<const char*>(&num_losses_), sizeof(num_losses_));
    out.write(reinterpret_cast<const char*>(&num_draws_), sizeof(num_draws_));
    out.write(reinterpret_cast<const char*>(&start_id_), sizeof(start_id_));
    out.write(reinterpret_cast<const char*>(&enum_processed_), sizeof(enum_processed_));

    // Write number of states
    uint64_t num_states = states_.size();
    out.write(reinterpret_cast<const char*>(&num_states), sizeof(num_states));

    // Write each state (packed + result + num_successors + winning_succs)
    // We don't save predecessors - they will be rebuilt
    for (const auto& state : states_) {
        out.write(reinterpret_cast<const char*>(&state.packed), sizeof(state.packed));
        uint8_t result_val = static_cast<uint8_t>(state.result);
        out.write(reinterpret_cast<const char*>(&result_val), sizeof(result_val));
        out.write(reinterpret_cast<const char*>(&state.num_successors), sizeof(state.num_successors));
        out.write(reinterpret_cast<const char*>(&state.winning_succs), sizeof(state.winning_succs));
    }

    // Write enum queue for resumable enumeration
    uint64_t queue_size = enum_queue_.size();
    out.write(reinterpret_cast<const char*>(&queue_size), sizeof(queue_size));
    for (uint32_t id : enum_queue_) {
        out.write(reinterpret_cast<const char*>(&id), sizeof(id));
    }

    out.close();
    std::cerr << "Checkpoint saved: " << states_.size() << " states, phase " << phase_val << "\n";
    return true;
}

bool RetrogradeSolver::load_checkpoint(const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open checkpoint file for reading: " << filename << "\n";
        return false;
    }

    // Read and verify header
    char magic[4];
    in.read(magic, 4);
    if (std::memcmp(magic, CHECKPOINT_MAGIC, 4) != 0) {
        std::cerr << "Invalid checkpoint file (bad magic)\n";
        return false;
    }

    uint32_t version;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != CHECKPOINT_VERSION) {
        std::cerr << "Unsupported checkpoint version: " << version << "\n";
        return false;
    }

    // Read phase
    uint32_t phase_val;
    in.read(reinterpret_cast<char*>(&phase_val), sizeof(phase_val));
    phase_ = static_cast<SolvePhase>(phase_val);

    // Read statistics
    in.read(reinterpret_cast<char*>(&num_wins_), sizeof(num_wins_));
    in.read(reinterpret_cast<char*>(&num_losses_), sizeof(num_losses_));
    in.read(reinterpret_cast<char*>(&num_draws_), sizeof(num_draws_));
    in.read(reinterpret_cast<char*>(&start_id_), sizeof(start_id_));
    in.read(reinterpret_cast<char*>(&enum_processed_), sizeof(enum_processed_));

    // Read number of states
    uint64_t num_states;
    in.read(reinterpret_cast<char*>(&num_states), sizeof(num_states));

    // Read states
    states_.clear();
    states_.reserve(num_states);
    state_to_id_.clear();
    state_to_id_.reserve(num_states);

    for (uint64_t i = 0; i < num_states; ++i) {
        StateInfo state;
        in.read(reinterpret_cast<char*>(&state.packed), sizeof(state.packed));
        uint8_t result_val;
        in.read(reinterpret_cast<char*>(&result_val), sizeof(result_val));
        state.result = static_cast<Result>(result_val);
        in.read(reinterpret_cast<char*>(&state.num_successors), sizeof(state.num_successors));
        in.read(reinterpret_cast<char*>(&state.winning_succs), sizeof(state.winning_succs));

        state_to_id_[state.packed] = states_.size();
        states_.push_back(state);
    }

    // Read enum queue
    uint64_t queue_size;
    in.read(reinterpret_cast<char*>(&queue_size), sizeof(queue_size));
    enum_queue_.clear();
    enum_queue_.reserve(queue_size);
    for (uint64_t i = 0; i < queue_size; ++i) {
        uint32_t id;
        in.read(reinterpret_cast<char*>(&id), sizeof(id));
        enum_queue_.push_back(id);
    }

    in.close();
    std::cerr << "Checkpoint loaded: " << states_.size() << " states, phase " << phase_val
              << ", queue size " << enum_queue_.size() << "\n";
    return true;
}

} // namespace bobail
