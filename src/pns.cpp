#include "pns.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <algorithm>
#include <limits>

namespace bobail {

PNSSolver::PNSSolver(size_t tt_size) : tt_(tt_size) {}

Result PNSSolver::solve(const State& root_state) {
    nodes_searched_ = 0;
    nodes_proved_ = 0;
    nodes_disproved_ = 0;
    tt_.clear();

    root_ = std::make_unique<PNSNode>(root_state);

    // Check if root is already terminal
    if (is_terminal(root_state)) {
        set_terminal(root_.get());
        if (root_->proof == 0) return Result::WIN;
        if (root_->disproof == 0) return Result::LOSS;
    }

    // Main PNS loop
    while (root_->proof != 0 && root_->disproof != 0) {
        if (node_limit_ > 0 && nodes_searched_ >= node_limit_) {
            break;  // Hit node limit
        }

        pns_search(root_.get());

        // Progress callback
        if (progress_cb_ && (nodes_searched_ % 100000 == 0)) {
            progress_cb_(nodes_searched_, nodes_proved_, nodes_disproved_);
        }
    }

    if (root_->proof == 0) {
        return Result::WIN;
    } else if (root_->disproof == 0) {
        return Result::LOSS;
    }
    return Result::UNKNOWN;  // Hit limit before solving
}

void PNSSolver::pns_search(PNSNode* node) {
    // Collect path from root to most proving node
    std::vector<PNSNode*> path;
    path.push_back(node);

    PNSNode* current = node;
    bool is_or = (current->state.white_to_move == root_->state.white_to_move);

    while (current->expanded && !current->children.empty()) {
        // Select best child
        PNSNode* best = nullptr;
        uint32_t best_value = PN_INFINITY;

        for (auto& child : current->children) {
            uint32_t value = is_or ? child->proof : child->disproof;
            if (value < best_value) {
                best_value = value;
                best = child.get();
            }
        }

        if (best == nullptr || best_value == 0) {
            break;  // Solved or no valid child
        }

        path.push_back(best);
        current = best;
        is_or = !is_or;
    }

    // Expand the leaf node
    if (!current->expanded) {
        expand(current);
    }

    // Update only nodes on the path (from leaf to root)
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        update_node(*it);
    }
}

PNSNode* PNSSolver::select_most_proving(PNSNode* node, bool is_or_node) {
    if (!node->expanded) {
        return node;  // This node needs expansion
    }

    if (node->children.empty()) {
        return nullptr;  // Terminal node
    }

    // Select child based on node type
    PNSNode* best = nullptr;
    uint32_t best_value = PN_INFINITY;

    for (auto& child : node->children) {
        uint32_t value = is_or_node ? child->proof : child->disproof;

        if (value < best_value) {
            best_value = value;
            best = child.get();
        }
    }

    if (best == nullptr || best_value == 0) {
        return nullptr;  // Already solved
    }

    // Recurse
    return select_most_proving(best, !is_or_node);
}

void PNSSolver::expand(PNSNode* node) {
    if (node->expanded) return;

    node->expanded = true;
    ++nodes_searched_;

    // Check TT first
    uint64_t hash = get_hash(node->state);
    TTEntry* tt_entry = tt_.probe(hash);
    if (tt_entry && tt_entry->is_solved()) {
        if (tt_entry->result == Result::WIN) {
            node->proof = 0;
            node->disproof = PN_INFINITY;
            ++nodes_proved_;
        } else if (tt_entry->result == Result::LOSS) {
            node->proof = PN_INFINITY;
            node->disproof = 0;
            ++nodes_disproved_;
        }
        return;
    }

    // Check terminal
    if (is_terminal(node->state)) {
        set_terminal(node);
        // Store in TT
        TTEntry entry;
        entry.key = hash;
        entry.proof = node->proof;
        entry.disproof = node->disproof;
        if (node->proof == 0) entry.result = Result::WIN;
        else if (node->disproof == 0) entry.result = Result::LOSS;
        tt_.store(hash, entry);
        return;
    }

    // Generate moves and create children
    auto moves = generate_moves(node->state);

    if (moves.empty()) {
        // No legal moves = loss for side to move
        node->proof = PN_INFINITY;
        node->disproof = 0;
        ++nodes_disproved_;

        TTEntry entry;
        entry.key = hash;
        entry.proof = PN_INFINITY;
        entry.disproof = 0;
        entry.result = Result::LOSS;
        tt_.store(hash, entry);
        return;
    }

    for (const auto& move : moves) {
        State child_state = apply_move(node->state, move);
        auto child = std::make_unique<PNSNode>(child_state);
        child->move = move;

        // Check if child is terminal or in TT
        uint64_t child_hash = get_hash(child_state);
        TTEntry* child_tt = tt_.probe(child_hash);
        if (child_tt && child_tt->is_solved()) {
            child->proof = child_tt->proof;
            child->disproof = child_tt->disproof;
            child->expanded = true;
        } else if (is_terminal(child_state)) {
            set_terminal(child.get());
            child->expanded = true;
        }

        node->children.push_back(std::move(child));
    }

    // Update this node's proof numbers based on children
    update_node(node);
}

