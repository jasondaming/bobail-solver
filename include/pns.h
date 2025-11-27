#pragma once

#include "board.h"
#include "movegen.h"
#include "tt.h"
#include <vector>
#include <memory>
#include <functional>

namespace bobail {

// Node in the proof-number search tree
struct PNSNode {
    State state;
    uint32_t proof;         // Proof number
    uint32_t disproof;      // Disproof number
    bool expanded;          // Has children been generated?
    std::vector<std::unique_ptr<PNSNode>> children;
    Move move;              // Move that led to this node (for PV extraction)

    PNSNode(const State& s)
        : state(s), proof(1), disproof(1), expanded(false) {}
};

// Proof-Number Search solver
class PNSSolver {
public:
    // Callback for progress reporting
    using ProgressCallback = std::function<void(uint64_t nodes, uint64_t proved, uint64_t disproved)>;

    explicit PNSSolver(size_t tt_size = 1 << 24);  // Default 16M entries

    // Solve from a given position
    // Returns WIN if starting player wins, LOSS if they lose, DRAW otherwise
    Result solve(const State& root_state);

    // Get the principal variation (best play for both sides)
    std::vector<Move> get_pv() const;

    // Statistics
    uint64_t nodes_searched() const { return nodes_searched_; }
    uint64_t nodes_proved() const { return nodes_proved_; }
    uint64_t nodes_disproved() const { return nodes_disproved_; }

    // Set progress callback (called periodically during search)
    void set_progress_callback(ProgressCallback cb) { progress_cb_ = cb; }

    // Set node limit (0 = unlimited)
    void set_node_limit(uint64_t limit) { node_limit_ = limit; }

private:
    TranspositionTable tt_;
    std::unique_ptr<PNSNode> root_;

    uint64_t nodes_searched_ = 0;
    uint64_t nodes_proved_ = 0;
    uint64_t nodes_disproved_ = 0;
    uint64_t node_limit_ = 0;

    ProgressCallback progress_cb_;

    // Core PNS algorithm
    void pns_search(PNSNode* node);

    // Select most proving node (for OR nodes, min proof; for AND nodes, min disproof)
    PNSNode* select_most_proving(PNSNode* node, bool is_or_node);

    // Expand a node (generate children)
    void expand(PNSNode* node);

    // Update proof/disproof numbers after expansion or child update
    void update_ancestors(PNSNode* node, bool is_or_node);

    // Set proof numbers based on terminal state
    void set_terminal(PNSNode* node);

    // Check if node is terminal (win/loss/no moves)
    bool is_terminal(const State& s);

    // Get canonical hash for TT lookup
    uint64_t get_hash(const State& s);

    // Update a single node's proof numbers from children
    void update_node(PNSNode* node);

    // Update entire tree (post-order traversal)
    void update_tree(PNSNode* node);
};

} // namespace bobail
