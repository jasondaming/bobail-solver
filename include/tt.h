#pragma once

#include <cstdint>
#include <vector>
#include <atomic>

namespace bobail {

// Result values for solved positions
enum class Result : int8_t {
    UNKNOWN = 0,
    WIN = 1,      // Win for side to move
    LOSS = -1,    // Loss for side to move
    DRAW = 2      // Draw (repetition)
};

// Transposition table entry for proof-number search
struct TTEntry {
    uint64_t key;           // Zobrist hash for verification
    uint32_t proof;         // Proof number (0 = proven true)
    uint32_t disproof;      // Disproof number (0 = proven false)
    Result result;          // Final result if solved
    uint8_t depth;          // Depth at which this was computed

    TTEntry() : key(0), proof(1), disproof(1), result(Result::UNKNOWN), depth(0) {}

    bool is_solved() const {
        return result != Result::UNKNOWN;
    }

    bool is_proven() const {
        return proof == 0;
    }

    bool is_disproven() const {
        return disproof == 0;
    }
};

// Fixed-size transposition table with replacement
class TranspositionTable {
public:
    // Create table with given number of entries (will be rounded to power of 2)
    explicit TranspositionTable(size_t num_entries);

    // Clear all entries
    void clear();

    // Probe the table for a position
    // Returns pointer to entry if found (key matches), nullptr otherwise
    TTEntry* probe(uint64_t hash);

    // Store an entry (always replaces - could add replacement strategy later)
    void store(uint64_t hash, const TTEntry& entry);

    // Get statistics
    size_t size() const { return entries_.size(); }
    uint64_t hits() const { return hits_; }
    uint64_t misses() const { return misses_; }
    uint64_t stores() const { return stores_; }
    double fill_rate() const;

    void reset_stats() { hits_ = misses_ = stores_ = 0; }

private:
    std::vector<TTEntry> entries_;
    size_t mask_;  // For fast modulo (size - 1)

    // Statistics
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
    uint64_t stores_ = 0;
};

// Infinity values for proof numbers
constexpr uint32_t PN_INFINITY = 0xFFFFFFFF;

} // namespace bobail
