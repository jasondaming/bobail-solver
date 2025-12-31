#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include "retrograde_db.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <csignal>
#include <atomic>
#include <unordered_map>

// Enhanced PNS solver with:
// 1. Checkpoint support (saves TT to disk periodically)
// 2. Retrograde DB integration (uses already-solved positions)

namespace bobail {

// PN_INFINITY is already defined in tt.h (included via retrograde_db.h)

// Simplified TT entry for disk storage
struct PNSTTEntry {
    uint64_t hash;
    uint32_t proof;
    uint32_t disproof;
    uint8_t result;  // 0=unknown, 1=win, 2=loss, 3=draw
};

class EnhancedPNSSolver {
public:
    EnhancedPNSSolver(size_t tt_size = 1 << 24) : tt_size_(tt_size) {
        tt_.reserve(tt_size);
    }

    void set_retrograde_db(RetrogradeSolverDB* db) { retro_db_ = db; }
    void set_checkpoint_path(const std::string& path) { checkpoint_path_ = path; }
    void set_checkpoint_interval(uint64_t interval) { checkpoint_interval_ = interval; }

    bool load_checkpoint() {
        if (checkpoint_path_.empty()) return false;

        std::ifstream in(checkpoint_path_, std::ios::binary);
        if (!in) return false;

        // Read header
        uint64_t magic, version, num_entries;
        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (magic != 0x504E5343484B5054ULL) {  // "PNSCHKPT"
            std::cerr << "Invalid checkpoint magic\n";
            return false;
        }
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        in.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
        in.read(reinterpret_cast<char*>(&nodes_searched_), sizeof(nodes_searched_));
        in.read(reinterpret_cast<char*>(&nodes_proved_), sizeof(nodes_proved_));
        in.read(reinterpret_cast<char*>(&nodes_disproved_), sizeof(nodes_disproved_));
        in.read(reinterpret_cast<char*>(&retro_hits_), sizeof(retro_hits_));

        // Read entries
        tt_.clear();
        for (uint64_t i = 0; i < num_entries; ++i) {
            PNSTTEntry entry;
            in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
            tt_[entry.hash] = entry;
        }

        std::cout << "Loaded checkpoint: " << num_entries << " entries, "
                  << nodes_searched_ << " nodes searched\n";
        return true;
    }

    bool save_checkpoint() {
        if (checkpoint_path_.empty()) return false;

        std::string temp_path = checkpoint_path_ + ".tmp";
        std::ofstream out(temp_path, std::ios::binary);
        if (!out) {
            std::cerr << "Failed to open checkpoint file for writing\n";
            return false;
        }

        // Write header
        uint64_t magic = 0x504E5343484B5054ULL;  // "PNSCHKPT"
        uint64_t version = 1;
        uint64_t num_entries = tt_.size();
        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&num_entries), sizeof(num_entries));
        out.write(reinterpret_cast<const char*>(&nodes_searched_), sizeof(nodes_searched_));
        out.write(reinterpret_cast<const char*>(&nodes_proved_), sizeof(nodes_proved_));
        out.write(reinterpret_cast<const char*>(&nodes_disproved_), sizeof(nodes_disproved_));
        out.write(reinterpret_cast<const char*>(&retro_hits_), sizeof(retro_hits_));

        // Write entries
        for (const auto& [hash, entry] : tt_) {
            out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
        }

        out.close();

        // Atomic rename
        std::remove(checkpoint_path_.c_str());
        std::rename(temp_path.c_str(), checkpoint_path_.c_str());

