#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class UpstreamKind {
  kHttp,
  kSocks5,
};

struct UpstreamConfig {
  UpstreamKind kind = UpstreamKind::kHttp;
  std::string host = "127.0.0.1";
  uint16_t port = 7897;
  std::string username;
  std::string password;
};

struct Config {
  UpstreamConfig upstream;

  // 参与拦截的进程名，小写，不带路径
  std::vector<std::string> processes;

  // 命中即放行的进程名
  std::vector<std::string> exclude_processes;

  // 本地接收端口池，包重定向后落到这个区间
  uint16_t base_port = 40000;
  uint16_t port_count = 512;

  // 无流量后回收流程的秒数
  int flow_idle_seconds = 180;

  static bool Load(const std::string& path, Config* out, std::string* error);
};

// 按进程名快照匹配当前运行的进程
std::vector<uint32_t> CollectTargetPids(const Config& config);
