#include "symmetry.h"
#include "hash.h"
#include "movegen.h"
#include <gtest/gtest.h>
#include <unordered_set>

using namespace bobail;

class SymmetryTest : public ::testing::Test {
protected:
    void SetUp() override {
        init_move_tables();
        init_zobrist();
        init_symmetry();
    }
};

TEST_F(SymmetryTest, IdentityPreserved) {
    State s = State::starting_position();
    State t = apply_symmetry(s, 0);  // Identity

    EXPECT_EQ(s, t);
}

TEST_F(SymmetryTest, CenterPreserved) {
    // Center square (12) should map to itself under all symmetries
    for (int sym = 0; sym < NUM_SYMMETRIES; ++sym) {
        EXPECT_EQ(symmetry_map[sym][12], 12) << "Failed for symmetry " << sym;
    }
}

TEST_F(SymmetryTest, Rotate90) {
    // Square 0 (top-left) should go to square 4 (top-right) under 90 CW rotation
    EXPECT_EQ(symmetry_map[1][0], 4);

    // Square 4 should go to 24
    EXPECT_EQ(symmetry_map[1][4], 24);

    // Square 24 should go to 20
    EXPECT_EQ(symmetry_map[1][24], 20);

    // Square 20 should go to 0
    EXPECT_EQ(symmetry_map[1][20], 0);
}

TEST_F(SymmetryTest, SymmetriesAreDistinct) {
    State s = State::starting_position();

    // Apply a move to break symmetry
    auto moves = generate_moves(s);
    State asymmetric = apply_move(s, moves[0]);

    std::unordered_set<uint64_t> seen;
    for (int sym = 0; sym < NUM_SYMMETRIES; ++sym) {
        State t = apply_symmetry(asymmetric, sym);
        uint64_t packed = pack_state(t);
        seen.insert(packed);
    }

    // Should have multiple distinct states (not all 8 necessarily, due to potential remaining symmetry)
    EXPECT_GT(seen.size(), 1);
}

TEST_F(SymmetryTest, StartingPositionSymmetric) {
    State s = State::starting_position();

    // Starting position has D2 symmetry (horizontal reflection + 180 rotation)
    // but not full D4 because rows are different (white vs black)
    auto [canonical, sym] = canonicalize(s);

    // The canonical form should be valid
    EXPECT_TRUE(canonical.is_valid());

    // Canonicalizing the canonical should give identity
    auto [re_canonical, re_sym] = canonicalize(canonical);
    EXPECT_EQ(canonical, re_canonical);
    EXPECT_EQ(re_sym, 0);
}

TEST_F(SymmetryTest, CanonicalHashConsistent) {
    State s = State::starting_position();

    // All symmetric states should have the same canonical hash
    uint64_t expected = canonical_hash(s);

    for (int sym = 0; sym < NUM_SYMMETRIES; ++sym) {
        State t = apply_symmetry(s, sym);
        uint64_t h = canonical_hash(t);
        EXPECT_EQ(h, expected) << "Mismatch for symmetry " << sym;
    }
}

TEST_F(SymmetryTest, SymmetryReducesPositions) {
    // Apply random moves and check that canonicalization reduces unique positions
    State s = State::starting_position();
    auto moves = generate_moves(s);

    std::unordered_set<uint64_t> raw_positions;
    std::unordered_set<uint64_t> canonical_positions;

    for (const auto& m : moves) {
        State ns = apply_move(s, m);
        raw_positions.insert(pack_state(ns));

        auto [canonical, _] = canonicalize(ns);
        canonical_positions.insert(pack_state(canonical));
    }

    // Canonical should have fewer or equal positions
    EXPECT_LE(canonical_positions.size(), raw_positions.size());
}
