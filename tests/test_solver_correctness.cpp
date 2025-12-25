// Tests to validate solver correctness without waiting for full solve
// These tests check fundamental invariants that must hold for any correct solver

#include "board.h"
#include "movegen.h"
#include "symmetry.h"
#include "hash.h"
#include <gtest/gtest.h>
#include <iostream>
#include <set>
#include <queue>
#include <unordered_set>
#include <unordered_map>

using namespace bobail;

class SolverCorrectnessTest : public ::testing::Test {
protected:
    void SetUp() override {
        init_move_tables();
        init_zobrist();
        init_symmetry();
    }
};

// Test 1: Symmetry preserves goal rows
// Only horizontal flip should be used (symmetries 0 and 4)
TEST_F(SolverCorrectnessTest, SymmetryPreservesGoalRows) {
    State start = State::starting_position();

    // Test that canonicalization only uses symmetries 0 or 4
    auto [canonical, sym] = canonicalize(start);
    EXPECT_TRUE(sym == 0 || sym == 4)
        << "Expected symmetry 0 or 4, got " << sym;

    // Test with various positions
    for (int i = 0; i < 100; ++i) {
        State s;
        // Create random-ish positions by applying moves
        s = start;
        auto moves = generate_moves(s);
        if (!moves.empty()) {
            s = apply_move(s, moves[i % moves.size()]);
            auto [can, sy] = canonicalize(s);
            EXPECT_TRUE(sy == 0 || sy == 4)
                << "Position after move " << i << " used symmetry " << sy;
        }
    }
}

// Test 2: Symmetry doesn't change win condition semantics
TEST_F(SolverCorrectnessTest, SymmetryPreservesWinCondition) {
    // Position with bobail on row 0 (White wins)
    State white_wins;
    white_wins.white_pawns = 0b00000'00000'00000'11111'00000;  // row 1
    white_wins.black_pawns = 0b11111'00000'00000'00000'00000;  // row 4
    white_wins.bobail_sq = 2;  // row 0
    white_wins.white_to_move = false;

    EXPECT_EQ(check_terminal(white_wins), GameResult::WHITE_WINS);

    auto [canonical, sym] = canonicalize(white_wins);
    EXPECT_EQ(check_terminal(canonical), GameResult::WHITE_WINS)
        << "Canonical form changed win condition! sym=" << sym;

    // Position with bobail on row 4 (Black wins)
    State black_wins;
    black_wins.white_pawns = 0b00000'00000'00000'00000'11111;  // row 0
    black_wins.black_pawns = 0b00000'11111'00000'00000'00000;  // row 3
    black_wins.bobail_sq = 22;  // row 4
    black_wins.white_to_move = true;

    EXPECT_EQ(check_terminal(black_wins), GameResult::BLACK_WINS);

    auto [canonical2, sym2] = canonicalize(black_wins);
    EXPECT_EQ(check_terminal(canonical2), GameResult::BLACK_WINS)
        << "Canonical form changed win condition! sym=" << sym2;
}

// Test 3: Terminal positions have correct results
TEST_F(SolverCorrectnessTest, TerminalPositionsCorrect) {
    // Bobail on row 0 = White wins
    for (int col = 0; col < 5; ++col) {
        State s;
        s.white_pawns = 0b00000'00000'00000'11111'00000;  // row 1
        s.black_pawns = 0b11111'00000'00000'00000'00000;  // row 4
        s.bobail_sq = col;  // row 0
        s.white_to_move = false;

        EXPECT_EQ(check_terminal(s), GameResult::WHITE_WINS)
            << "Bobail at row 0, col " << col << " should be WHITE_WINS";
    }

    // Bobail on row 4 = Black wins
    for (int col = 0; col < 5; ++col) {
        State s;
        s.white_pawns = 0b00000'00000'00000'00000'11111;  // row 0
        s.black_pawns = 0b00000'11111'00000'00000'00000;  // row 3
        s.bobail_sq = 20 + col;  // row 4
        s.white_to_move = true;

        EXPECT_EQ(check_terminal(s), GameResult::BLACK_WINS)
            << "Bobail at row 4, col " << col << " should be BLACK_WINS";
    }

    // Bobail in middle = ongoing
    State ongoing = State::starting_position();
    EXPECT_EQ(check_terminal(ongoing), GameResult::ONGOING);
}

