#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include "retrograde_db.h"
#include <iostream>
#include <fstream>
#include <queue>
#include <unordered_set>
#include <string>
#include <cstring>

// Export opening book from solved database to JSON format
// The JSON maps position strings to evaluation data

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --db PATH           Database directory (required)\n"
              << "  --output FILE       Output JSON file (required)\n"
              << "  --depth N           Maximum ply depth to export (default: 20)\n"
              << "  --official          Use Official rules (pawns must move max distance) [default]\n"
              << "  --flexible          Use Flexible rules (pawns can stop anywhere)\n"
              << "  --help              Show this help\n";
}

// Convert state to a compact string representation for JSON key
std::string state_to_key(const bobail::State& s) {
    // Format: "white_pawns,black_pawns,bobail,side"
    // Each as hex to keep it compact
    std::string key;
    char buf[64];
    snprintf(buf, sizeof(buf), "%x,%x,%d,%d",
             s.white_pawns, s.black_pawns, s.bobail_sq, s.white_to_move ? 1 : 0);
    return std::string(buf);
}

int main(int argc, char* argv[]) {
    std::string db_path;
    std::string output_file;
    int max_depth = 20;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "--db") == 0) {
            if (i + 1 < argc) {
                db_path = argv[++i];
            } else {
                std::cerr << "Error: --db requires a path\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                std::cerr << "Error: --output requires a filename\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--depth") == 0) {
            if (i + 1 < argc) {
                max_depth = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: --depth requires a number\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--official") == 0) {
            bobail::g_rules_variant = bobail::RulesVariant::OFFICIAL;
        } else if (std::strcmp(argv[i], "--flexible") == 0) {
            bobail::g_rules_variant = bobail::RulesVariant::FLEXIBLE;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (db_path.empty() || output_file.empty()) {
        std::cerr << "Error: --db and --output are required\n";
        print_usage(argv[0]);
        return 1;
    }

    // Initialize tables
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    std::cout << "Opening Book Exporter\n";
    std::cout << "====================\n";
    std::cout << "Rules variant: "
              << (bobail::g_rules_variant == bobail::RulesVariant::OFFICIAL
                  ? "OFFICIAL" : "FLEXIBLE")
              << "\n";
    std::cout << "Max depth: " << max_depth << " plies\n\n";

    // Open solver database
    bobail::RetrogradeSolverDB solver;
    if (!solver.open(db_path)) {
        std::cerr << "Failed to open database: " << db_path << "\n";
        return 1;
    }

    std::cout << "Database opened. Total states: " << solver.num_states() << "\n";
    std::cout << "Starting position result: ";
    bobail::Result start_result = solver.starting_result();
    switch (start_result) {
        case bobail::Result::WIN: std::cout << "WIN\n"; break;
        case bobail::Result::LOSS: std::cout << "LOSS\n"; break;
        case bobail::Result::DRAW: std::cout << "DRAW\n"; break;
        default: std::cout << "UNKNOWN\n"; break;
    }

    // BFS to explore positions up to max_depth
    struct QueueEntry {
        bobail::State state;
        int depth;
    };

    std::queue<QueueEntry> queue;
    std::unordered_set<uint64_t> visited;

    auto start = bobail::State::starting_position();
    queue.push({start, 0});
    visited.insert(bobail::pack_state(bobail::canonicalize(start).first));

    std::ofstream out(output_file);
    if (!out) {
        std::cerr << "Failed to open output file: " << output_file << "\n";
        return 1;
    }

    out << "{\n";
    bool first = true;
    uint64_t exported = 0;

    std::cout << "Exporting positions...\n";

    while (!queue.empty()) {
        auto [state, depth] = queue.front();
        queue.pop();

        // Get evaluation for this position
        bobail::Result result = solver.get_result(state);

        // Output position
        if (!first) out << ",\n";
        first = false;

        out << "  \"" << state_to_key(state) << "\": {";
        out << "\"r\":" << static_cast<int>(result);

        // For non-terminal positions, also output best move
        if (result != bobail::Result::UNKNOWN) {
            bobail::Move best = solver.get_best_move(state);
            out << ",\"b\":[" << static_cast<int>(best.bobail_to) << ","
                << static_cast<int>(best.pawn_from) << ","
                << static_cast<int>(best.pawn_to) << "]";
        }
        out << "}";

        exported++;
        if (exported % 10000 == 0) {
            std::cout << "\rExported: " << exported << " positions (depth " << depth << ", queue: " << queue.size() << ")   " << std::flush;
        }

        // Expand children if not at max depth
        if (depth < max_depth) {
            auto moves = bobail::generate_moves(state);
            for (const auto& move : moves) {
                bobail::State next = bobail::apply_move(state, move);
                uint64_t packed = bobail::pack_state(bobail::canonicalize(next).first);

                if (visited.find(packed) == visited.end()) {
                    visited.insert(packed);
                    queue.push({next, depth + 1});
                }
            }
        }
    }

    out << "\n}\n";
    out.close();

    std::cout << "\n\nExport complete!\n";
    std::cout << "Total positions exported: " << exported << "\n";
    std::cout << "Output file: " << output_file << "\n";

    solver.close();
    return 0;
}