        return true;
    }

    Result solve(const State& root_state, std::atomic<bool>& stop_flag) {
        root_state_ = root_state;
        root_hash_ = canonical_hash(root_state);

        // Check if root is in retrograde DB
        if (retro_db_) {
            Result r = retro_db_->get_result(root_state);
            if (r != Result::UNKNOWN) {
                std::cout << "Root position found in retrograde DB!\n";
                return r;
            }
        }

        // Initialize root if not in TT
        if (tt_.find(root_hash_) == tt_.end()) {
            PNSTTEntry entry;
            entry.hash = root_hash_;
            entry.proof = 1;
            entry.disproof = 1;
            entry.result = 0;
            tt_[root_hash_] = entry;
        }

        auto last_checkpoint = std::chrono::steady_clock::now();
        auto last_progress = std::chrono::steady_clock::now();
        uint64_t last_nodes = nodes_searched_;

        // Main PNS loop
        while (!stop_flag) {
            auto& root_entry = tt_[root_hash_];

            if (root_entry.proof == 0) {
                return Result::WIN;
            }
            if (root_entry.disproof == 0) {
                return Result::LOSS;
            }

            // Do one iteration of PNS
            pns_iteration(root_state_, root_hash_, true);

            // Progress reporting
            auto now = std::chrono::steady_clock::now();
            auto progress_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_progress).count();
            if (progress_elapsed >= 10) {
                uint64_t nodes_delta = nodes_searched_ - last_nodes;
                double rate = nodes_delta / (double)progress_elapsed;

                std::cout << "\rNodes: " << nodes_searched_
                          << " | Proved: " << nodes_proved_
                          << " | Disproved: " << nodes_disproved_
                          << " | RetroDB hits: " << retro_hits_
                          << " | TT size: " << tt_.size()
                          << " | Rate: " << (int)rate << "/s"
                          << " | Root PN: " << root_entry.proof
                          << " DN: " << root_entry.disproof
                          << std::flush;

                last_progress = now;
                last_nodes = nodes_searched_;
            }

            // Checkpoint
            auto checkpoint_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_checkpoint).count();
            if (checkpoint_interval_ > 0 && (uint64_t)checkpoint_elapsed >= checkpoint_interval_) {
                std::cout << "\nSaving checkpoint... " << std::flush;
                if (save_checkpoint()) {
                    std::cout << "done.\n";
                } else {
                    std::cout << "FAILED!\n";
                }
                last_checkpoint = now;
            }
        }

        // Save final checkpoint on stop
        std::cout << "\nStopping, saving checkpoint... " << std::flush;
        save_checkpoint();
        std::cout << "done.\n";

        return Result::UNKNOWN;
    }

    uint64_t nodes_searched() const { return nodes_searched_; }
    uint64_t nodes_proved() const { return nodes_proved_; }
    uint64_t nodes_disproved() const { return nodes_disproved_; }
    uint64_t retro_hits() const { return retro_hits_; }
    uint64_t tt_size() const { return tt_.size(); }

