#pragma once

#include "board.h"
#include <vector>
#include <array>

namespace bobail {

// Game variant: affects pawn movement rules
enum class RulesVariant {
    FLEXIBLE,  // Pawns can stop anywhere along the ray (original solver)
    OFFICIAL   // Pawns must move to furthest unoccupied square (BGA rules)
};

// Current rules variant (set at startup)
extern RulesVariant g_rules_variant;

// A move consists of:
// 1. Bobail move (destination square)
// 2. Pawn move (from square, to square)
struct Move {
    uint8_t bobail_to;    // Where Bobail moves (1 step)
    uint8_t pawn_from;    // Which pawn moves
    uint8_t pawn_to;      // Where pawn slides to

    bool operator==(const Move& other) const;
    std::string to_string() const;
};

// Precomputed ray tables for sliding moves
// rays[sq][dir] = ordered list of squares in that direction from sq
extern std::array<std::array<std::vector<int>, 8>, NUM_SQUARES> rays;

// Precomputed neighbor tables for Bobail (1-step moves)
// neighbors[sq] = list of adjacent squares
extern std::array<std::vector<int>, NUM_SQUARES> neighbors;

// Initialize the precomputed tables (call once at startup)
void init_move_tables();

// Generate all legal Bobail moves from current position
// Returns list of destination squares
std::vector<int> generate_bobail_moves(const State& s);

// Generate all legal pawn moves for a given piece set and occupied mask
// Returns list of (from, to) pairs
std::vector<std::pair<int, int>> generate_pawn_moves(uint32_t pawns, uint32_t occupied);

// Generate all complete moves (Bobail + pawn) from position
// This is the main move generation function
std::vector<Move> generate_moves(const State& s);

// Apply a move to a state, returning the new state
State apply_move(const State& s, const Move& m);

// Check if a move is legal (for validation)
bool is_legal_move(const State& s, const Move& m);

// Count legal moves (for perft)
size_t count_moves(const State& s);

} // namespace bobail
