#pragma once

#include "board.h"
#include <array>
#include <cstdint>

namespace bobail {

// The 5x5 board has D4 symmetry (dihedral group of the square)
// 8 symmetries: 4 rotations x 2 (with/without reflection)
//
// Symmetry indices:
// 0: identity
// 1: rotate 90 CW
// 2: rotate 180
// 3: rotate 270 CW
// 4: reflect horizontal
// 5: reflect + rotate 90
// 6: reflect + rotate 180
// 7: reflect + rotate 270

constexpr int NUM_SYMMETRIES = 8;

// Precomputed square mappings for each symmetry
// symmetry_map[sym][sq] = where square sq maps to under symmetry sym
extern std::array<std::array<int, NUM_SQUARES>, NUM_SYMMETRIES> symmetry_map;

// Initialize symmetry tables
void init_symmetry();

// Apply a symmetry transformation to a state
State apply_symmetry(const State& s, int sym);

// Apply a symmetry transformation to a bitboard
uint32_t transform_bitboard(uint32_t bb, int sym);

// Find the canonical form of a state (lexicographically smallest under all symmetries)
// Returns both the canonical state and which symmetry was applied
std::pair<State, int> canonicalize(const State& s);

// Get canonical hash (hash of canonical form)
uint64_t canonical_hash(const State& s);

// Transform a move under a symmetry (for mapping moves back from canonical form)
// Given a move in canonical space and the symmetry that was applied,
// return the equivalent move in the original space
// inverse_sym is the inverse of the symmetry used for canonicalization

} // namespace bobail