private:
    static constexpr int MAX_DEPTH = 500;  // Prevent stack overflow

    void pns_iteration(const State& state, uint64_t hash, bool is_or_node, int depth = 0) {
        // Depth limit to prevent stack overflow
        if (depth >= MAX_DEPTH) {
            return;  // Stop recursion, will be continued in next iteration
        }

        auto it = tt_.find(hash);
        if (it == tt_.end()) {
            // Expand this node
            expand_node(state, hash, is_or_node);
            return;
        }

        auto& entry = it->second;
        if (entry.proof == 0 || entry.disproof == 0) {
            return;  // Already solved
        }

        // Generate moves and find most proving child
        auto moves = generate_moves(state);
        if (moves.empty()) {
            entry.proof = PN_INFINITY;
            entry.disproof = 0;
            entry.result = 2;  // Loss
            ++nodes_disproved_;
            return;
        }

        // Find best child to recurse into
        State best_child_state;
        uint64_t best_child_hash = 0;
        uint32_t best_value = PN_INFINITY;

        for (const auto& move : moves) {
            State child_state = apply_move(state, move);
            uint64_t child_hash = canonical_hash(child_state);

            auto child_it = tt_.find(child_hash);
            if (child_it == tt_.end()) {
                // Check if unexpanded child is terminal
                GameResult gr = check_terminal(child_state);
                if (gr != GameResult::ONGOING) {
                    // Terminal child - expand it immediately to record result
                    expand_node(child_state, child_hash, !is_or_node);
                    // Update this node with new terminal child
                    update_node(state, hash, is_or_node);
                    // Re-check if we're now solved
                    auto& entry = tt_[hash];
                    if (entry.proof == 0 || entry.disproof == 0) {
                        return;  // Solved by terminal child
                    }
                    continue;  // Check next child
                }
                // Unexpanded non-terminal child - this is our target
                best_child_state = child_state;
                best_child_hash = child_hash;
                break;
            }

            uint32_t value = is_or_node ? child_it->second.proof : child_it->second.disproof;
            if (value < best_value && value > 0) {
                best_value = value;
                best_child_state = child_state;
                best_child_hash = child_hash;
            }
        }

        if (best_child_hash != 0) {
            // Recurse with incremented depth
            pns_iteration(best_child_state, best_child_hash, !is_or_node, depth + 1);
        }

        // Update this node
        update_node(state, hash, is_or_node);
    }

    void expand_node(const State& state, uint64_t hash, bool is_or_node) {
        ++nodes_searched_;

        // Check retrograde DB first
        if (retro_db_) {
            Result r = retro_db_->get_result(state);
            if (r != Result::UNKNOWN) {
                ++retro_hits_;
                PNSTTEntry entry;
                entry.hash = hash;
                if (r == Result::WIN) {
                    entry.proof = 0;
                    entry.disproof = PN_INFINITY;
                    entry.result = 1;
                    ++nodes_proved_;
                } else if (r == Result::LOSS) {
                    entry.proof = PN_INFINITY;
                    entry.disproof = 0;
                    entry.result = 2;
                    ++nodes_disproved_;
                } else {
                    entry.proof = PN_INFINITY;
                    entry.disproof = PN_INFINITY;
                    entry.result = 3;
                }
                tt_[hash] = entry;
                return;
            }
        }

        // Check terminal
        GameResult gr = check_terminal(state);
        if (gr != GameResult::ONGOING) {
            PNSTTEntry entry;
            entry.hash = hash;

            bool current_player_wins =
                (gr == GameResult::WHITE_WINS && state.white_to_move) ||
                (gr == GameResult::BLACK_WINS && !state.white_to_move);
            bool current_player_loses =
                (gr == GameResult::WHITE_WINS && !state.white_to_move) ||
                (gr == GameResult::BLACK_WINS && state.white_to_move);

            if (current_player_wins) {
                entry.proof = 0;
                entry.disproof = PN_INFINITY;
                entry.result = 1;
                ++nodes_proved_;
            } else if (current_player_loses) {
                entry.proof = PN_INFINITY;
                entry.disproof = 0;
                entry.result = 2;
                ++nodes_disproved_;
            }
            tt_[hash] = entry;
            return;
        }

        // Initialize with default proof numbers
        PNSTTEntry entry;
        entry.hash = hash;
        entry.proof = 1;
        entry.disproof = 1;
        entry.result = 0;
        tt_[hash] = entry;

        // Immediately update based on children (if any are in TT)
        update_node(state, hash, is_or_node);
    }

    void update_node(const State& state, uint64_t hash, bool is_or_node) {
        auto it = tt_.find(hash);
        if (it == tt_.end()) return;

        auto moves = generate_moves(state);
        if (moves.empty()) {
            it->second.proof = PN_INFINITY;
            it->second.disproof = 0;
            it->second.result = 2;
            return;
        }

        uint32_t min_proof = PN_INFINITY;
        uint32_t min_disproof = PN_INFINITY;
        uint64_t sum_proof = 0;
        uint64_t sum_disproof = 0;
        int known_children = 0;

        for (const auto& move : moves) {
            State child_state = apply_move(state, move);
            uint64_t child_hash = canonical_hash(child_state);

            uint32_t child_proof = 1;
            uint32_t child_disproof = 1;

            auto child_it = tt_.find(child_hash);
            if (child_it != tt_.end()) {
                ++known_children;
                child_proof = child_it->second.proof;
                child_disproof = child_it->second.disproof;
            } else {
                // Check if child is terminal (even if not in TT)
                GameResult gr = check_terminal(child_state);
                if (gr != GameResult::ONGOING) {
                    bool child_wins = (gr == GameResult::WHITE_WINS && child_state.white_to_move) ||
                                      (gr == GameResult::BLACK_WINS && !child_state.white_to_move);
                    bool child_loses = (gr == GameResult::WHITE_WINS && !child_state.white_to_move) ||
                                       (gr == GameResult::BLACK_WINS && child_state.white_to_move);
                    if (child_wins) {
                        child_proof = 0;
                        child_disproof = PN_INFINITY;
                    } else if (child_loses) {
                        child_proof = PN_INFINITY;
                        child_disproof = 0;
                    }
                }
                // Else: unknown non-terminal child uses default PN=1, DN=1
            }

            min_proof = std::min(min_proof, child_proof);
            min_disproof = std::min(min_disproof, child_disproof);
            sum_proof += child_proof;
            sum_disproof += child_disproof;
        }

        // Clamp sums
        if (sum_proof > PN_INFINITY) sum_proof = PN_INFINITY;
        if (sum_disproof > PN_INFINITY) sum_disproof = PN_INFINITY;

        if (is_or_node) {
            it->second.proof = min_proof;
            it->second.disproof = static_cast<uint32_t>(sum_disproof);
        } else {
            it->second.proof = static_cast<uint32_t>(sum_proof);
            it->second.disproof = min_disproof;
        }

        // Check if solved
        if (it->second.proof == 0) {
            it->second.result = 1;
            ++nodes_proved_;
        } else if (it->second.disproof == 0) {
            it->second.result = 2;
            ++nodes_disproved_;
        }
    }

    std::unordered_map<uint64_t, PNSTTEntry> tt_;
    size_t tt_size_;
    RetrogradeSolverDB* retro_db_ = nullptr;
    std::string checkpoint_path_;
    uint64_t checkpoint_interval_ = 300;  // 5 minutes default

    State root_state_;
    uint64_t root_hash_;

    uint64_t nodes_searched_ = 0;
    uint64_t nodes_proved_ = 0;
    uint64_t nodes_disproved_ = 0;
    uint64_t retro_hits_ = 0;
};

}  // namespace bobail

