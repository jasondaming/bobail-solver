// Trace a winning line from a proved position
// Shows the complete winning strategy

#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <iostream>
#include <fstream>
#include <unordered_map>

namespace bobail {

struct PNSTTEntry {
    uint64_t hash;
    uint32_t proof;
    uint32_t disproof;
    uint8_t result;  // 0=unknown, 1=win, 2=loss, 3=draw
};

}  // namespace bobail

std::unordered_map<uint64_t, bobail::PNSTTEntry> pns_table;

void print_board(const bobail::State& state) {
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
    std::cout << (state.white_to_move ? "White" : "Black") << " to move\n\n";
}

// Find the best move for current player from proved positions
bobail::Move find_best_move(const bobail::State& state, bool maximizing) {
    auto moves = bobail::generate_moves(state);

    bobail::Move best_move;
    int best_score = maximizing ? -999999 : 999999;

    for (const auto& move : moves) {
        bobail::State child = bobail::apply_move(state, move);
        uint64_t child_hash = bobail::canonical_hash(child);

        auto it = pns_table.find(child_hash);
        if (it == pns_table.end()) continue;

        int score = 0;
        // result is from child's perspective (opponent after our move)
        if (it->second.result == 2) {  // Opponent loses = we win
            score = maximizing ? 100000 : -100000;
        } else if (it->second.result == 1) {  // Opponent wins = we lose
            score = maximizing ? -100000 : 100000;
        } else {
            // Use DN/(PN+DN) as heuristic
            double ratio = (double)it->second.disproof / (it->second.proof + it->second.disproof + 1);
            score = (int)((ratio - 0.5) * 1000);
            if (!maximizing) score = -score;
        }

        if (maximizing ? (score > best_score) : (score < best_score)) {
            best_score = score;
            best_move = move;
        }
    }

    return best_move;
}

int main(int argc, char* argv[]) {
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    std::string checkpoint = "/workspace/pns_checkpoint.bin";
    if (argc > 1) {
        checkpoint = argv[1];
    }

    // Load checkpoint
    std::ifstream in(checkpoint, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open checkpoint: " << checkpoint << "\n";
        return 1;
    }

    uint64_t magic, version, num_entries;
    uint64_t nodes_searched, nodes_proved, nodes_disproved, retro_hits;

    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
    in.read(reinterpret_cast<char*>(&nodes_searched), sizeof(nodes_searched));
    in.read(reinterpret_cast<char*>(&nodes_proved), sizeof(nodes_proved));
    in.read(reinterpret_cast<char*>(&nodes_disproved), sizeof(nodes_disproved));
    in.read(reinterpret_cast<char*>(&retro_hits), sizeof(retro_hits));

    std::cout << "Loading " << num_entries << " PNS entries...\n";

    for (uint64_t i = 0; i < num_entries; ++i) {
        bobail::PNSTTEntry entry;
        in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        pns_table[entry.hash] = entry;
    }

    std::cout << "Loaded " << pns_table.size() << " entries\n\n";

    // Start from root and trace winning line
    bobail::State state = bobail::State::starting_position();

    std::cout << "=== TRACING WINNING LINE ===\n\n";
    std::cout << "Initial position:\n";
    print_board(state);

    int move_num = 1;
    while (true) {
        auto gr = bobail::check_terminal(state);
        if (gr != bobail::GameResult::ONGOING) {
            if (gr == bobail::GameResult::WHITE_WINS) {
                std::cout << "*** WHITE WINS! ***\n";
            } else {
                std::cout << "*** BLACK WINS! ***\n";
            }
            break;
        }

        auto moves = bobail::generate_moves(state);
        if (moves.empty()) {
            std::cout << "No legal moves - " << (state.white_to_move ? "White" : "Black") << " loses!\n";
            break;
        }

        // Find best proved move
        bobail::Move best;
        bool found_proved = false;

        for (const auto& move : moves) {
            bobail::State child = bobail::apply_move(state, move);
            uint64_t child_hash = bobail::canonical_hash(child);

            auto it = pns_table.find(child_hash);
            if (it != pns_table.end() && it->second.result == 2) {
                // This move forces opponent into a lost position
                best = move;
                found_proved = true;
                break;
            }
        }

        if (!found_proved) {
            // Look for move where opponent has no winning response
            best = find_best_move(state, state.white_to_move);
            if (best.bobail_to == 255) {
                std::cout << "No good move found - line ends here\n";
                break;
            }
        }

        std::cout << move_num << ". " << (state.white_to_move ? "White" : "Black")
                  << " plays: " << best.to_string();

        bobail::State child = bobail::apply_move(state, best);
        uint64_t child_hash = bobail::canonical_hash(child);
        auto it = pns_table.find(child_hash);
        if (it != pns_table.end()) {
            std::cout << " (result=" << (int)it->second.result << ")";
        }
        std::cout << "\n";

        state = child;
        print_board(state);

        ++move_num;
        if (move_num > 50) {
            std::cout << "Stopping at move 50\n";
            break;
        }
    }

    return 0;
}
