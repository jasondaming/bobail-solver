// Practical Bobail engine for strong play against humans
// Uses PNS checkpoint data + alpha-beta search

#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <limits>
#include <sstream>

namespace bobail {

// PNS TT entry (must match pns_enhanced.cpp)
struct PNSTTEntry {
    uint64_t hash;
    uint32_t proof;
    uint32_t disproof;
    uint8_t result;  // 0=unknown, 1=win, 2=loss, 3=draw
};

class BobailEngine {
public:
    BobailEngine() = default;

    bool load_pns_data(const std::string& checkpoint_path) {
        std::ifstream in(checkpoint_path, std::ios::binary);
        if (!in) {
            std::cerr << "Cannot open checkpoint: " << checkpoint_path << "\n";
            return false;
        }

        uint64_t magic, version, num_entries;
        uint64_t nodes_searched, nodes_proved, nodes_disproved, retro_hits;

        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (magic != 0x504E5343484B5054ULL) {
            std::cerr << "Invalid checkpoint magic\n";
            return false;
        }

        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        in.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
        in.read(reinterpret_cast<char*>(&nodes_searched), sizeof(nodes_searched));
        in.read(reinterpret_cast<char*>(&nodes_proved), sizeof(nodes_proved));
        in.read(reinterpret_cast<char*>(&nodes_disproved), sizeof(nodes_disproved));
        in.read(reinterpret_cast<char*>(&retro_hits), sizeof(retro_hits));

        std::cout << "Loading " << num_entries << " PNS entries...\n";

        pns_table_.clear();
        pns_table_.reserve(num_entries);

        for (uint64_t i = 0; i < num_entries; ++i) {
            PNSTTEntry entry;
            in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
            pns_table_[entry.hash] = entry;

            if ((i + 1) % 5000000 == 0) {
                std::cout << "  Loaded " << (i + 1) / 1000000 << "M entries...\n";
            }
        }

        std::cout << "Loaded " << pns_table_.size() << " entries\n";
        std::cout << "  Proved: " << nodes_proved << "\n";
        std::cout << "  Disproved: " << nodes_disproved << "\n";

        return true;
    }

    // Get best move for a position
    Move get_best_move(const State& state, int time_ms = 5000) {
        nodes_searched_ = 0;
        auto start = std::chrono::steady_clock::now();
        deadline_ = start + std::chrono::milliseconds(time_ms);

        auto moves = generate_moves(state);
        if (moves.empty()) return Move();
        if (moves.size() == 1) return moves[0];

        Move best_move = moves[0];
        int best_score = -INFINITY_SCORE;

        // First, check if any move leads to a known win
        for (const auto& move : moves) {
            State child = apply_move(state, move);
            uint64_t child_hash = canonical_hash(child);

            auto it = pns_table_.find(child_hash);
            if (it != pns_table_.end()) {
                // Result is from opponent's perspective after our move
                if (it->second.result == 2) {  // Loss for opponent = win for us
                    std::cout << "Found forced win: " << move.to_string() << "\n";
                    return move;
                }
            }
        }

        // Iterative deepening
        for (int depth = 1; depth <= 30; ++depth) {
            int alpha = -INFINITY_SCORE;
            int beta = INFINITY_SCORE;
            Move iter_best = moves[0];
            int iter_score = -INFINITY_SCORE;

            // Sort moves by previous iteration's scores
            for (const auto& move : moves) {
                if (time_up()) break;

                State child = apply_move(state, move);
                int score = -alpha_beta(child, depth - 1, -beta, -alpha);

                if (score > iter_score) {
                    iter_score = score;
                    iter_best = move;
                }
                if (score > alpha) {
                    alpha = score;
                }
            }

            if (!time_up()) {
                best_move = iter_best;
                best_score = iter_score;

                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                std::cout << "Depth " << depth << ": " << best_move.to_string()
                          << " score=" << best_score
                          << " nodes=" << nodes_searched_
                          << " time=" << elapsed << "ms\n";

                // If we found a winning score, stop
                if (best_score > 90000) break;
            } else {
                break;
            }
        }

        return best_move;
    }

    uint64_t nodes_searched() const { return nodes_searched_; }

private:
    static constexpr int INFINITY_SCORE = 100000;
    static constexpr int WIN_SCORE = 99000;
    static constexpr int LOSS_SCORE = -99000;

    std::unordered_map<uint64_t, PNSTTEntry> pns_table_;
    uint64_t nodes_searched_ = 0;
    std::chrono::steady_clock::time_point deadline_;