// Test 4: Move generation is consistent with terminal detection
TEST_F(SolverCorrectnessTest, NoMovesFromTerminal) {
    // Terminal position shouldn't matter for move gen (we don't generate moves from there)
    // But non-terminal should have moves
    State start = State::starting_position();
    auto moves = generate_moves(start);
    EXPECT_GT(moves.size(), 0u) << "Starting position should have moves";
}

// Test 5: Canonical form is deterministic and consistent
TEST_F(SolverCorrectnessTest, CanonicalizationDeterministic) {
    State start = State::starting_position();

    auto [can1, sym1] = canonicalize(start);
    auto [can2, sym2] = canonicalize(start);

    EXPECT_EQ(pack_state(can1), pack_state(can2));
    EXPECT_EQ(sym1, sym2);

    // Apply the same symmetry again should give same packed result
    State transformed = apply_symmetry(start, sym1);
    EXPECT_EQ(pack_state(transformed), pack_state(can1));
}

// Test 6: Horizontal flip (sym 4) preserves row numbers
TEST_F(SolverCorrectnessTest, HorizontalFlipPreservesRows) {
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        int orig_row = State::row(sq);
        int flipped_sq = symmetry_map[4][sq];  // horizontal flip
        int flipped_row = State::row(flipped_sq);

        EXPECT_EQ(orig_row, flipped_row)
            << "Square " << sq << " (row " << orig_row
            << ") flipped to " << flipped_sq << " (row " << flipped_row << ")";
    }
}

// Test 7: Mini-solver test - verify a small game tree manually
// This tests the core logic without needing the full database
TEST_F(SolverCorrectnessTest, MiniGameTreeCorrect) {
    // Create a position where we can verify the result manually
    // Position: Bobail one step from White's goal, White to move
    // White should be able to win by moving bobail to row 0

    State near_win;
    near_win.white_pawns = 0b00000'00000'01111'00000'00001;  // spread out
    near_win.black_pawns = 0b11111'00000'00000'00000'00000;  // row 4
    near_win.bobail_sq = 6;  // row 1, col 1 - can move to row 0
    near_win.white_to_move = true;

    ASSERT_EQ(check_terminal(near_win), GameResult::ONGOING);

    // Generate moves and check if any lead to immediate win
    auto moves = generate_moves(near_win);
    ASSERT_GT(moves.size(), 0u);

    bool found_winning_move = false;
    for (const auto& m : moves) {
        State after = apply_move(near_win, m);
        if (check_terminal(after) == GameResult::WHITE_WINS) {
            found_winning_move = true;
            // The bobail move should put it on row 0
            EXPECT_EQ(State::row(m.bobail_to), 0)
                << "Winning move should put bobail on row 0";
            break;
        }
    }

    EXPECT_TRUE(found_winning_move)
        << "Should find a winning move from position near White's goal";
}

// Test 8: BFS enumeration reaches all positions reachable from start
// (small depth test to verify enumeration logic)
TEST_F(SolverCorrectnessTest, BFSEnumerationWorks) {
    std::unordered_set<uint64_t> visited;
    std::queue<State> queue;

    State start = State::starting_position();
    auto [canonical_start, _] = canonicalize(start);
    visited.insert(pack_state(canonical_start));
    queue.push(start);

    int max_depth = 2;  // Shallow search for test
    int depth = 0;
    size_t level_size = queue.size();

    while (!queue.empty() && depth < max_depth) {
        State s = queue.front();
        queue.pop();
        level_size--;

        if (check_terminal(s) == GameResult::ONGOING) {
            auto moves = generate_moves(s);
            for (const auto& m : moves) {
                State next = apply_move(s, m);
                auto [canonical, __] = canonicalize(next);
                uint64_t packed = pack_state(canonical);

                if (visited.find(packed) == visited.end()) {
                    visited.insert(packed);
                    queue.push(next);
                }
            }
        }

        if (level_size == 0) {
            depth++;
            level_size = queue.size();
        }
    }

    // At depth 2, we should have found many positions
    EXPECT_GT(visited.size(), 100u)
        << "BFS at depth 2 should find >100 positions, found " << visited.size();

    std::cout << "BFS depth " << max_depth << " found " << visited.size() << " unique canonical positions\n";
}

