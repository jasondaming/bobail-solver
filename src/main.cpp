#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include "pns.h"
#include <iostream>
#include <chrono>

int main(int argc, char* argv[]) {
    // Initialize tables
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    // Create starting position
    auto state = bobail::State::starting_position();
    std::cout << "Bobail Solver\n";
    std::cout << "=============\n\n";
    std::cout << "Starting position:\n";
    std::cout << state.to_string() << "\n";

    // Parse node limit from command line
    uint64_t node_limit = 0;
    if (argc > 1) {
        node_limit = std::stoull(argv[1]);
        std::cout << "Node limit: " << node_limit << "\n\n";
    }

    // Create solver with 1M entry TT for testing
    std::cout << "Allocating TT..." << std::flush;
    bobail::PNSSolver solver(1 << 20);
    std::cout << " done.\n" << std::flush;

    if (node_limit > 0) {
        solver.set_node_limit(node_limit);
    }

    // Progress callback
    solver.set_progress_callback([](uint64_t nodes, uint64_t proved, uint64_t disproved) {
        std::cout << "\rNodes: " << nodes
                  << " | Proved: " << proved
                  << " | Disproved: " << disproved << std::flush;
    });

    std::cout << "Starting proof-number search...\n" << std::flush;
    auto t0 = std::chrono::high_resolution_clock::now();

    bobail::Result result = solver.solve(state);

    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    std::cout << "\n\nSearch completed in " << ms << " ms\n";
    std::cout << "Nodes searched: " << solver.nodes_searched() << "\n";
    std::cout << "Nodes proved: " << solver.nodes_proved() << "\n";
    std::cout << "Nodes disproved: " << solver.nodes_disproved() << "\n";

    std::cout << "\nResult: ";
    switch (result) {
        case bobail::Result::WIN:
            std::cout << "WHITE WINS with perfect play\n";
            break;
        case bobail::Result::LOSS:
            std::cout << "BLACK WINS with perfect play\n";
            break;
        case bobail::Result::DRAW:
            std::cout << "DRAW with perfect play\n";
            break;
        case bobail::Result::UNKNOWN:
            std::cout << "UNKNOWN (search incomplete)\n";
            break;
    }

    // Show principal variation
    auto pv = solver.get_pv();
    if (!pv.empty()) {
        std::cout << "\nPrincipal variation (" << pv.size() << " moves):\n";
        auto pos = state;
        for (size_t i = 0; i < std::min(pv.size(), size_t(10)); ++i) {
            std::cout << (i + 1) << ". " << pv[i].to_string() << "\n";
            pos = bobail::apply_move(pos, pv[i]);
        }
        if (pv.size() > 10) {
            std::cout << "... (" << (pv.size() - 10) << " more moves)\n";
        }
    }

    return 0;
}
