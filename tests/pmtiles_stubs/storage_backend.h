#pragma once

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <stdint.h>

extern std::map<std::string, std::vector<uint8_t>> mock_sd;

class MeshInkStorageFile {
    const std::vector<uint8_t>* data_ = nullptr;
    size_t pos_ = 0;
public:
    MeshInkStorageFile() = default;
    explicit MeshInkStorageFile(const std::vector<uint8_t>* data) : data_(data) {}
    explicit operator bool() const { return data_ != nullptr; }
    uint32_t size() const { return data_ ? (uint32_t)data_->size() : 0; }
    uint32_t position() const { return (uint32_t)pos_; }
    bool seek(uint32_t pos) {
        if (!data_ || pos > data_->size()) return false;
        pos_ = pos;
        return true;
    }
    size_t read(uint8_t* data, size_t n) {
        if (!data_) return 0;
        n = std::min(n, data_->size() - pos_);
        memcpy(data, data_->data() + pos_, n);
        pos_ += n;
        return n;
    }
    void close() { data_ = nullptr; pos_ = 0; }
};

inline bool meshink_storage_begin() { return true; }
inline void meshink_storage_end() {}
inline MeshInkStorageFile meshink_storage_open(const char* path) {
    if(!path)return MeshInkStorageFile();
    auto it=mock_sd.find(path);
    return it==mock_sd.end()?MeshInkStorageFile():MeshInkStorageFile(&it->second);
}
inline bool meshink_storage_exists(const char* path) {
    return path && mock_sd.find(path)!=mock_sd.end();
}
inline uint32_t meshink_storage_bus_hz() { return 25000000; }
