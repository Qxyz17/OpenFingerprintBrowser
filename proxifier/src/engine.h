#pragma once

#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "targets.h"

using LogSink = void (*)(const char* text);

// 没有标签的快照缓存，避免每条 SYN 都枚举进程
struct ProcEntry {
  std::string name;
};

class Engine {
 public:
  explicit Engine(const Config& config);
  ~Engine();

  bool Start(std::string* error);
  void Stop();
  bool running() const { return running_.load(); }
  void set_log(LogSink sink) { log_ = sink; }

 private:
  struct FlowKey {
    uint32_t src_addr;
    uint16_t src_port;
    uint32_t dst_addr;
    uint16_t dst_port;

    bool operator==(const FlowKey& other) const {
      return src_addr == other.src_addr && src_port == other.src_port &&
             dst_addr == other.dst_addr && dst_port == other.dst_port;
    }
  };

  struct FlowKeyHash {
    size_t operator()(const FlowKey& key) const {
      size_t h = key.src_addr;
      h = h * 16777619u ^ key.src_port;
      h = h * 16777619u ^ key.dst_addr;
      h = h * 16777619u ^ key.dst_port;
      return h;
    }
  };

  struct FlowEntry {
    uint16_t proxy_port;
    uint64_t last_active;
  };

  bool OpenDivert(std::string* error);
  void Run();
  void OnPacket(unsigned char* packet, UINT* packet_len);
  void Sweep(uint64_t now);

  void EnsureListener(uint16_t proxy_port);
  void CloseListener(uint16_t proxy_port);
  void AcceptLoop(SOCKET listen_socket, uint16_t proxy_port);

  void RefreshProcTable();
  bool IsTargetPid(uint32_t pid);

  void Log(const std::string& text);

  Config config_;
  TargetTable targets_;

  void* divert_ = nullptr;
  std::atomic<bool> running_{false};
  uint32_t local_ip_ = 0;

  std::thread worker_;
  LogSink log_ = nullptr;

  std::mutex flow_mu_;
  std::unordered_map<FlowKey, FlowEntry, FlowKeyHash> flows_;

  std::mutex listener_mu_;
  std::unordered_map<uint16_t, SOCKET> listeners_;

  std::mutex proc_mu_;
  std::unordered_map<uint32_t, ProcEntry> proc_table_;
  uint64_t proc_stamp_ = 0;
};
