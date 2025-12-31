// Verify all Black responses to the winning first move
// Check that White can force a win regardless of Black's response

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
}

int main(int argc, char* argv[]) {
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    std::string checkpoint = "/workspace/pns_checkpoint.bin";
    if (argc > 1) {
        checkpoint = argv[1];
    }

    std::ifstream in(checkpoint, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open checkpoint\n";
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

    std::cout << "Loaded.\n\n";

    // Starting position
    bobail::State root = bobail::State::starting_position();

    // The winning move: B->13 P:3->8
    std::cout << "=== CHECKING ALL BLACK RESPONSES TO B->13 P:3->8 ===\n\n";

    // Find and play this move
    bobail::State after_white;
    auto moves = bobail::generate_moves(root);
    for (const auto& move : moves) {
        if (move.to_string() == "B->13 P:3->8") {
            after_white = bobail::apply_move(root, move);
            break;
        }
    }

    std::cout << "Position after 1. B->13 P:3->8:\n";
    print_board(after_white);
    std::cout << "Black to move\n\n";

    uint64_t after_hash = bobail::canonical_hash(after_white);
    auto it = pns_table.find(after_hash);
    if (it != pns_table.end()) {
        std::cout << "This position: PN=" << it->second.proof
                  << " DN=" << it->second.disproof
                  << " result=" << (int)it->second.result << "\n\n";
    }

    // Check all of Black's responses
    auto black_moves = bobail::generate_moves(after_white);
    std::cout << "Black has " << black_moves.size() << " legal moves:\n\n";

    int white_wins = 0;
    int black_wins = 0;
    int unknown = 0;
    int not_in_tt = 0;

    for (const auto& move : black_moves) {
        bobail::State after_black = bobail::apply_move(after_white, move);
        uint64_t child_hash = bobail::canonical_hash(after_black);

        std::cout << "  " << move.to_string() << ": ";

        auto child_it = pns_table.find(child_hash);
        if (child_it != pns_table.end()) {
            std::cout << "PN=" << child_it->second.proof
                      << " DN=" << child_it->second.disproof
                      << " result=" << (int)child_it->second.result;

            // Now it's White's turn in this position
            // result=1 means WIN for white, result=2 means LOSS for white
            if (child_it->second.result == 1) {
                std::cout << " -> WHITE HAS FORCED WIN";
                ++white_wins;
            } else if (child_it->second.result == 2) {
                std::cout << " -> Black has forced win";
                ++black_wins;
            } else {
                std::cout << " -> unknown";
                ++unknown;
            }
            std::cout << "\n";
        } else {
            std::cout << "NOT IN TT\n";
            ++not_in_tt;
        }
    }

    std::cout << "\n=== SUMMARY ===\n";
    std::cout << "White can force a win after: " << white_wins << " Black responses\n";
    std::cout << "Black can force a win after: " << black_wins << " Black responses\n";
    std::cout << "Unknown: " << unknown << "\n";
    std::cout << "Not in TT: " << not_in_tt << "\n\n";

    if (black_wins == 0 && white_wins > 0) {
        std::cout << "*** CONFIRMED: B->13 P:3->8 is a winning move! ***\n";
        std::cout << "    White can force a win against any Black response.\n";
    } else if (black_wins > 0) {
        std::cout << "WARNING: Black has escape routes!\n";
    } else {
        std::cout << "Result inconclusive - more positions need to be proved.\n";
    }

    return 0;
}
