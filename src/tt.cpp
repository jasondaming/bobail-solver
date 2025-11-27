#include "tt.h"
#include <bit>
#include <algorithm>

namespace bobail {

TranspositionTable::TranspositionTable(size_t num_entries) {
    // Round up to next power of 2
    size_t size = std::bit_ceil(num_entries);
    entries_.resize(size);
    mask_ = size - 1;
}

void TranspositionTable::clear() {
    std::fill(entries_.begin(), entries_.end(), TTEntry{});
    reset_stats();
}

TTEntry* TranspositionTable::probe(uint64_t hash) {
    size_t idx = hash & mask_;
    TTEntry& entry = entries_[idx];

    if (entry.key == hash) {
        ++hits_;
        return &entry;
    }

    ++misses_;
    return nullptr;
}

void TranspositionTable::store(uint64_t hash, const TTEntry& entry) {
    size_t idx = hash & mask_;
    entries_[idx] = entry;
    entries_[idx].key = hash;
    ++stores_;
}

double TranspositionTable::fill_rate() const {
    size_t filled = 0;
    for (const auto& e : entries_) {
        if (e.key != 0) ++filled;
    }
    return static_cast<double>(filled) / entries_.size();
}

} // namespace bobail
