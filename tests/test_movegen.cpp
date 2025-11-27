#include "movegen.h"
#include <gtest/gtest.h>
#include <algorithm>

using namespace bobail;

class MoveGenTest : public ::testing::Test {
protected:
    void SetUp() override {
        init_move_tables();
    }
};

TEST_F(MoveGenTest, TablesInitialized) {
    // Center square should have 8 neighbors
    EXPECT_EQ(neighbors[12].size(), 8);

    // Corner squares should have 3 neighbors
    EXPECT_EQ(neighbors[0].size(), 3);
    EXPECT_EQ(neighbors[4].size(), 3);
    EXPECT_EQ(neighbors[20].size(), 3);
    EXPECT_EQ(neighbors[24].size(), 3);

    // Edge squares should have 5 neighbors
    EXPECT_EQ(neighbors[2].size(), 5);
    EXPECT_EQ(neighbors[10].size(), 5);
}

TEST_F(MoveGenTest, BobailMoves) {
    State s = State::starting_position();
    auto moves = generate_bobail_moves(s);

    // From center (12), Bobail should be able to move to 8 adjacent squares
    // But row 0 and row 4 are occupied by pawns
    // Adjacent: 6,7,8,11,13,16,17,18
    // Free: 6,7,8,11,13,16,17,18 (all are on rows 1-3)
    EXPECT_EQ(moves.size(), 8);
}

TEST_F(MoveGenTest, PawnSliding) {
    // Test that pawns slide correctly
    uint32_t single_pawn = 1u << 12;  // Center square
    uint32_t occupied = single_pawn;

    auto moves = generate_pawn_moves(single_pawn, occupied);

    // From center with no blockers, should reach edge in all 8 directions
    // N: 7,2 (2 squares)
    // S: 17,22 (2 squares)
    // E: 13,14 (2 squares)
    // W: 11,10 (2 squares)
    // NE: 8,4 (2 squares)
    // NW: 6,0 (2 squares)
    // SE: 18,24 (2 squares)
    // SW: 16,20 (2 squares)
    // Total: 16 moves
    EXPECT_EQ(moves.size(), 16);
}

TEST_F(MoveGenTest, StartingMoveCount) {
    State s = State::starting_position();
    auto moves = generate_moves(s);

    // Bobail has 8 moves from center
    // For each Bobail move, White's 5 pawns can slide in various directions
    // This is a complex calculation - just verify it's reasonable
    EXPECT_GT(moves.size(), 50);
    EXPECT_LT(moves.size(), 500);
}

TEST_F(MoveGenTest, ApplyMove) {
    State s = State::starting_position();
    auto moves = generate_moves(s);

    ASSERT_FALSE(moves.empty());

    State ns = apply_move(s, moves[0]);

    // Side should switch
    EXPECT_FALSE(ns.white_to_move);

    // Bobail should have moved
    EXPECT_NE(ns.bobail_sq, s.bobail_sq);
    EXPECT_EQ(ns.bobail_sq, moves[0].bobail_to);
}

TEST_F(MoveGenTest, MoveEquality) {
    Move m1{12, 0, 5};
    Move m2{12, 0, 5};
    Move m3{12, 0, 6};

    EXPECT_EQ(m1, m2);
    EXPECT_FALSE(m1 == m3);
}
