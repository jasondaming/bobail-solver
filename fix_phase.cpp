#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <db_path> <new_phase>" << std::endl;
        return 1;
    }

    std::string db_path = argv[1];
    uint32_t new_phase = std::stoul(argv[2]);

    // First list all column families
    std::vector<std::string> cf_names;
    rocksdb::Status s = rocksdb::DB::ListColumnFamilies(rocksdb::DBOptions(), db_path, &cf_names);
    if (!s.ok()) {
        std::cerr << "Failed to list CFs: " << s.ToString() << std::endl;
        return 1;
    }

    std::cout << "Column families in DB:" << std::endl;
    for (const auto& name : cf_names) {
        std::cout << "  - " << name << std::endl;
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

    // Find metadata handle
    rocksdb::ColumnFamilyHandle* metadata_handle = nullptr;
    for (size_t i = 0; i < cf_names.size(); i++) {
        if (cf_names[i] == "metadata") {
            metadata_handle = handles[i];
            break;
        }
    }

    if (!metadata_handle) {
        std::cerr << "No metadata CF found" << std::endl;
        return 1;
    }

    // Read current phase
    std::string phase_key = "phase";
    std::string phase_value;
    s = db->Get(rocksdb::ReadOptions(), metadata_handle, phase_key, &phase_value);
    if (s.ok() && phase_value.size() >= 4) {
        uint32_t current;
        std::memcpy(&current, phase_value.data(), sizeof(current));
        std::cout << "Current phase: " << current << std::endl;
    }

    // Write new phase
    std::string new_value(reinterpret_cast<char*>(&new_phase), sizeof(new_phase));
    s = db->Put(rocksdb::WriteOptions(), metadata_handle, phase_key, new_value);
    if (!s.ok()) {
        std::cerr << "Failed to write phase: " << s.ToString() << std::endl;
        return 1;
    }

    std::cout << "Phase updated to: " << new_phase << std::endl;

    for (auto h : handles) delete h;
    delete db;
    return 0;
}
