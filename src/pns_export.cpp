// Export proved positions from PNS checkpoint with web app URLs
// Usage: pns_export --checkpoint FILE [--wins N] [--losses N]

#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace bobail {

struct PNSTTEntry {
    uint64_t hash;
    uint32_t proof;
    uint32_t disproof;
    uint8_t result;  // 0=unknown, 1=win, 2=loss, 3=draw
};

// Convert position to web app URL format
std::string to_web_url(const State& s) {
    std::string url = "https://jasondaming.github.io/bobail-solver/?pos=";

    // Green pawns (white_pawns)
    std::string gp;
    for (int sq = 0; sq < 25; ++sq) {
        if (s.white_pawns & (1u << sq)) {
            if (sq < 10) gp += ('0' + sq);
            else gp += ('a' + (sq - 10));
        }
    }

    // Red pawns (black_pawns)
    std::string rp;
    for (int sq = 0; sq < 25; ++sq) {
        if (s.black_pawns & (1u << sq)) {
            if (sq < 10) rp += ('0' + sq);
            else rp += ('a' + (sq - 10));
        }
    }

    // Bobail position
    char bob = s.bobail_sq < 10 ? ('0' + s.bobail_sq) : ('a' + (s.bobail_sq - 10));

    // Turn and phase (assume bobail phase for non-starting positions)
    char turn = s.white_to_move ? 'w' : 'b';
    char phase = is_starting_position(s) ? 'P' : 'B';

    url += gp + "-" + rp + "-" + bob + turn + phase;
    return url;
}

void print_board(const State& s) {
    std::cout << "  0 1 2 3 4\n";
    for (int r = 0; r < 5; ++r) {
        std::cout << r << " ";
        for (int c = 0; c < 5; ++c) {
            int sq = r * 5 + c;
            if (s.bobail_sq == sq) std::cout << "B ";
            else if (s.white_pawns & (1u << sq)) std::cout << "G ";
            else if (s.black_pawns & (1u << sq)) std::cout << "R ";
            else std::cout << ". ";
        }
        std::cout << "\n";
    }
    std::cout << (s.white_to_move ? "Green" : "Red") << " to move\n";
}

} // namespace bobail

int main(int argc, char* argv[]) {
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    std::string checkpoint_path = "/workspace/pns_checkpoint.bin";
    int max_wins = 5;
    int max_losses = 5;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--checkpoint") == 0 && i + 1 < argc) {
            checkpoint_path = argv[++i];
        } else if (strcmp(argv[i], "--wins") == 0 && i + 1 < argc) {
            max_wins = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--losses") == 0 && i + 1 < argc) {
            max_losses = std::stoi(argv[++i]);
        }
    }

    // Load checkpoint
    std::ifstream in(checkpoint_path, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open checkpoint: " << checkpoint_path << "\n";
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

    std::cout << "Loading PNS checkpoint: " << num_entries << " entries\n";
    std::cout << "Proved: " << nodes_proved << ", Disproved: " << nodes_disproved << "\n\n";

    // We need to regenerate positions from the game tree to find ones matching hashes
    // For now, let's explore from starting position and collect proved positions

    std::vector<std::pair<bobail::State, bobail::PNSTTEntry>> wins;
    std::vector<std::pair<bobail::State, bobail::PNSTTEntry>> losses;

    // Load all entries into a map
    std::unordered_map<uint64_t, bobail::PNSTTEntry> tt;
    for (uint64_t i = 0; i < num_entries; ++i) {
        bobail::PNSTTEntry entry;
        in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        tt[entry.hash] = entry;
    }

    std::cout << "Loaded " << tt.size() << " entries\n\n";

    // BFS from starting position to find proved positions
    std::vector<bobail::State> queue;
    std::unordered_set<uint64_t> visited;

    bobail::State start = bobail::State::starting_position();
    queue.push_back(start);
    visited.insert(bobail::canonical_hash(start));

    size_t queue_idx = 0;
    while (queue_idx < queue.size() && (wins.size() < 100 || losses.size() < 100)) {
        bobail::State s = queue[queue_idx++];

        // Check if terminal
        bobail::GameResult gr = bobail::check_terminal(s);
        if (gr != bobail::GameResult::ONGOING) continue;

        // Check PNS table
        uint64_t hash = bobail::canonical_hash(s);
        auto it = tt.find(hash);
        if (it != tt.end()) {
            if (it->second.result == 1 && wins.size() < 100) {
                wins.push_back({s, it->second});
            } else if (it->second.result == 2 && losses.size() < 100) {
                losses.push_back({s, it->second});
            }
        }

        // Generate successors
        auto moves = bobail::generate_moves(s);
        for (const auto& m : moves) {
            bobail::State ns = bobail::apply_move(s, m);
            uint64_t ns_hash = bobail::canonical_hash(ns);
            if (visited.find(ns_hash) == visited.end()) {
                visited.insert(ns_hash);
                queue.push_back(ns);
            }
        }

        if (queue_idx % 10000 == 0) {
            std::cerr << "\rSearched " << queue_idx << " positions, found "
                      << wins.size() << " wins, " << losses.size() << " losses...";
        }
    }
    std::cerr << "\n\n";

    // Print sample wins
    std::cout << "=== SAMPLE PROVED WINS (current player wins) ===\n\n";
    for (int i = 0; i < std::min((int)wins.size(), max_wins); ++i) {
        const auto& [state, entry] = wins[i];
        std::cout << "Position " << (i+1) << ":\n";
        bobail::print_board(state);
        std::cout << "PN=" << entry.proof << " DN=" << entry.disproof << "\n";
        std::cout << "URL: " << bobail::to_web_url(state) << "\n\n";
    }

    // Print sample losses
    std::cout << "=== SAMPLE PROVED LOSSES (current player loses) ===\n\n";
    for (int i = 0; i < std::min((int)losses.size(), max_losses); ++i) {
        const auto& [state, entry] = losses[i];
        std::cout << "Position " << (i+1) << ":\n";
        bobail::print_board(state);
        std::cout << "PN=" << entry.proof << " DN=" << entry.disproof << "\n";
        std::cout << "URL: " << bobail::to_web_url(state) << "\n\n";
    }

    std::cout << "Total found: " << wins.size() << " wins, " << losses.size() << " losses\n";

    return 0;
}
