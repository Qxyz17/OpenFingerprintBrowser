#pragma once

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "targets.h"

class Engine {
 public:
  Engine(const Config& config, std::vector<uint32_t> pids);
  ~Engine();

  bool Start(std::string* error);
  void Run();
  void Stop();

 private:
  bool OpenDivert(std::string* error);
  bool IsTargetPid(uint32_t pid) const;
  void OnPacket(unsigned char* packet, UINT* packet_len);

  void EnsureListener(uint16_t proxy_port);
  void CloseListener(uint16_t proxy_port);
  void AcceptLoop(SOCKET listen_socket, uint16_t proxy_port);

  Config config_;
  std::vector<uint32_t> pids_;
  TargetTable targets_;

  void* divert_ = nullptr;
  volatile bool running_ = false;
  uint32_t local_ip_ = 0;

  std::mutex flow_mu_;
  std::unordered_map<uint64_t, uint16_t> flows_;

  std::mutex listener_mu_;
  std::unordered_map<uint16_t, SOCKET> listeners_;
};
