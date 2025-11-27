#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace bobail {

// 5x5 board with squares numbered 0-24 (row-major)
// Row 0 is White's home row, Row 4 is Black's home row
constexpr int BOARD_SIZE = 5;
constexpr int NUM_SQUARES = 25;
constexpr int PAWNS_PER_SIDE = 5;

// Direction offsets for 8-directional movement
enum Direction : int {
    NORTH = -5,
    SOUTH = 5,
    EAST = 1,
    WEST = -1,
    NORTH_EAST = -4,
    NORTH_WEST = -6,
    SOUTH_EAST = 6,
    SOUTH_WEST = 4
};

constexpr std::array<Direction, 8> ALL_DIRECTIONS = {
    NORTH, SOUTH, EAST, WEST,
    NORTH_EAST, NORTH_WEST, SOUTH_EAST, SOUTH_WEST
};

// Game state representation
// Uses bitboards for efficient manipulation
struct State {
    uint32_t white_pawns;    // Bit i = 1 if white pawn on square i
    uint32_t black_pawns;    // Bit i = 1 if black pawn on square i
    uint8_t bobail_sq;       // 0-24, position of the Bobail
    bool white_to_move;      // True if White's turn

    // Create the standard starting position
    static State starting_position();

    // Check if position is valid
    bool is_valid() const;

    // Get all occupied squares as a bitboard
    uint32_t occupied() const;

    // Get row (0-4) and column (0-4) from square index
    static int row(int sq);
    static int col(int sq);
    static int square(int row, int col);

    // Check if a square is on the board
    static bool is_valid_square(int sq);

    // String representation for debugging
    std::string to_string() const;

    // Equality comparison
    bool operator==(const State& other) const;
};

// Compact 64-bit representation for hashing/storage
uint64_t pack_state(const State& s);
State unpack_state(uint64_t packed);

// Terminal state detection
enum class GameResult {
    ONGOING,
    WHITE_WINS,
    BLACK_WINS
};

GameResult check_terminal(const State& s);

} // namespace bobail