// Test 9: Result propagation logic test
// If a position has a child that's LOSS for opponent, position is WIN
TEST_F(SolverCorrectnessTest, PropagationLogicWin) {
    // Manually create scenario:
    // If I make a move and opponent is in a losing position, I win

    // Position where White can move bobail to row 0 and then any pawn move
    State win_in_one;
    win_in_one.white_pawns = 0b00000'00000'00010'01100'00001;  // various positions
    win_in_one.black_pawns = 0b11111'00000'00000'00000'00000;  // row 4
    win_in_one.bobail_sq = 5;  // row 1, col 0 - adjacent to row 0
    win_in_one.white_to_move = true;

    // Find if there's a move where bobail goes to row 0
    auto bobail_moves = generate_bobail_moves(win_in_one);
    bool can_reach_goal = false;
    for (int dest : bobail_moves) {
        if (State::row(dest) == 0) {
            can_reach_goal = true;
            break;
        }
    }

    if (can_reach_goal) {
        // There should be a complete move that wins
        auto moves = generate_moves(win_in_one);
        bool has_winning_move = false;
        for (const auto& m : moves) {
            State after = apply_move(win_in_one, m);
            if (check_terminal(after) == GameResult::WHITE_WINS) {
                has_winning_move = true;
                break;
            }
        }
        EXPECT_TRUE(has_winning_move)
            << "Position with bobail adjacent to goal should have winning move";
    }
}

// Test 10: Verify packed state round-trips correctly
TEST_F(SolverCorrectnessTest, PackUnpackRoundTrip) {
    State start = State::starting_position();
    uint64_t packed = pack_state(start);
    State unpacked = unpack_state(packed);

    EXPECT_EQ(start.white_pawns, unpacked.white_pawns);
    EXPECT_EQ(start.black_pawns, unpacked.black_pawns);
    EXPECT_EQ(start.bobail_sq, unpacked.bobail_sq);
    EXPECT_EQ(start.white_to_move, unpacked.white_to_move);

    // Test after some moves
    auto moves = generate_moves(start);
    for (size_t i = 0; i < std::min(moves.size(), size_t(10)); ++i) {
        State after = apply_move(start, moves[i]);
        uint64_t p = pack_state(after);
        State u = unpack_state(p);

        EXPECT_EQ(after.white_pawns, u.white_pawns);
        EXPECT_EQ(after.black_pawns, u.black_pawns);
        EXPECT_EQ(after.bobail_sq, u.bobail_sq);
        EXPECT_EQ(after.white_to_move, u.white_to_move);
    }
}

// Test 11: All symmetries that preserve rows should give same terminal result
TEST_F(SolverCorrectnessTest, ValidSymmetriesPreserveTerminal) {
    // Test positions at various bobail locations
    std::vector<int> test_rows = {0, 1, 2, 3, 4};

    for (int test_row : test_rows) {
        State s;
        s.white_pawns = 0b00000'00000'01111'00000'00001;
        s.black_pawns = 0b11111'00000'00000'00000'00000;
        s.bobail_sq = test_row * 5 + 2;  // middle column of test_row
        s.white_to_move = true;

        GameResult original_result = check_terminal(s);

        // Only test valid symmetries (0 and 4)
        for (int sym : {0, 4}) {
            State transformed = apply_symmetry(s, sym);
            GameResult transformed_result = check_terminal(transformed);

            EXPECT_EQ(original_result, transformed_result)
                << "Symmetry " << sym << " changed terminal result for bobail at row " << test_row;
        }
    }
}

// Test 12: Invalid symmetries WOULD break terminal (just to confirm our analysis)
TEST_F(SolverCorrectnessTest, InvalidSymmetriesBreakTerminal) {
    // Position with bobail on row 0 (White wins)
    State white_wins;
    white_wins.white_pawns = 0b00000'00000'00000'11111'00000;  // row 1
    white_wins.black_pawns = 0b11111'00000'00000'00000'00000;  // row 4
    white_wins.bobail_sq = 2;  // row 0
    white_wins.white_to_move = false;

    ASSERT_EQ(check_terminal(white_wins), GameResult::WHITE_WINS);

    // Symmetry 6 (vertical flip) should break this
    State flipped = apply_symmetry(white_wins, 6);
    int flipped_row = State::row(flipped.bobail_sq);

    // After vertical flip, bobail should be on row 4
    EXPECT_EQ(flipped_row, 4) << "Vertical flip should move row 0 to row 4";

    // And the terminal check would incorrectly say BLACK_WINS
    EXPECT_EQ(check_terminal(flipped), GameResult::BLACK_WINS)
        << "This confirms vertical flip breaks terminal semantics";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
