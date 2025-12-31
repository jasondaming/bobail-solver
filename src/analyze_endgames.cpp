#include <iostream>
#include <map>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include "board.h"
#include "retrograde_db.h"

using namespace bobail;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <db_path>\n";
        return 1;
    }

    std::string db_path = argv[1];

    // Open database read-only
    rocksdb::DB* db;
    rocksdb::Options options;
    options.create_if_missing = false;

    std::vector<rocksdb::ColumnFamilyDescriptor> cf_descs;
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(rocksdb::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions()));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor("states", rocksdb::ColumnFamilyOptions()));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor("packed_to_id", rocksdb::ColumnFamilyOptions()));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor("predecessors", rocksdb::ColumnFamilyOptions()));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor("queue", rocksdb::ColumnFamilyOptions()));
    cf_descs.push_back(rocksdb::ColumnFamilyDescriptor("metadata", rocksdb::ColumnFamilyOptions()));

    std::vector<rocksdb::ColumnFamilyHandle*> cf_handles;
    rocksdb::Status status = rocksdb::DB::OpenForReadOnly(options, db_path, cf_descs, &cf_handles, &db);

    if (!status.ok()) {
        std::cerr << "Failed to open database: " << status.ToString() << "\n";
        return 1;
    }

    auto cf_packed_to_id = cf_handles[2];
    auto cf_states = cf_handles[1];

    // Count states by (white_pawn_count, black_pawn_count)
    std::map<std::pair<int,int>, uint64_t> pawn_counts;
    std::map<std::pair<int,int>, uint64_t> solved_counts;  // WIN or LOSS
    std::map<std::pair<int,int>, uint64_t> draw_counts;

    // Iterate through packed_to_id to get all packed states
    rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions(), cf_packed_to_id);

    uint64_t total = 0;
    uint64_t solved = 0;
    uint64_t draws = 0;

    // Sample 0.1% of states for faster analysis
    const uint64_t SAMPLE_RATE = 1000;  // 1 in 1000
    uint64_t skip_counter = 0;

    std::cout << "Scanning database (sampling 1 in " << SAMPLE_RATE << ")..." << std::endl;
    std::cout.setf(std::ios::unitbuf);  // Disable buffering

    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        skip_counter++;
        if (skip_counter % SAMPLE_RATE != 0) continue;  // Skip most entries

        if (it->key().size() != sizeof(uint64_t)) continue;

        uint64_t packed;
        memcpy(&packed, it->key().data(), sizeof(packed));

        // Unpack to get pawn counts
        State s = unpack_state(packed);
        int white_pawns = __builtin_popcount(s.white_pawns);
        int black_pawns = __builtin_popcount(s.black_pawns);

        auto key = std::make_pair(white_pawns, black_pawns);
        pawn_counts[key]++;

        // Get state ID and check if solved
        if (it->value().size() == sizeof(uint32_t)) {
            uint32_t state_id;
            memcpy(&state_id, it->value().data(), sizeof(state_id));

            // Read state info
            std::string id_key(reinterpret_cast<char*>(&state_id), sizeof(state_id));
            std::string state_value;
            if (db->Get(rocksdb::ReadOptions(), cf_states, id_key, &state_value).ok()) {
                if (state_value.size() >= 1) {
                    uint8_t result = static_cast<uint8_t>(state_value[0]);
                    if (result == 1 || result == 2) {  // WIN or LOSS
                        solved_counts[key]++;
                        solved++;
                    } else if (result == 3) {  // DRAW
                        draw_counts[key]++;
                        draws++;
                    }
                }
            }
        }

        total++;
        if (total % 10000 == 0) {
            std::cout << "Sampled " << total << " states (est. " << (skip_counter / 1000000) << "M total)..." << std::endl;
        }
    }

    delete it;

    std::cout << "\n=== Endgame Analysis (Sampled) ===\n";
    std::cout << "Sampled states: " << total << " (1 in " << SAMPLE_RATE << ")\n";
    std::cout << "Estimated total: " << (skip_counter) << "\n";
    std::cout << "Solved (WIN/LOSS): " << solved << " (" << (100.0 * solved / total) << "%)\n";
    std::cout << "Draws: " << draws << " (" << (100.0 * draws / total) << "%)\n\n";

    std::cout << "States by pawn count (W,B):\n";
    std::cout << "W  B  | Count      | Solved     | Draws      | % Solved\n";
    std::cout << "------|------------|------------|------------|----------\n";

    for (const auto& [key, count] : pawn_counts) {
        auto solved_it = solved_counts.find(key);
        auto draw_it = draw_counts.find(key);
        uint64_t s = (solved_it != solved_counts.end()) ? solved_it->second : 0;
        uint64_t d = (draw_it != draw_counts.end()) ? draw_it->second : 0;
        double pct = (count > 0) ? (100.0 * (s + d) / count) : 0;

        printf("%2d %2d | %10lu | %10lu | %10lu | %6.2f%%\n",
               key.first, key.second, count, s, d, pct);
    }

    // Clean up
    for (auto h : cf_handles) delete h;
    delete db;

    return 0;
}
