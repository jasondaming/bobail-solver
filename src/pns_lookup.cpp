// PNS checkpoint lookup tool
// Usage: pns_lookup --checkpoint FILE --query WP,BP,BOB,STM
//        pns_lookup --checkpoint FILE --interactive

#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <cstring>

namespace bobail {

struct PNSTTEntry {
    uint64_t hash;
    uint32_t proof;
    uint32_t disproof;
    uint8_t result;  // 0=unknown, 1=win, 2=loss, 3=draw
};

std::unordered_map<uint64_t, PNSTTEntry> pns_table;

bool load_checkpoint(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open: " << path << "\n";
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

    std::cerr << "Loading " << num_entries << " PNS entries...\n";

    pns_table.clear();
    pns_table.reserve(num_entries);

    for (uint64_t i = 0; i < num_entries; ++i) {
        PNSTTEntry entry;
        in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        pns_table[entry.hash] = entry;
    }

    std::cerr << "Loaded. Proved: " << nodes_proved << ", Disproved: " << nodes_disproved << "\n";
    return true;
}

State parse_position(const std::string& pos_str) {
    // Format: WP,BP,BOB,STM (hex,hex,int,int)
    State s;
    std::istringstream iss(pos_str);
    std::string wp_str, bp_str, bob_str, stm_str;

    std::getline(iss, wp_str, ',');
    std::getline(iss, bp_str, ',');
    std::getline(iss, bob_str, ',');
    std::getline(iss, stm_str, ',');

    s.white_pawns = std::stoul(wp_str, nullptr, 16);
    s.black_pawns = std::stoul(bp_str, nullptr, 16);
    s.bobail_sq = std::stoi(bob_str);
    s.white_to_move = (stm_str == "1");

    return s;
}

std::string result_to_string(uint8_t result) {
    switch (result) {
        case 1: return "WIN";
        case 2: return "LOSS";
        case 3: return "DRAW";
        default: return "UNKNOWN";
    }
}

void lookup_position(const State& s) {
    uint64_t hash = canonical_hash(s);

    std::cout << s.to_string() << "\n";

    auto it = pns_table.find(hash);
    if (it != pns_table.end()) {
        const auto& entry = it->second;
        std::cout << "Result: " << result_to_string(entry.result) << "\n";
        std::cout << "PN: " << entry.proof << ", DN: " << entry.disproof << "\n";

        // If position is solved, show best moves
        if (entry.result == 1 || entry.result == 2) {
            std::cout << "\nMoves:\n";
            auto moves = generate_moves(s);
            for (const auto& m : moves) {
                State child = apply_move(s, m);
                uint64_t child_hash = canonical_hash(child);

                std::string child_result_str;
                uint8_t child_result_code = 0;

                // First check if child is terminal
                GameResult gr = check_terminal(child);
                if (gr != GameResult::ONGOING) {
                    bool child_loses = (gr == GameResult::WHITE_WINS && !child.white_to_move) ||
                                       (gr == GameResult::BLACK_WINS && child.white_to_move);
                    child_result_code = child_loses ? 2 : 1;  // 2=LOSS, 1=WIN
                    child_result_str = child_loses ? "LOSS" : "WIN";
                } else {
                    auto child_it = pns_table.find(child_hash);
                    if (child_it != pns_table.end()) {
                        child_result_code = child_it->second.result;
                        child_result_str = result_to_string(child_result_code);
                    } else {
                        child_result_str = "?";
                    }
                }

                std::cout << "  " << m.to_string() << " -> " << child_result_str;
                if (entry.result == 1 && child_result_code == 2) {
                    std::cout << " *";  // Mark winning moves
                }
                std::cout << "\n";
            }
        }
    } else {
        std::cout << "Result: UNKNOWN (not in PNS table)\n";

        // Still show move evaluations if children are known
        auto moves = generate_moves(s);
        int known = 0;
        for (const auto& m : moves) {
            State child = apply_move(s, m);
            uint64_t child_hash = canonical_hash(child);
            if (pns_table.find(child_hash) != pns_table.end()) {
                ++known;
            }
        }
        if (known > 0) {
            std::cout << "\nMoves (" << known << "/" << moves.size() << " in PNS):\n";
            for (const auto& m : moves) {
                State child = apply_move(s, m);
                uint64_t child_hash = canonical_hash(child);

                auto child_it = pns_table.find(child_hash);
                if (child_it != pns_table.end()) {
                    std::cout << "  " << m.to_string() << " -> "
                              << result_to_string(child_it->second.result) << "\n";
                }
            }
        }
    }
}

// JSON output for server integration
void lookup_json(const State& s) {
    uint64_t hash = canonical_hash(s);

    std::cout << "{";

    auto it = pns_table.find(hash);
    if (it != pns_table.end()) {
        const auto& entry = it->second;
        std::string result = result_to_string(entry.result);
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);

        std::cout << "\"result\":\"" << result << "\",";
        std::cout << "\"pn\":" << entry.proof << ",";
        std::cout << "\"dn\":" << entry.disproof << ",";

        // Find best move
        std::cout << "\"moves\":[";
        auto moves = generate_moves(s);
        bool first = true;
        for (const auto& m : moves) {
            State child = apply_move(s, m);
            uint64_t child_hash = canonical_hash(child);

            std::string child_result;

            // First check if child is terminal (game over)
            GameResult gr = check_terminal(child);
            if (gr != GameResult::ONGOING) {
                // Terminal position - determine result from child's perspective
                bool child_wins = (gr == GameResult::WHITE_WINS && child.white_to_move) ||
                                  (gr == GameResult::BLACK_WINS && !child.white_to_move);
                bool child_loses = (gr == GameResult::WHITE_WINS && !child.white_to_move) ||
                                   (gr == GameResult::BLACK_WINS && child.white_to_move);
                if (child_wins) child_result = "win";
                else if (child_loses) child_result = "loss";
                else child_result = "draw";
            } else {
                // Check PNS table
                auto child_it = pns_table.find(child_hash);
                if (child_it != pns_table.end()) {
                    child_result = result_to_string(child_it->second.result);
                    std::transform(child_result.begin(), child_result.end(), child_result.begin(), ::tolower);
                } else {
                    continue;  // Skip moves with unknown non-terminal children
                }
            }

            if (!first) std::cout << ",";
            first = false;

            std::cout << "{\"bobail_to\":" << (int)m.bobail_to
                      << ",\"pawn_from\":" << (int)m.pawn_from
                      << ",\"pawn_to\":" << (int)m.pawn_to
                      << ",\"eval\":\"" << child_result << "\"}";
        }
        std::cout << "]";
    } else {
        std::cout << "\"result\":\"unknown\"";
    }

