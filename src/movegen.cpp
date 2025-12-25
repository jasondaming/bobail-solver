#include "movegen.h"
#include <bit>

namespace bobail {

// Default to Official rules (max distance) - can be changed at startup
RulesVariant g_rules_variant = RulesVariant::OFFICIAL;

std::array<std::array<std::vector<int>, 8>, NUM_SQUARES> rays;
std::array<std::vector<int>, NUM_SQUARES> neighbors;

namespace {
    // Direction index mapping
    constexpr int dir_to_index(Direction d) {
        switch (d) {
            case NORTH: return 0;
            case SOUTH: return 1;
            case EAST: return 2;
            case WEST: return 3;
            case NORTH_EAST: return 4;
            case NORTH_WEST: return 5;
            case SOUTH_EAST: return 6;
            case SOUTH_WEST: return 7;
        }
        return -1;
    }

    // Check if moving in direction stays on board
    bool can_move(int sq, Direction d) {
        int r = State::row(sq);
        int c = State::col(sq);

        switch (d) {
            case NORTH: return r > 0;
            case SOUTH: return r < BOARD_SIZE - 1;
            case EAST: return c < BOARD_SIZE - 1;
            case WEST: return c > 0;
            case NORTH_EAST: return r > 0 && c < BOARD_SIZE - 1;
            case NORTH_WEST: return r > 0 && c > 0;
            case SOUTH_EAST: return r < BOARD_SIZE - 1 && c < BOARD_SIZE - 1;
            case SOUTH_WEST: return r < BOARD_SIZE - 1 && c > 0;
        }
        return false;
    }
}

void init_move_tables() {
    // Build ray tables for sliding pieces
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        for (int di = 0; di < 8; ++di) {
            Direction d = ALL_DIRECTIONS[di];
            rays[sq][di].clear();

            int curr = sq;
            while (can_move(curr, d)) {
                curr += static_cast<int>(d);
                rays[sq][di].push_back(curr);
            }
        }

        // Build neighbor table (1-step moves for Bobail)
        neighbors[sq].clear();
        for (Direction d : ALL_DIRECTIONS) {
            if (can_move(sq, d)) {
                neighbors[sq].push_back(sq + static_cast<int>(d));
            }
        }
    }
}

std::vector<int> generate_bobail_moves(const State& s) {
    std::vector<int> moves;
    uint32_t occ = s.occupied();

    for (int dest : neighbors[s.bobail_sq]) {
        if (!(occ & (1u << dest))) {
            moves.push_back(dest);
        }
    }
    return moves;
}

std::vector<std::pair<int, int>> generate_pawn_moves(uint32_t pawns, uint32_t occupied) {
    std::vector<std::pair<int, int>> moves;

    uint32_t remaining = pawns;
    while (remaining) {
        int sq = std::countr_zero(remaining);
        remaining &= remaining - 1;

        for (int di = 0; di < 8; ++di) {
            if (g_rules_variant == RulesVariant::FLEXIBLE) {
                // Flexible: can stop at any square along the ray
                for (int dest : rays[sq][di]) {
                    if (occupied & (1u << dest)) {
                        break;  // Blocked
                    }
                    moves.emplace_back(sq, dest);
                }
            } else {
                // Official: must move to furthest unoccupied square
                int furthest = -1;
                for (int dest : rays[sq][di]) {
                    if (occupied & (1u << dest)) {
                        break;  // Blocked
                    }
                    furthest = dest;
                }
                if (furthest >= 0) {
                    moves.emplace_back(sq, furthest);
                }
            }
        }
    }
    return moves;
}

std::vector<Move> generate_moves(const State& s) {
    std::vector<Move> moves;

    // First generate all Bobail moves
    auto bobail_moves = generate_bobail_moves(s);

    if (bobail_moves.empty()) {
        return moves;  // No legal moves - current player loses
    }

    uint32_t our_pawns = s.white_to_move ? s.white_pawns : s.black_pawns;

    // For each Bobail move, generate all pawn moves
    for (int bobail_dest : bobail_moves) {
        // Update occupied mask for pawn move generation
        uint32_t new_occupied = (s.white_pawns | s.black_pawns | (1u << bobail_dest));

        auto pawn_moves = generate_pawn_moves(our_pawns, new_occupied);

        for (auto [from, to] : pawn_moves) {
            Move m;
            m.bobail_to = static_cast<uint8_t>(bobail_dest);
            m.pawn_from = static_cast<uint8_t>(from);
            m.pawn_to = static_cast<uint8_t>(to);
            moves.push_back(m);
        }
    }

    return moves;
}

State apply_move(const State& s, const Move& m) {
    State ns = s;

    // Move Bobail
    ns.bobail_sq = m.bobail_to;

    // Move pawn
    uint32_t from_bit = 1u << m.pawn_from;
    uint32_t to_bit = 1u << m.pawn_to;

    if (s.white_to_move) {
        ns.white_pawns = (ns.white_pawns & ~from_bit) | to_bit;
    } else {
        ns.black_pawns = (ns.black_pawns & ~from_bit) | to_bit;
    }

    // Switch side
    ns.white_to_move = !ns.white_to_move;

    return ns;
}

bool is_legal_move(const State& s, const Move& m) {
    auto legal = generate_moves(s);
    for (const auto& lm : legal) {
        if (lm == m) return true;
    }
    return false;
}

size_t count_moves(const State& s) {
    return generate_moves(s).size();
}

bool Move::operator==(const Move& other) const {
    return bobail_to == other.bobail_to &&
           pawn_from == other.pawn_from &&
           pawn_to == other.pawn_to;
}

std::string Move::to_string() const {
    std::string s;
    s += "B->" + std::to_string(bobail_to);
    s += " P:" + std::to_string(pawn_from) + "->" + std::to_string(pawn_to);
    return s;
}

} // namespace bobail