void PNSSolver::update_node(PNSNode* node) {
    if (node->children.empty()) return;

    // Determine node type
    bool is_or_node = (node->state.white_to_move == root_->state.white_to_move);

    if (is_or_node) {
        // OR node: proof = min(children.proof), disproof = sum(children.disproof)
        uint32_t min_proof = PN_INFINITY;
        uint64_t sum_disproof = 0;

        for (auto& child : node->children) {
            min_proof = std::min(min_proof, child->proof);
            sum_disproof += child->disproof;
            if (sum_disproof > PN_INFINITY) sum_disproof = PN_INFINITY;
        }

        node->proof = min_proof;
        node->disproof = static_cast<uint32_t>(std::min(sum_disproof, static_cast<uint64_t>(PN_INFINITY)));
    } else {
        // AND node: proof = sum(children.proof), disproof = min(children.disproof)
        uint64_t sum_proof = 0;
        uint32_t min_disproof = PN_INFINITY;

        for (auto& child : node->children) {
            sum_proof += child->proof;
            if (sum_proof > PN_INFINITY) sum_proof = PN_INFINITY;
            min_disproof = std::min(min_disproof, child->disproof);
        }

        node->proof = static_cast<uint32_t>(std::min(sum_proof, static_cast<uint64_t>(PN_INFINITY)));
        node->disproof = min_disproof;
    }

    // Track solved nodes
    if (node->proof == 0) {
        ++nodes_proved_;
        uint64_t hash = get_hash(node->state);
        TTEntry entry;
        entry.key = hash;
        entry.proof = 0;
        entry.disproof = PN_INFINITY;
        entry.result = Result::WIN;
        tt_.store(hash, entry);
    } else if (node->disproof == 0) {
        ++nodes_disproved_;
        uint64_t hash = get_hash(node->state);
        TTEntry entry;
        entry.key = hash;
        entry.proof = PN_INFINITY;
        entry.disproof = 0;
        entry.result = Result::LOSS;
        tt_.store(hash, entry);
    }
}

void PNSSolver::update_tree(PNSNode* node) {
    if (!node->expanded || node->children.empty()) return;

    // Update children first (post-order)
    for (auto& child : node->children) {
        update_tree(child.get());
    }

    // Then update this node
    update_node(node);
}

void PNSSolver::set_terminal(PNSNode* node) {
    GameResult result = check_terminal(node->state);

    if (result == GameResult::WHITE_WINS) {
        if (node->state.white_to_move) {
            // Current player (white) just won - but this shouldn't happen
            // as terminal is checked after opponent's move
            // Actually in Bobail, you win when Bobail reaches YOUR home row
            // So if it's white's turn and Bobail is on row 0, white already won
            // This means the PREVIOUS player (black) moved Bobail to white's home row
            // which would be a loss for black... Let me reconsider.

            // After a move, we check terminal. The state has already switched sides.
            // If white_to_move and Bobail on row 0, that means black just moved
            // and Bobail ended on white's home row. White wins!
            node->proof = 0;
            node->disproof = PN_INFINITY;
            ++nodes_proved_;
        } else {
            // It's black's turn, Bobail on row 0 = white wins
            // From black's perspective, this is a loss
            node->proof = PN_INFINITY;
            node->disproof = 0;
            ++nodes_disproved_;
        }
    } else if (result == GameResult::BLACK_WINS) {
        if (!node->state.white_to_move) {
            // Black's turn, Bobail on row 4 = black wins = win for current player
            node->proof = 0;
            node->disproof = PN_INFINITY;
            ++nodes_proved_;
        } else {
            // White's turn, Bobail on row 4 = black wins = loss for current player
            node->proof = PN_INFINITY;
            node->disproof = 0;
            ++nodes_disproved_;
        }
    }
}

bool PNSSolver::is_terminal(const State& s) {
    return check_terminal(s) != GameResult::ONGOING;
}

uint64_t PNSSolver::get_hash(const State& s) {
    return canonical_hash(s);
}

std::vector<Move> PNSSolver::get_pv() const {
    std::vector<Move> pv;
    if (!root_) return pv;

    PNSNode* node = root_.get();
    while (node && node->expanded && !node->children.empty()) {
        // Find the best child (proved if this is proved, or best proof number)
        PNSNode* best = nullptr;
        bool is_or = (node->state.white_to_move == root_->state.white_to_move);

        for (auto& child : node->children) {
            if (best == nullptr) {
                best = child.get();
            } else if (is_or) {
                // OR node: want min proof
                if (child->proof < best->proof) {
                    best = child.get();
                }
            } else {
                // AND node: want min disproof
                if (child->disproof < best->disproof) {
                    best = child.get();
                }
            }
        }

        if (best) {
            pv.push_back(best->move);
            node = best;
        } else {
            break;
        }
    }

    return pv;
}

} // namespace bobail
