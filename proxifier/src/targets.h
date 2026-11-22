#pragma once

#include <winsock2.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 进程到本地代端口的映射。包重定向把目标改写成本机某张网卡上的代端口，
// 反向时靠这张表查回原始目标。
struct TargetKey {
  uint32_t addr;
  uint16_t port;

  bool operator==(const TargetKey& other) const {
    return addr == other.addr && port == other.port;
  }
};

struct TargetKeyHash {
  size_t operator()(const TargetKey& key) const {
    return (static_cast<size_t>(key.addr) << 16) ^ key.port;
  }
};

struct Target {
  TargetKey key{};
  uint16_t proxy_port = 0;
  uint64_t last_active = 0;
};

class TargetTable {
 public:
  TargetTable(uint16_t base_port, uint16_t port_count);

  bool Acquire(const TargetKey& key, uint16_t* proxy_port);
  bool LookupByProxyPort(uint16_t proxy_port, Target* out);
  void Touch(uint16_t proxy_port, uint64_t now);
  void Drop(uint16_t proxy_port);

  // 回收空闲代端口，返回待关闭监听的端口列表
  std::vector<uint16_t> CollectIdle(uint64_t now, int idle_seconds);

  size_t Size() const { return by_port_.size(); }

 private:
  uint16_t base_port_;
  uint16_t port_count_;
  uint16_t next_slot_ = 0;

  std::unordered_map<TargetKey, uint16_t, TargetKeyHash> by_key_;
  std::unordered_map<uint16_t, Target> by_port_;
};