    bool time_up() const {
        return std::chrono::steady_clock::now() >= deadline_;
    }

    int alpha_beta(const State& state, int depth, int alpha, int beta) {
        ++nodes_searched_;

        // Check terminal
        GameResult gr = check_terminal(state);
        if (gr != GameResult::ONGOING) {
            if ((gr == GameResult::WHITE_WINS && state.white_to_move) ||
                (gr == GameResult::BLACK_WINS && !state.white_to_move)) {
                return WIN_SCORE;  // Current player wins
            } else {
                return LOSS_SCORE;  // Current player loses
            }
        }

        // Check PNS table
        uint64_t hash = canonical_hash(state);
        auto it = pns_table_.find(hash);
        if (it != pns_table_.end()) {
            const auto& entry = it->second;

            // Known result
            if (entry.result == 1) return WIN_SCORE;   // Win
            if (entry.result == 2) return LOSS_SCORE;  // Loss
            if (entry.result == 3) return 0;           // Draw

            // Use PN/DN as heuristic for leaf evaluation
            if (depth <= 0 || time_up()) {
                return pn_dn_eval(entry.proof, entry.disproof);
            }
        } else if (depth <= 0 || time_up()) {
            // No PNS data, use simple evaluation
            return evaluate(state);
        }

        auto moves = generate_moves(state);
        if (moves.empty()) {
            return LOSS_SCORE;  // No moves = loss
        }

        // Sort moves by PNS heuristic
        sort_moves(state, moves);

        int best_score = -INFINITY_SCORE;
        for (const auto& move : moves) {
            if (time_up()) break;

            State child = apply_move(state, move);
            int score = -alpha_beta(child, depth - 1, -beta, -alpha);

            if (score > best_score) {
                best_score = score;
            }
            if (score > alpha) {
                alpha = score;
            }
            if (alpha >= beta) {
                break;  // Beta cutoff
            }
        }

        return best_score;
    }

    // Convert PN/DN to evaluation score
    int pn_dn_eval(uint32_t pn, uint32_t dn) const {
        if (pn == 0) return WIN_SCORE;
        if (dn == 0) return LOSS_SCORE;

        // PN/DN ratio as heuristic: low PN = likely win, low DN = likely loss
        // Scale to [-10000, 10000] range
        double ratio = static_cast<double>(dn) / (pn + dn);  // 0 to 1, higher = better
        return static_cast<int>((ratio - 0.5) * 20000);
    }

    // Simple evaluation for positions not in PNS table
    int evaluate(const State& state) const {
        int score = 0;

        // Material
        int white_pawns = __builtin_popcount(state.white_pawns);
        int black_pawns = __builtin_popcount(state.black_pawns);
        score += (white_pawns - black_pawns) * 100;

        // Bobail position (center control)
        int bobail_row = State::row(state.bobail_sq);
        int bobail_col = State::col(state.bobail_sq);

        // Bobail closer to center is better for defender
        int center_dist = std::abs(bobail_row - 2) + std::abs(bobail_col - 2);
        score += center_dist * 10;  // Bobail away from center helps attacker

        // Mobility
        auto moves = generate_moves(state);
        score += moves.size() * 5;

        return state.white_to_move ? score : -score;
    }

