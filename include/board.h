#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <vector>

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

// Orthogonal directions only (for Bobail movement)
constexpr std::array<Direction, 4> ORTHOGONAL_DIRECTIONS = {
    NORTH, SOUTH, EAST, WEST
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
    BLACK_WINS,
    DRAW
};

GameResult check_terminal(const State& s);

// Game history for tracking repetitions
class GameHistory {
public:
    GameHistory() = default;

    // Add a position to history (call after each move)
    void push(const State& s);
    void push(uint64_t packed_state);

    // Remove last position (for undoing moves)
    void pop();

    // Check if current position would be a 3-fold repetition
    // Call BEFORE pushing the new position
    bool is_threefold_repetition(const State& s) const;
    bool is_threefold_repetition(uint64_t packed_state) const;

    // Get count of how many times a position has occurred
    int count(const State& s) const;
    int count(uint64_t packed_state) const;

    // Clear history
    void clear();

    // Get number of positions in history
    size_t size() const { return history_.size(); }

private:
    std::vector<uint64_t> history_;
};

// Check terminal with repetition detection
GameResult check_terminal_with_history(const State& s, const GameHistory& history);

} // namespace bobail
