#include "hash.h"
#include <bit>

namespace bobail {

std::array<std::array<uint64_t, 3>, NUM_SQUARES> zobrist_pieces;
uint64_t zobrist_side;

namespace {
    // Simple xorshift64 PRNG for deterministic Zobrist initialization
    uint64_t xorshift64(uint64_t& state) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
}

void init_zobrist(uint64_t seed) {
    uint64_t state = seed;

    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        for (int piece = 0; piece < 3; ++piece) {
            zobrist_pieces[sq][piece] = xorshift64(state);
        }
    }
    zobrist_side = xorshift64(state);
}

uint64_t compute_hash(const State& s) {
    uint64_t hash = 0;

    // White pawns
    uint32_t wp = s.white_pawns;
    while (wp) {
        int sq = std::countr_zero(wp);
        wp &= wp - 1;
        hash ^= zobrist_pieces[sq][0];  // piece type 0 = white pawn
    }

    // Black pawns
    uint32_t bp = s.black_pawns;
    while (bp) {
        int sq = std::countr_zero(bp);
        bp &= bp - 1;
        hash ^= zobrist_pieces[sq][1];  // piece type 1 = black pawn
    }

    // Bobail
    hash ^= zobrist_pieces[s.bobail_sq][2];  // piece type 2 = bobail

    // Side to move
    if (s.white_to_move) {
        hash ^= zobrist_side;
    }

    return hash;
}

uint64_t hash_toggle_pawn(uint64_t hash, int sq, bool is_white) {
    return hash ^ zobrist_pieces[sq][is_white ? 0 : 1];
}

uint64_t hash_toggle_bobail(uint64_t hash, int from, int to) {
    return hash ^ zobrist_pieces[from][2] ^ zobrist_pieces[to][2];
}

uint64_t hash_toggle_side(uint64_t hash) {
    return hash ^ zobrist_side;
}

} // namespace bobail
