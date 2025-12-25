#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include "retrograde_db.h"
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>

// Simple command-line tool to lookup positions in the solved database
// Can be used as a backend for the web UI via a simple API

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --db PATH           Database directory (required)\n"
              << "  --official          Use Official rules [default]\n"
              << "  --flexible          Use Flexible rules\n"
              << "  --interactive       Interactive mode\n"
              << "  --query WP,BP,BOB,STM  Query a specific position\n"
              << "                      WP=white pawns (hex), BP=black pawns (hex)\n"
              << "                      BOB=bobail square, STM=1 for white, 0 for black\n"
              << "  --help              Show this help\n";
}

void print_position_info(bobail::RetrogradeSolverDB& solver, const bobail::State& s) {
    std::cout << s.to_string() << "\n";

    bobail::Result result = solver.get_result(s);
    std::cout << "Result: ";
    switch (result) {
        case bobail::Result::WIN: std::cout << "WIN (for " << (s.white_to_move ? "White" : "Black") << ")\n"; break;
        case bobail::Result::LOSS: std::cout << "LOSS (for " << (s.white_to_move ? "White" : "Black") << ")\n"; break;
        case bobail::Result::DRAW: std::cout << "DRAW\n"; break;
        default: std::cout << "UNKNOWN\n"; break;
    }

    if (result != bobail::Result::UNKNOWN) {
        bobail::Move best = solver.get_best_move(s);
        std::cout << "Best move: " << best.to_string() << "\n";

        // Show all moves with their evaluations
        std::cout << "\nAll moves:\n";
        auto moves = bobail::generate_moves(s);
        for (const auto& m : moves) {
            bobail::State next = bobail::apply_move(s, m);
            bobail::Result r = solver.get_result(next);

            std::cout << "  " << m.to_string() << " -> ";
            // Result is from opponent's perspective after the move
            switch (r) {
                case bobail::Result::WIN: std::cout << "LOSS"; break;  // Opponent wins = we lose
                case bobail::Result::LOSS: std::cout << "WIN"; break;  // Opponent loses = we win
                case bobail::Result::DRAW: std::cout << "DRAW"; break;
                default: std::cout << "?"; break;
            }
            if (m == best) std::cout << " *";
            std::cout << "\n";
        }
    }
}

bool parse_position(const std::string& input, bobail::State& s) {
    // Format: WP,BP,BOB,STM (hex,hex,int,int)
    // Example: 1f,1f00000,12,1
    std::istringstream iss(input);
    std::string wp_str, bp_str, bob_str, stm_str;

    if (!std::getline(iss, wp_str, ',')) return false;
    if (!std::getline(iss, bp_str, ',')) return false;
    if (!std::getline(iss, bob_str, ',')) return false;
    if (!std::getline(iss, stm_str, ',')) return false;

    try {
        s.white_pawns = std::stoul(wp_str, nullptr, 16);
        s.black_pawns = std::stoul(bp_str, nullptr, 16);
        s.bobail_sq = std::stoi(bob_str);
        s.white_to_move = (std::stoi(stm_str) != 0);
    } catch (...) {
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    std::string db_path;
    bool interactive = false;
    std::string query;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "--db") == 0) {
            if (i + 1 < argc) db_path = argv[++i];
        } else if (std::strcmp(argv[i], "--official") == 0) {
            bobail::g_rules_variant = bobail::RulesVariant::OFFICIAL;
        } else if (std::strcmp(argv[i], "--flexible") == 0) {
            bobail::g_rules_variant = bobail::RulesVariant::FLEXIBLE;
        } else if (std::strcmp(argv[i], "--interactive") == 0) {
            interactive = true;
        } else if (std::strcmp(argv[i], "--query") == 0) {
            if (i + 1 < argc) query = argv[++i];
        }
    }

    if (db_path.empty()) {
        std::cerr << "Error: --db is required\n";
        print_usage(argv[0]);
        return 1;
    }

    // Initialize
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    // Open database
    bobail::RetrogradeSolverDB solver;
    if (!solver.open(db_path)) {
        std::cerr << "Failed to open database: " << db_path << "\n";
        return 1;
    }

    std::cerr << "Database opened. Rules: "
              << (bobail::g_rules_variant == bobail::RulesVariant::OFFICIAL ? "OFFICIAL" : "FLEXIBLE")
              << "\n";

    if (!query.empty()) {
        // Single query mode
        bobail::State s;
        if (parse_position(query, s)) {
            print_position_info(solver, s);
        } else {
            std::cerr << "Invalid position format. Use: WP,BP,BOB,STM (hex,hex,int,int)\n";
            return 1;
        }
    } else if (interactive) {
        // Interactive mode
        std::cout << "Interactive lookup mode. Enter positions as: WP,BP,BOB,STM\n";
        std::cout << "Example: 1f,1f00000,12,1 (starting position)\n";
        std::cout << "Or 'start' for starting position, 'quit' to exit\n\n";

        std::string line;
        while (std::cout << "> " && std::getline(std::cin, line)) {
            if (line == "quit" || line == "q") break;

            bobail::State s;
            if (line == "start" || line == "s") {
                s = bobail::State::starting_position();
            } else if (!parse_position(line, s)) {
                std::cout << "Invalid format. Use: WP,BP,BOB,STM or 'start'\n";
                continue;
            }

            print_position_info(solver, s);
            std::cout << "\n";
        }
    } else {
        // Default: show starting position
        auto s = bobail::State::starting_position();
        print_position_info(solver, s);
    }

    solver.close();
    return 0;
}
