#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <db_path>" << std::endl;
        return 1;
    }

    std::string db_path = argv[1];

    // List all column families
    std::vector<std::string> cf_names;
    rocksdb::Status s = rocksdb::DB::ListColumnFamilies(rocksdb::DBOptions(), db_path, &cf_names);
    if (!s.ok()) {
        std::cerr << "Failed to list CFs: " << s.ToString() << std::endl;
        return 1;
    }

    // Open with all CFs
    std::vector<rocksdb::ColumnFamilyDescriptor> cf_descs;
    for (const auto& name : cf_names) {
        cf_descs.push_back(rocksdb::ColumnFamilyDescriptor(name, rocksdb::ColumnFamilyOptions()));
    }

    rocksdb::DB* db;
    std::vector<rocksdb::ColumnFamilyHandle*> handles;
    s = rocksdb::DB::Open(rocksdb::Options(), db_path, cf_descs, &handles, &db);
    if (!s.ok()) {
        std::cerr << "Failed to open DB: " << s.ToString() << std::endl;
        return 1;
    }

    // Find handles
    rocksdb::ColumnFamilyHandle* default_handle = nullptr;
    rocksdb::ColumnFamilyHandle* metadata_handle = nullptr;
    for (size_t i = 0; i < cf_names.size(); i++) {
        if (cf_names[i] == "default") default_handle = handles[i];
        if (cf_names[i] == "metadata") metadata_handle = handles[i];
    }

    if (!default_handle || !metadata_handle) {
        std::cerr << "Missing required column families" << std::endl;
        return 1;
    }

    // Keys to migrate
    std::vector<std::string> keys = {"phase", "num_states", "num_wins", "num_losses", 
                                      "num_draws", "start_id", "enum_processed",
                                      "queue_head", "queue_tail"};

    rocksdb::WriteBatch batch;
    int migrated = 0;

    for (const auto& key : keys) {
        std::string value;
        s = db->Get(rocksdb::ReadOptions(), default_handle, key, &value);
        if (s.ok()) {
            // Copy to metadata CF
            batch.Put(metadata_handle, key, value);
            std::cout << "Migrating " << key << " (" << value.size() << " bytes)" << std::endl;
            migrated++;
        }
    }

    if (migrated > 0) {
        s = db->Write(rocksdb::WriteOptions(), &batch);
        if (!s.ok()) {
            std::cerr << "Failed to write: " << s.ToString() << std::endl;
            return 1;
        }
        std::cout << "Migrated " << migrated << " keys to metadata CF" << std::endl;
    } else {
        std::cout << "No keys found in default CF to migrate" << std::endl;
    }

    // Verify
    std::string phase_val;
    s = db->Get(rocksdb::ReadOptions(), metadata_handle, "phase", &phase_val);
    if (s.ok() && phase_val.size() >= 4) {
        uint32_t phase;
        std::memcpy(&phase, phase_val.data(), sizeof(phase));
        std::cout << "Phase in metadata CF: " << phase << std::endl;
    }

    for (auto h : handles) delete h;
    delete db;
    return 0;
}
