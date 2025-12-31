// Verify PNS "forced win" claims
// Checks if starting position children are truly proved

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
    if (magic != 0x504E5343484B5054ULL) {
        std::cerr << "Invalid checkpoint magic\n";
        return 1;
    }

    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
    in.read(reinterpret_cast<char*>(&nodes_searched), sizeof(nodes_searched));
    in.read(reinterpret_cast<char*>(&nodes_proved), sizeof(nodes_proved));
    in.read(reinterpret_cast<char*>(&nodes_disproved), sizeof(nodes_disproved));
    in.read(reinterpret_cast<char*>(&retro_hits), sizeof(retro_hits));

    std::cout << "Loading " << num_entries << " PNS entries...\n";
    std::cout << "Stats: proved=" << nodes_proved << " disproved=" << nodes_disproved << "\n\n";

    std::unordered_map<uint64_t, bobail::PNSTTEntry> pns_table;
    pns_table.reserve(num_entries);

    for (uint64_t i = 0; i < num_entries; ++i) {
        bobail::PNSTTEntry entry;
        in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        pns_table[entry.hash] = entry;
    }

    std::cout << "Loaded " << pns_table.size() << " entries\n\n";

    // Check starting position
    bobail::State root = bobail::State::starting_position();
    uint64_t root_hash = bobail::canonical_hash(root);

    std::cout << "=== STARTING POSITION ===\n";
    std::cout << "Side to move: " << (root.white_to_move ? "White" : "Black") << "\n";
    std::cout << "Root hash: " << std::hex << root_hash << std::dec << "\n";

    auto root_it = pns_table.find(root_hash);
    if (root_it != pns_table.end()) {
        std::cout << "Root in TT: PN=" << root_it->second.proof
                  << " DN=" << root_it->second.disproof
                  << " result=" << (int)root_it->second.result << "\n";
    } else {
        std::cout << "Root NOT in TT!\n";
    }

    // Check all legal moves from starting position
    std::cout << "\n=== CHECKING ALL LEGAL MOVES ===\n";
    auto moves = bobail::generate_moves(root);
    std::cout << "Total legal moves: " << moves.size() << "\n\n";

    int wins_found = 0;
    int losses_found = 0;
    int unknown_found = 0;
    int not_in_tt = 0;

    for (const auto& move : moves) {
        bobail::State child = bobail::apply_move(root, move);
        uint64_t child_hash = bobail::canonical_hash(child);

        auto it = pns_table.find(child_hash);
        if (it != pns_table.end()) {
            std::cout << move.to_string() << ": ";
            std::cout << "PN=" << it->second.proof;
            std::cout << " DN=" << it->second.disproof;
            std::cout << " result=" << (int)it->second.result;

            if (it->second.result == 1) {
                std::cout << " [WIN for player-to-move = Black wins = BAD for us]";
                ++losses_found;
            } else if (it->second.result == 2) {
                std::cout << " [LOSS for player-to-move = Black loses = GOOD for us!]";
                ++wins_found;
                // Extra verification
                if (it->second.proof == 0xFFFFFFFF && it->second.disproof == 0) {
                    std::cout << " (VERIFIED: proof=INF, disproof=0)";
                } else {
                    std::cout << " (WARNING: proof/disproof don't match result!)";
                }
            } else if (it->second.result == 0) {
                ++unknown_found;
            }
            std::cout << "\n";
        } else {
            std::cout << move.to_string() << ": NOT IN TT\n";
            ++not_in_tt;
        }
    }

    std::cout << "\n=== SUMMARY ===\n";
    std::cout << "Moves leading to opponent LOSS (forced wins for us): " << wins_found << "\n";
    std::cout << "Moves leading to opponent WIN (losing moves): " << losses_found << "\n";
    std::cout << "Moves with unknown result: " << unknown_found << "\n";
    std::cout << "Moves not in TT: " << not_in_tt << "\n";

    if (wins_found > 0) {
        std::cout << "\n*** FORCED WINS EXIST FROM STARTING POSITION! ***\n";
    } else if (losses_found == (int)moves.size()) {
        std::cout << "\n*** ALL MOVES LOSE - BLACK HAS A WINNING STRATEGY! ***\n";
    } else {
        std::cout << "\n*** NO FORCED WINS FOUND YET ***\n";
    }

    return 0;
}
