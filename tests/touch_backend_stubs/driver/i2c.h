#pragma once

#include <stdint.h>
#include <stddef.h>
#include <deque>
#include <map>
#include <vector>

using i2c_port_t=int;
using esp_err_t=int;

static constexpr i2c_port_t I2C_NUM_0=0;
static constexpr esp_err_t ESP_OK=0;
static constexpr esp_err_t ESP_FAIL=-1;

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

namespace touch_stub {

struct ReadReply {
    bool ok=true;
    std::vector<uint8_t> bytes;
};

inline std::map<uint16_t,std::deque<ReadReply>> reads;
inline std::vector<std::vector<uint8_t>> writes;
inline bool fail_writes=false;

inline void reset_i2c() {
    reads.clear();
    writes.clear();
    fail_writes=false;
}

inline void queue_read(uint16_t reg,std::initializer_list<uint8_t> bytes) {
    reads[reg].push_back(ReadReply{true,std::vector<uint8_t>(bytes)});
}

inline void queue_read_bytes(uint16_t reg,const std::vector<uint8_t>& bytes) {
    reads[reg].push_back(ReadReply{true,bytes});
}

inline void queue_read_failure(uint16_t reg) {
    reads[reg].push_back(ReadReply{false,{}});
}

} // namespace touch_stub

inline esp_err_t i2c_master_write_read_device(
    i2c_port_t,uint8_t,const uint8_t* write_buffer,size_t write_size,
    uint8_t* read_buffer,size_t read_size,unsigned) {
    if(write_size!=2)return ESP_FAIL;
    const uint16_t reg=(uint16_t)((uint16_t)write_buffer[0]<<8)|write_buffer[1];
    auto it=touch_stub::reads.find(reg);
    if(it==touch_stub::reads.end()||it->second.empty())return ESP_FAIL;
    touch_stub::ReadReply reply=it->second.front();
    it->second.pop_front();
    if(!reply.ok||reply.bytes.size()!=read_size)return ESP_FAIL;
    for(size_t i=0;i<read_size;++i)read_buffer[i]=reply.bytes[i];
    return ESP_OK;
}

inline esp_err_t i2c_master_write_to_device(
    i2c_port_t,uint8_t,const uint8_t* data,size_t len,unsigned) {
    touch_stub::writes.emplace_back(data,data+len);
    return touch_stub::fail_writes?ESP_FAIL:ESP_OK;
}
