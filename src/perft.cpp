#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <iostream>
#include <chrono>
#include <unordered_set>

using namespace bobail;

// Perft: count positions at depth N
// Used to validate move generation
uint64_t perft(const State& s, int depth) {
    if (depth == 0) {
        return 1;
    }

    // Check for terminal
    GameResult result = check_terminal(s);
    if (result != GameResult::ONGOING) {
        return 0;  // No further moves from terminal
    }

    auto moves = generate_moves(s);
    if (moves.empty()) {
        return 0;  // Player has no moves (loss)
    }

    if (depth == 1) {
        return moves.size();
    }

    uint64_t count = 0;
    for (const auto& m : moves) {
        State ns = apply_move(s, m);
        count += perft(ns, depth - 1);
    }
    return count;
}

// Divide: show perft for each first move
void divide(const State& s, int depth) {
    auto moves = generate_moves(s);
    uint64_t total = 0;

    std::cout << "Divide at depth " << depth << ":\n";
    for (const auto& m : moves) {
        State ns = apply_move(s, m);
        uint64_t count = perft(ns, depth - 1);
        std::cout << "  " << m.to_string() << ": " << count << "\n";
        total += count;
    }
    std::cout << "Total: " << total << "\n";
}

// Count unique canonical positions at depth
uint64_t unique_positions(const State& s, int depth, std::unordered_set<uint64_t>& seen) {
    if (depth == 0) {
        auto [canonical, _] = canonicalize(s);
        uint64_t key = pack_state(canonical);
        if (seen.insert(key).second) {
            return 1;
        }
        return 0;
    }

    GameResult result = check_terminal(s);
    if (result != GameResult::ONGOING) {
        return 0;
    }

    auto moves = generate_moves(s);
    if (moves.empty()) {
        return 0;
    }

    uint64_t count = 0;
    for (const auto& m : moves) {
        State ns = apply_move(s, m);
        count += unique_positions(ns, depth - 1, seen);
    }
    return count;
}

int main(int argc, char* argv[]) {
    int max_depth = 4;
    if (argc > 1) {
        max_depth = std::stoi(argv[1]);
    }

    // Initialize
    init_move_tables();
    init_zobrist();
    init_symmetry();

    State start = State::starting_position();
    std::cout << "Bobail Perft\n";
    std::cout << "============\n\n";
    std::cout << start.to_string() << "\n";

    // Run perft for each depth
    for (int d = 0; d <= max_depth; ++d) {
        auto t0 = std::chrono::high_resolution_clock::now();
        uint64_t count = perft(start, d);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        std::cout << "perft(" << d << ") = " << count;
        if (ms > 0) {
            std::cout << " (" << ms << " ms, " << (count * 1000 / ms) << " nodes/s)";
        }
        std::cout << "\n";
    }

    // Show unique positions with symmetry reduction
    std::cout << "\nUnique canonical positions:\n";
    for (int d = 0; d <= std::min(max_depth, 3); ++d) {
        std::unordered_set<uint64_t> seen;
        unique_positions(start, d, seen);
        std::cout << "depth " << d << ": " << seen.size() << " unique positions\n";
    }

    return 0;
}
