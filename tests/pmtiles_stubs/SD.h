#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <stdint.h>

extern std::map<std::string, std::vector<uint8_t>> mock_sd;
class File {
    const std::vector<uint8_t>* data_ = nullptr;
    size_t pos_ = 0;
public:
    File() = default;
    explicit File(const std::vector<uint8_t>* data) : data_(data) {}
    explicit operator bool() const { return data_ != nullptr; }
    uint32_t size() const { return data_ ? (uint32_t)data_->size() : 0; }
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
    void close() { data_ = nullptr; }
};
struct MockSD {
    File open(const char* path, int) {
        auto it = mock_sd.find(path);
        return it == mock_sd.end() ? File() : File(&it->second);
    }
};
extern MockSD SD;
