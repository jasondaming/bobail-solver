#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <iostream>

int main() {
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

    // Generate and count moves
    auto moves = bobail::generate_moves(state);
    std::cout << "Legal moves from start: " << moves.size() << "\n\n";

    // Show first few moves
    std::cout << "First 5 moves:\n";
    for (size_t i = 0; i < std::min(moves.size(), size_t(5)); ++i) {
        std::cout << "  " << moves[i].to_string() << "\n";
    }

    // Test symmetry reduction
    auto [canonical, sym] = bobail::canonicalize(state);
    std::cout << "\nCanonical form uses symmetry " << sym << "\n";
    std::cout << "Hash: " << std::hex << bobail::compute_hash(state) << std::dec << "\n";
    std::cout << "Canonical hash: " << std::hex << bobail::canonical_hash(state) << std::dec << "\n";

    return 0;
}
