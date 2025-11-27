#pragma once

#include "board.h"
#include <cstdint>
#include <array>

namespace bobail {

// Zobrist hashing for transposition tables
// Uses random 64-bit values XORed together based on piece positions

// Random values for each (square, piece-type) combination
// Piece types: 0 = white pawn, 1 = black pawn, 2 = bobail
extern std::array<std::array<uint64_t, 3>, NUM_SQUARES> zobrist_pieces;

// Random value for side to move
extern uint64_t zobrist_side;

// Initialize Zobrist tables with deterministic seed
void init_zobrist(uint64_t seed = 0x12345678ABCDEF00ULL);

// Compute full hash of a state
uint64_t compute_hash(const State& s);

// Incremental hash update functions
// Call these instead of recomputing when making/unmaking moves
uint64_t hash_toggle_pawn(uint64_t hash, int sq, bool is_white);
uint64_t hash_toggle_bobail(uint64_t hash, int from, int to);
uint64_t hash_toggle_side(uint64_t hash);

} // namespace bobail
