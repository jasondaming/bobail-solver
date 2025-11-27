#include "board.h"
#include <gtest/gtest.h>

using namespace bobail;

TEST(BoardTest, StartingPosition) {
    State s = State::starting_position();

    EXPECT_TRUE(s.is_valid());
    EXPECT_TRUE(s.white_to_move);
    EXPECT_EQ(s.bobail_sq, 12);  // Center square

    // White pawns on row 0
    for (int c = 0; c < BOARD_SIZE; ++c) {
        int sq = State::square(0, c);
        EXPECT_TRUE(s.white_pawns & (1u << sq));
    }

    // Black pawns on row 4
    for (int c = 0; c < BOARD_SIZE; ++c) {
        int sq = State::square(4, c);
        EXPECT_TRUE(s.black_pawns & (1u << sq));
    }
}

TEST(BoardTest, RowCol) {
    EXPECT_EQ(State::row(0), 0);
    EXPECT_EQ(State::col(0), 0);

    EXPECT_EQ(State::row(12), 2);
    EXPECT_EQ(State::col(12), 2);

    EXPECT_EQ(State::row(24), 4);
    EXPECT_EQ(State::col(24), 4);

    EXPECT_EQ(State::square(2, 3), 13);
}

TEST(BoardTest, PackUnpack) {
    State s = State::starting_position();
    uint64_t packed = pack_state(s);
    State unpacked = unpack_state(packed);

    EXPECT_EQ(s, unpacked);
}

TEST(BoardTest, Terminal) {
    State s = State::starting_position();
    EXPECT_EQ(check_terminal(s), GameResult::ONGOING);

    // Bobail on row 0 = White wins
    s.bobail_sq = 2;
    EXPECT_EQ(check_terminal(s), GameResult::WHITE_WINS);

    // Bobail on row 4 = Black wins
    s.bobail_sq = 22;
    EXPECT_EQ(check_terminal(s), GameResult::BLACK_WINS);
}

TEST(BoardTest, Occupied) {
    State s = State::starting_position();
    uint32_t occ = s.occupied();

    // Should have 11 bits set (5 white + 5 black + 1 bobail)
    EXPECT_EQ(__builtin_popcount(occ), 11);
}
