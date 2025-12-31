// Trace the alternate winning line (B->11 P:1->6)

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

bobail::Move find_best_move(const bobail::State& state) {
    auto moves = bobail::generate_moves(state);

    // First look for moves that lead to proved loss for opponent
    for (const auto& move : moves) {
        bobail::State child = bobail::apply_move(state, move);
        uint64_t child_hash = bobail::canonical_hash(child);

        auto it = pns_table.find(child_hash);
        if (it != pns_table.end() && it->second.result == 2) {
            return move;
        }
    }

    // Otherwise pick move with lowest PN (for attacker) or lowest DN (for defender)
    bobail::Move best;
    uint32_t best_score = 0xFFFFFFFF;

    for (const auto& move : moves) {
        bobail::State child = bobail::apply_move(state, move);
        uint64_t child_hash = bobail::canonical_hash(child);

        auto it = pns_table.find(child_hash);
        if (it != pns_table.end()) {
            // Use PN as score - we want low PN (opponent needs more work to win)
            if (it->second.proof < best_score) {
                best_score = it->second.proof;
                best = move;
            }
        }
    }

    return best;
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
    uint64_t stats[4];

    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
    in.read(reinterpret_cast<char*>(stats), sizeof(stats));

    std::cout << "Loading " << num_entries << " entries...\n";

    for (uint64_t i = 0; i < num_entries; ++i) {
        bobail::PNSTTEntry entry;
        in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        pns_table[entry.hash] = entry;
    }

    std::cout << "Loaded " << pns_table.size() << " entries\n\n";

    // Start from root and make the alternate first move
    bobail::State state = bobail::State::starting_position();

    std::cout << "=== TRACING ALTERNATE WINNING LINE (B->11 P:1->6) ===\n\n";
    std::cout << "Initial position:\n";
    print_board(state);

    // Find and play the alternate first move: B->11 P:1->6
    auto moves = bobail::generate_moves(state);
    for (const auto& move : moves) {
        if (move.to_string() == "B->11 P:1->6") {
            std::cout << "1. White plays: " << move.to_string() << "\n";
            state = bobail::apply_move(state, move);
            break;
        }
    }
    print_board(state);

    int move_num = 2;
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

        auto legal_moves = bobail::generate_moves(state);
        if (legal_moves.empty()) {
            std::cout << (state.white_to_move ? "White" : "Black") << " has no moves - loses!\n";
            break;
        }

        bobail::Move best = find_best_move(state);
        if (best.bobail_to == 255) {
            std::cout << "No move found in TT\n";
            break;
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
        if (move_num > 50) break;
    }

    return 0;
}
