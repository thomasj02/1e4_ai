#pragma once

#include <string>
#include <fstream>
#include <xxhash.h>

// Bucket record storage format:
//  +--------------+----------------+-----------------+------------------+-------------------+
//  | record_size  |    key_len     |       key       |    record_len    |      record       |
//  | (uint32_t)   |   (uint32_t)   |   (key_len B)   |    (uint32_t)    |   (record_len B)  |
//  +--------------+----------------+-----------------+------------------+-------------------+
//  record_size: total size of the record including all fields (facilitates skipping to next record)
//  key_len: length of the key in bytes
//  key: the position key used for hashing to buckets
//  record_len: length of the JSON record in bytes
//  record: the JSON record data

namespace chessmimic {

// Bucket record structure shared between components
struct BucketRecord {
    uint32_t record_size{}; // Total record size (including all fields)
    uint32_t key_len{}; // Key length
    std::string key; // Position key
    uint32_t record_len{}; // Record length
    std::string record; // JSON record

    // Serialize the record to a binary stream
    void serialize(std::ofstream& out) const {
        // Write record size (total size of all fields)
        out.write(reinterpret_cast<const char*>(&record_size), sizeof(record_size));

        // Write key length and key
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(key.data(), key_len);

        // Write record length and record
        out.write(reinterpret_cast<const char*>(&record_len), sizeof(record_len));
        out.write(record.data(), record_len);
    }

    // Deserialize the record from a binary stream
    static BucketRecord deserialize(std::ifstream& in) {
        BucketRecord rec;

        // Read record size
        in.read(reinterpret_cast<char*>(&rec.record_size), sizeof(rec.record_size));
        if (!in) return rec;

        // Read key length
        in.read(reinterpret_cast<char*>(&rec.key_len), sizeof(rec.key_len));

        // Read key
        rec.key.resize(rec.key_len);
        in.read(&rec.key[0], rec.key_len);

        // Read record length
        in.read(reinterpret_cast<char*>(&rec.record_len), sizeof(rec.record_len));

        // Read record
        rec.record.resize(rec.record_len);
        in.read(&rec.record[0], rec.record_len);

        return rec;
    }

    // Calculate the primary bucket index for this record
    [[nodiscard]] uint32_t getBucketIndex(uint32_t num_buckets) const {
        // Hash the key with xxHash
        XXH64_hash_t hash = XXH64(key.data(), key.size(), 0); // Seed value 0
        return hash % num_buckets;
    }
    
    // Calculate bucket index using record counter to distribute identical keys
    [[nodiscard]] uint32_t getBucketIndex(uint32_t num_buckets, size_t record_counter) const {
        // Use record counter as seed to ensure different hash for same key
        XXH64_hash_t hash = XXH64(key.data(), key.size(), record_counter);
        return hash % num_buckets;
    }
    
    // Get the hash value for this record (used for overflow calculations)
    [[nodiscard]] XXH64_hash_t getHashValue() const {
        return XXH64(key.data(), key.size(), 0); // Seed value 0
    }
    
    // Get total size of this record in bytes
    [[nodiscard]] size_t getTotalSize() const {
        return sizeof(record_size) + sizeof(key_len) + key.size() + 
               sizeof(record_len) + record.size();
    }
};

} // namespace chessmimic