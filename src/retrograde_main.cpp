#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include "retrograde.h"
#include <iostream>
#include <chrono>
#include <iomanip>

int main() {
    // Initialize tables
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    std::cout << "Bobail Strong Solver (Retrograde Analysis)\n";
    std::cout << "==========================================\n\n";

    // Show starting position
    auto start = bobail::State::starting_position();
    std::cout << "Starting position:\n";
    std::cout << start.to_string() << "\n";

    // Create solver
    bobail::RetrogradeSolver solver;

    // Progress callback
    solver.set_progress_callback([](const char* phase, uint64_t current, uint64_t total) {
        if (total > 0) {
            double pct = 100.0 * current / total;
            std::cout << "\r" << phase << ": " << current << " / " << total
                      << " (" << std::fixed << std::setprecision(1) << pct << "%)" << std::flush;
        } else {
            std::cout << "\r" << phase << ": " << current << " states" << std::flush;
        }
    });

    std::cout << "Starting retrograde analysis...\n\n";
    auto t0 = std::chrono::high_resolution_clock::now();

    solver.solve();

    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    std::cout << "\n\n";
    std::cout << "========================================\n";
    std::cout << "SOLUTION COMPLETE\n";
    std::cout << "========================================\n\n";

    std::cout << "Time: " << ms / 1000.0 << " seconds\n";
    std::cout << "Total states: " << solver.num_states() << "\n";
    std::cout << "Wins:   " << solver.num_wins() << "\n";
    std::cout << "Losses: " << solver.num_losses() << "\n";
    std::cout << "Draws:  " << solver.num_draws() << "\n\n";

    // Show result
    bobail::Result result = solver.starting_result();
    std::cout << "STARTING POSITION RESULT: ";
    switch (result) {
        case bobail::Result::WIN:
            std::cout << "WHITE WINS with perfect play!\n";
            break;
        case bobail::Result::LOSS:
            std::cout << "BLACK WINS with perfect play!\n";
            break;
        case bobail::Result::DRAW:
            std::cout << "DRAW with perfect play!\n";
            break;
        default:
            std::cout << "UNKNOWN\n";
            break;
    }

    // Show optimal opening moves
    std::cout << "\nOptimal play from start:\n";
    auto state = start;
    for (int ply = 0; ply < 20; ++ply) {
        bobail::Result r = solver.get_result(state);
        bobail::Move best = solver.get_best_move(state);

        std::cout << (ply + 1) << ". ";
        if (state.white_to_move) std::cout << "White: ";
        else std::cout << "Black: ";

        std::cout << best.to_string();
        std::cout << " (";
        switch (r) {
            case bobail::Result::WIN: std::cout << "WIN"; break;
            case bobail::Result::LOSS: std::cout << "LOSS"; break;
            case bobail::Result::DRAW: std::cout << "DRAW"; break;
            default: std::cout << "?"; break;
        }
        std::cout << ")\n";

        state = bobail::apply_move(state, best);

        // Check if game over
        if (bobail::check_terminal(state) != bobail::GameResult::ONGOING) {
            std::cout << "\nGame over!\n";
            std::cout << state.to_string();
            break;
        }

        auto moves = bobail::generate_moves(state);
        if (moves.empty()) {
            std::cout << "\nNo moves - game over!\n";
            break;
        }
    }

    return 0;
}