    // Sort moves by PNS heuristic
    void sort_moves(const State& state, std::vector<Move>& moves) const {
        std::vector<std::pair<int, Move>> scored_moves;
        scored_moves.reserve(moves.size());

        for (const auto& move : moves) {
            State child = apply_move(state, move);
            uint64_t child_hash = canonical_hash(child);

            int score = 0;
            auto it = pns_table_.find(child_hash);
            if (it != pns_table_.end()) {
                // Result is from opponent's perspective
                if (it->second.result == 2) {
                    score = 1000000;  // Forces opponent loss = our win
                } else if (it->second.result == 1) {
                    score = -1000000;  // Gives opponent win = our loss
                } else {
                    // Use inverted PN/DN (opponent's PN is our DN)
                    score = -pn_dn_eval(it->second.proof, it->second.disproof);
                }
            }
            scored_moves.push_back({score, move});
        }

        std::sort(scored_moves.begin(), scored_moves.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        for (size_t i = 0; i < moves.size(); ++i) {
            moves[i] = scored_moves[i].second;
        }
    }
};

}  // namespace bobail

void print_board(const bobail::State& state) {
    std::cout << (state.white_to_move ? "White" : "Black") << " to move\n";
    std::cout << "  01234\n";
    for (int r = 0; r < 5; ++r) {
        std::cout << r << " ";
        for (int c = 0; c < 5; ++c) {
            int pos = r * 5 + c;
            if (state.bobail_sq == pos) {
                std::cout << 'B';
            } else if (state.white_pawns & (1 << pos)) {
                std::cout << 'W';
            } else if (state.black_pawns & (1 << pos)) {
                std::cout << 'X';
            } else {
                std::cout << '.';
            }
        }
        std::cout << "\n";
    }
}

int main(int argc, char* argv[]) {
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    bobail::BobailEngine engine;

    // Load PNS data
    std::string checkpoint = "/workspace/pns_checkpoint.bin";
    if (argc > 1) {
        checkpoint = argv[1];
    }

    std::cout << "Bobail Engine v1.0\n";
    std::cout << "==================\n\n";

    if (!engine.load_pns_data(checkpoint)) {
        std::cout << "Warning: No PNS data loaded, using pure alpha-beta\n";
    }

    bobail::State state = bobail::State::starting_position();

    std::cout << "\nCommands:\n";
    std::cout << "  moves             - Show all legal moves\n";
    std::cout << "  play <n>          - Play move number n from the list\n";
    std::cout << "  go [time_ms]      - Let engine play (default 5000ms)\n";
    std::cout << "  auto              - Engine plays both sides\n";
    std::cout << "  new               - New game\n";
    std::cout << "  quit              - Exit\n\n";

    print_board(state);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit") {
            break;
        } else if (cmd == "new") {
            state = bobail::State::starting_position();
            print_board(state);
        } else if (cmd == "moves") {
            auto legal_moves = bobail::generate_moves(state);
            std::cout << "Legal moves (" << legal_moves.size() << "):\n";
            for (size_t i = 0; i < legal_moves.size(); ++i) {
                std::cout << "  " << (i + 1) << ". " << legal_moves[i].to_string() << "\n";
            }
        } else if (cmd == "play") {
            int n;
            iss >> n;
            auto legal_moves = bobail::generate_moves(state);
            if (n >= 1 && n <= (int)legal_moves.size()) {
                auto move = legal_moves[n - 1];
                std::cout << "Playing: " << move.to_string() << "\n";
                state = bobail::apply_move(state, move);
                print_board(state);

                auto gr = bobail::check_terminal(state);
                if (gr == bobail::GameResult::WHITE_WINS) {
                    std::cout << "WHITE WINS!\n";
                } else if (gr == bobail::GameResult::BLACK_WINS) {
                    std::cout << "BLACK WINS!\n";
                }
            } else {
                std::cout << "Invalid move number. Use 'moves' to see legal moves.\n";
            }
        } else if (cmd == "go") {
            int time_ms = 5000;
            iss >> time_ms;

            auto legal_moves = bobail::generate_moves(state);
            if (legal_moves.empty()) {
                std::cout << "No legal moves!\n";
                continue;
            }

            auto move = engine.get_best_move(state, time_ms);
            std::cout << "Best move: " << move.to_string() << "\n";
            state = bobail::apply_move(state, move);
            print_board(state);

            auto gr = bobail::check_terminal(state);
            if (gr == bobail::GameResult::WHITE_WINS) {
                std::cout << "WHITE WINS!\n";
            } else if (gr == bobail::GameResult::BLACK_WINS) {
                std::cout << "BLACK WINS!\n";
            }
        } else if (cmd == "auto") {
            // Engine plays both sides
            while (true) {
                auto legal_moves = bobail::generate_moves(state);
                if (legal_moves.empty()) {
                    std::cout << "No legal moves - game over!\n";
                    break;
                }

                auto gr = bobail::check_terminal(state);
                if (gr != bobail::GameResult::ONGOING) {
                    if (gr == bobail::GameResult::WHITE_WINS) {
                        std::cout << "WHITE WINS!\n";
                    } else if (gr == bobail::GameResult::BLACK_WINS) {
                        std::cout << "BLACK WINS!\n";
                    }
                    break;
                }

                auto move = engine.get_best_move(state, 2000);
                std::cout << (state.white_to_move ? "White" : "Black")
                          << " plays: " << move.to_string() << "\n";
                state = bobail::apply_move(state, move);
                print_board(state);
            }
        } else if (!cmd.empty()) {
            std::cout << "Unknown command: " << cmd << "\n";
        }
    }

    return 0;
}
