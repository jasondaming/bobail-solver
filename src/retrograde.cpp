#include "retrograde.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <queue>
#include <iostream>

namespace bobail {

RetrogradeSolver::RetrogradeSolver() {}

bool RetrogradeSolver::solve() {
    // Phase 1: Enumerate all reachable states
    if (progress_cb_) progress_cb_("Enumerating states", 0, 0);
    enumerate_states();

    // Phase 2: Build predecessor graph
    if (progress_cb_) progress_cb_("Building predecessors", 0, states_.size());
    build_predecessors();

    // Phase 3: Mark terminal states
    if (progress_cb_) progress_cb_("Marking terminals", 0, states_.size());
    mark_terminals();

    // Phase 4: Retrograde propagation
    if (progress_cb_) progress_cb_("Propagating", 0, states_.size());
    propagate();

    return true;
}

void RetrogradeSolver::enumerate_states() {
    // BFS from starting position
    State start = State::starting_position();
    auto [canonical_start, _] = canonicalize(start);
    uint64_t start_packed = pack_state(canonical_start);

    start_id_ = get_or_create_state(start_packed);

    std::queue<uint32_t> queue;
    queue.push(start_id_);

    uint64_t processed = 0;

    while (!queue.empty()) {
        uint32_t id = queue.front();
        queue.pop();

        State s = unpack_state(states_[id].packed);

        // Check if terminal (skip move generation for terminals)
        GameResult gr = check_terminal(s);
        if (gr != GameResult::ONGOING) {
            states_[id].num_successors = 0;
            continue;
        }

        // Generate all moves
        auto moves = generate_moves(s);
        states_[id].num_successors = moves.size();

        if (moves.empty()) {
            // No moves = loss for current player (this is a terminal too)
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

        ++processed;
        if (progress_cb_ && processed % 100000 == 0) {
            progress_cb_("Enumerating states", processed, states_.size());
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

} // namespace bobail