// Global flag for signal handling
std::atomic<bool> g_stop_flag{false};

void signal_handler(int) {
    std::cout << "\nReceived interrupt signal, stopping gracefully...\n";
    g_stop_flag = true;
}

int main(int argc, char* argv[]) {
    std::string db_path;
    std::string checkpoint_path = "pns_checkpoint.bin";
    uint64_t checkpoint_interval = 300;  // 5 minutes
    bool resume = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (std::string(argv[i]) == "--checkpoint" && i + 1 < argc) {
            checkpoint_path = argv[++i];
        } else if (std::string(argv[i]) == "--interval" && i + 1 < argc) {
            checkpoint_interval = std::stoull(argv[++i]);
        } else if (std::string(argv[i]) == "--resume") {
            resume = true;
        } else if (std::string(argv[i]) == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --db PATH          Retrograde database path (optional)\n"
                      << "  --checkpoint PATH  Checkpoint file path (default: pns_checkpoint.bin)\n"
                      << "  --interval SECS    Checkpoint interval in seconds (default: 300)\n"
                      << "  --resume           Resume from checkpoint\n"
                      << "  --help             Show this help\n";
            return 0;
        }
    }

    // Set up signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    std::cout << "Enhanced PNS Solver\n";
    std::cout << "===================\n\n";

    // Open retrograde DB if specified
    std::unique_ptr<bobail::RetrogradeSolverDB> retro_db;
    if (!db_path.empty()) {
        retro_db = std::make_unique<bobail::RetrogradeSolverDB>();
        if (retro_db->open(db_path)) {
            std::cout << "Opened retrograde DB: " << db_path << "\n";
            std::cout << "  States: " << retro_db->num_states() << "\n";
            std::cout << "  Wins: " << retro_db->num_wins() << "\n";
            std::cout << "  Losses: " << retro_db->num_losses() << "\n";
        } else {
            std::cerr << "Warning: Failed to open retrograde DB, continuing without it\n";
            retro_db.reset();
        }
    }

    // Create solver
    bobail::EnhancedPNSSolver solver(1 << 26);  // 64M entry TT
    if (retro_db) {
        solver.set_retrograde_db(retro_db.get());
    }
    solver.set_checkpoint_path(checkpoint_path);
    solver.set_checkpoint_interval(checkpoint_interval);

    // Resume from checkpoint if requested
    if (resume) {
        if (solver.load_checkpoint()) {
            std::cout << "Resumed from checkpoint\n";
        } else {
            std::cout << "No checkpoint found, starting fresh\n";
        }
    }

    // Get starting position
    auto start = bobail::State::starting_position();
    std::cout << "\nStarting position:\n" << start.to_string() << "\n";
    std::cout << "Checkpoint interval: " << checkpoint_interval << " seconds\n";
    std::cout << "Checkpoint file: " << checkpoint_path << "\n\n";
    std::cout << "Press Ctrl+C to stop and save checkpoint\n\n";

    auto t0 = std::chrono::high_resolution_clock::now();

    bobail::Result result = solver.solve(start, g_stop_flag);

    auto t1 = std::chrono::high_resolution_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(t1 - t0).count();

    std::cout << "\n\n=== Results ===\n";
    std::cout << "Time: " << seconds << " seconds\n";
    std::cout << "Nodes searched: " << solver.nodes_searched() << "\n";
    std::cout << "Nodes proved: " << solver.nodes_proved() << "\n";
    std::cout << "Nodes disproved: " << solver.nodes_disproved() << "\n";
    std::cout << "Retrograde DB hits: " << solver.retro_hits() << "\n";
    std::cout << "TT entries: " << solver.tt_size() << "\n";

    std::cout << "\nResult: ";
    switch (result) {
        case bobail::Result::WIN:
            std::cout << "WHITE WINS with perfect play\n";
            break;
        case bobail::Result::LOSS:
            std::cout << "BLACK WINS with perfect play\n";
            break;
        case bobail::Result::DRAW:
            std::cout << "DRAW with perfect play\n";
            break;
        case bobail::Result::UNKNOWN:
            std::cout << "UNKNOWN (search incomplete, use --resume to continue)\n";
            break;
    }

    if (retro_db) {
        retro_db->close();
    }

    return 0;
}
