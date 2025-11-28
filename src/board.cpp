#include "board.h"
#include <sstream>
#include <bit>

namespace bobail {

State State::starting_position() {
    State s;
    // White pawns on row 0 (squares 0-4)
    s.white_pawns = 0b00000'00000'00000'00000'11111;
    // Black pawns on row 4 (squares 20-24)
    s.black_pawns = 0b11111'00000'00000'00000'00000;
    // Bobail starts in center (square 12)
    s.bobail_sq = 12;
    // White moves first
    s.white_to_move = true;
    return s;
}

bool State::is_valid() const {
    // Check pawn counts
    if (std::popcount(white_pawns) != PAWNS_PER_SIDE) return false;
    if (std::popcount(black_pawns) != PAWNS_PER_SIDE) return false;

    // Check no overlap
    if (white_pawns & black_pawns) return false;

    // Check Bobail position
    if (bobail_sq >= NUM_SQUARES) return false;
    uint32_t bobail_bit = 1u << bobail_sq;
    if (white_pawns & bobail_bit) return false;
    if (black_pawns & bobail_bit) return false;

    return true;
}

uint32_t State::occupied() const {
    return white_pawns | black_pawns | (1u << bobail_sq);
}

int State::row(int sq) {
    return sq / BOARD_SIZE;
}

int State::col(int sq) {
    return sq % BOARD_SIZE;
}

int State::square(int row, int col) {
    return row * BOARD_SIZE + col;
}

bool State::is_valid_square(int sq) {
    return sq >= 0 && sq < NUM_SQUARES;
}

std::string State::to_string() const {
    std::ostringstream ss;
    ss << (white_to_move ? "White" : "Black") << " to move\n";
    ss << "  01234\n";
    for (int r = 0; r < BOARD_SIZE; ++r) {
        ss << r << " ";
        for (int c = 0; c < BOARD_SIZE; ++c) {
            int sq = square(r, c);
            if (sq == bobail_sq) {
                ss << 'B';
            } else if (white_pawns & (1u << sq)) {
                ss << 'W';
            } else if (black_pawns & (1u << sq)) {
                ss << 'X';
            } else {
                ss << '.';
            }
        }
        ss << "\n";
    }
    return ss.str();
}

bool State::operator==(const State& other) const {
    return white_pawns == other.white_pawns &&
           black_pawns == other.black_pawns &&
           bobail_sq == other.bobail_sq &&
           white_to_move == other.white_to_move;
}

uint64_t pack_state(const State& s) {
    // Pack into 64 bits:
    // bits 0-24: white pawns
    // bits 25-49: black pawns
    // bits 50-54: bobail square (5 bits)
    // bit 55: side to move
    uint64_t packed = s.white_pawns;
    packed |= static_cast<uint64_t>(s.black_pawns) << 25;
    packed |= static_cast<uint64_t>(s.bobail_sq) << 50;
    packed |= static_cast<uint64_t>(s.white_to_move ? 1 : 0) << 55;
    return packed;
}

State unpack_state(uint64_t packed) {
    State s;
    s.white_pawns = packed & 0x1FFFFFFu;
    s.black_pawns = (packed >> 25) & 0x1FFFFFFu;
    s.bobail_sq = (packed >> 50) & 0x1F;
    s.white_to_move = (packed >> 55) & 1;
    return s;
}

GameResult check_terminal(const State& s) {
    int bobail_row = State::row(s.bobail_sq);

    // Bobail on White's home row (row 0) = White wins
    if (bobail_row == 0) {
        return GameResult::WHITE_WINS;
    }

    // Bobail on Black's home row (row 4) = Black wins
    if (bobail_row == BOARD_SIZE - 1) {
        return GameResult::BLACK_WINS;
    }

    return GameResult::ONGOING;
}

// GameHistory implementation
void GameHistory::push(const State& s) {
    push(pack_state(s));
}

void GameHistory::push(uint64_t packed_state) {
    history_.push_back(packed_state);
}

void GameHistory::pop() {
    if (!history_.empty()) {
        history_.pop_back();
    }
}

bool GameHistory::is_threefold_repetition(const State& s) const {
    return is_threefold_repetition(pack_state(s));
}

bool GameHistory::is_threefold_repetition(uint64_t packed_state) const {
    // Count occurrences in history
    // If it already appears 2+ times, adding it again makes 3-fold
    return count(packed_state) >= 2;
}

int GameHistory::count(const State& s) const {
    return count(pack_state(s));
}

int GameHistory::count(uint64_t packed_state) const {
    int cnt = 0;
    for (uint64_t h : history_) {
        if (h == packed_state) {
            ++cnt;
        }
    }
    return cnt;
}

void GameHistory::clear() {
    history_.clear();
}

GameResult check_terminal_with_history(const State& s, const GameHistory& history) {
    // First check standard terminal conditions
    GameResult result = check_terminal(s);
    if (result != GameResult::ONGOING) {
        return result;
    }

    // Check for 3-fold repetition
    if (history.is_threefold_repetition(s)) {
        return GameResult::DRAW;
    }

    return GameResult::ONGOING;
}

} // namespace bobail
