#include "symmetry.h"
#include "hash.h"

namespace bobail {

std::array<std::array<int, NUM_SQUARES>, NUM_SYMMETRIES> symmetry_map;

namespace {
    // Apply a single symmetry operation to coordinates
    // Returns (new_row, new_col) given (row, col)
    std::pair<int, int> apply_sym_coords(int r, int c, int sym) {
        // Symmetry operations on a 5x5 board (indices 0-4)
        // Center is at (2, 2)
        switch (sym) {
            case 0:  // Identity
                return {r, c};
            case 1:  // Rotate 90 CW
                return {c, BOARD_SIZE - 1 - r};
            case 2:  // Rotate 180
                return {BOARD_SIZE - 1 - r, BOARD_SIZE - 1 - c};
            case 3:  // Rotate 270 CW (= 90 CCW)
                return {BOARD_SIZE - 1 - c, r};
            case 4:  // Reflect horizontal (flip left-right)
                return {r, BOARD_SIZE - 1 - c};
            case 5:  // Reflect horizontal + rotate 90
                return {BOARD_SIZE - 1 - c, BOARD_SIZE - 1 - r};
            case 6:  // Reflect horizontal + rotate 180 (= vertical flip)
                return {BOARD_SIZE - 1 - r, c};
            case 7:  // Reflect horizontal + rotate 270
                return {c, r};
            default:
                return {r, c};
        }
    }
}

void init_symmetry() {
    for (int sym = 0; sym < NUM_SYMMETRIES; ++sym) {
        for (int sq = 0; sq < NUM_SQUARES; ++sq) {
            int r = State::row(sq);
            int c = State::col(sq);
            auto [nr, nc] = apply_sym_coords(r, c, sym);
            symmetry_map[sym][sq] = State::square(nr, nc);
        }
    }
}

uint32_t transform_bitboard(uint32_t bb, int sym) {
    uint32_t result = 0;
    while (bb) {
        int sq = __builtin_ctz(bb);
        bb &= bb - 1;
        result |= 1u << symmetry_map[sym][sq];
    }
    return result;
}

State apply_symmetry(const State& s, int sym) {
    State ns;
    ns.white_pawns = transform_bitboard(s.white_pawns, sym);
    ns.black_pawns = transform_bitboard(s.black_pawns, sym);
    ns.bobail_sq = symmetry_map[sym][s.bobail_sq];
    ns.white_to_move = s.white_to_move;
    return ns;
}

std::pair<State, int> canonicalize(const State& s) {
    State best = s;
    int best_sym = 0;
    uint64_t best_packed = pack_state(s);

    for (int sym = 1; sym < NUM_SYMMETRIES; ++sym) {
        State transformed = apply_symmetry(s, sym);
        uint64_t packed = pack_state(transformed);

        if (packed < best_packed) {
            best = transformed;
            best_sym = sym;
            best_packed = packed;
        }
    }

    return {best, best_sym};
}

uint64_t canonical_hash(const State& s) {
    auto [canonical, _] = canonicalize(s);
    return compute_hash(canonical);
}

} // namespace bobail