    std::cout << "}\n";
}

} // namespace bobail

int main(int argc, char* argv[]) {
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    std::string checkpoint_path;
    std::string query;
    bool interactive = false;
    bool json_output = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--checkpoint") == 0 && i + 1 < argc) {
            checkpoint_path = argv[++i];
        } else if (strcmp(argv[i], "--query") == 0 && i + 1 < argc) {
            query = argv[++i];
        } else if (strcmp(argv[i], "--interactive") == 0) {
            interactive = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        }
    }

    if (checkpoint_path.empty()) {
        std::cerr << "Usage: pns_lookup --checkpoint FILE [--query POS] [--interactive] [--json]\n";
        std::cerr << "  POS format: WP,BP,BOB,STM (hex,hex,int,0/1)\n";
        return 1;
    }

    if (!bobail::load_checkpoint(checkpoint_path)) {
        return 1;
    }

    if (!query.empty()) {
        bobail::State s = bobail::parse_position(query);
        if (json_output) {
            bobail::lookup_json(s);
        } else {
            bobail::lookup_position(s);
        }
    } else if (interactive) {
        std::cerr << "Interactive mode. Enter positions (WP,BP,BOB,STM) or 'quit':\n";
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "quit" || line == "exit") break;
            if (line.empty()) continue;

            try {
                bobail::State s = bobail::parse_position(line);
                if (json_output) {
                    bobail::lookup_json(s);
                } else {
                    bobail::lookup_position(s);
                }
            } catch (...) {
                std::cout << "Invalid position format\n";
            }
            std::cout << "\n";
        }
    } else {
        // Default: show starting position
        bobail::State s = bobail::State::starting_position();
        if (json_output) {
            bobail::lookup_json(s);
        } else {
            bobail::lookup_position(s);
        }
    }

    return 0;
}
