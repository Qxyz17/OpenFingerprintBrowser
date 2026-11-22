#include "engine.h"

#include <iphlpapi.h>
#include <tlhelp32.h>
#include <ws2tcpip.h>
#include <windivert.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <thread>

#include "relay.h"
#include "sni.h"

namespace {

constexpr uint64_t kProcRefreshMs = 1500;
constexpr uint64_t kSweepMs = 30000;

uint64_t NowMilli() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

uint64_t NowSeconds() { return NowMilli() / 1000; }

std::string LowerAscii(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

// 环回地址在 WinDivert 的包路径上看不到，重定向只能落到真实网卡的 IPv4
uint32_t PickLocalIpv4() {
  const ULONG flags = GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  ULONG size = 0;
  GetAdaptersAddresses(AF_INET, flags, nullptr, nullptr, &size);
  if (size == 0) {
    return htonl(INADDR_LOOPBACK);
  }

  std::vector<unsigned char> buf(size);
  auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  if (GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size) != NO_ERROR) {
    return htonl(INADDR_LOOPBACK);
  }

  for (auto* a = addrs; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp) {
      continue;
    }
    for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
      auto* sa = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
      if (sa->sin_family != AF_INET) {
        continue;
      }
      if (sa->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
        continue;
      }
      return sa->sin_addr.s_addr;
    }
  }
  return htonl(INADDR_LOOPBACK);
}

uint32_t LookupOwnerPid(uint32_t local_addr, uint16_t local_port,
                        uint32_t remote_addr, uint16_t remote_port) {
  ULONG size = 0;
  GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
  if (size == 0) {
    return 0;
  }

  std::vector<unsigned char> buf(size);
  auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
  if (GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL,
                          0) != NO_ERROR) {
    return 0;
  }

  for (DWORD i = 0; i < table->dwNumEntries; ++i) {
    const auto& row = table->table[i];
    if (static_cast<uint16_t>(ntohs(static_cast<unsigned short>(row.dwLocalPort))) !=
        local_port) {
      continue;
    }
    if (row.dwLocalAddr != local_addr || row.dwRemoteAddr != remote_addr) {
      continue;
    }
    if (static_cast<uint16_t>(
            ntohs(static_cast<unsigned short>(row.dwRemotePort))) != remote_port) {
      continue;
    }
    return row.dwOwningPid;
  }
  return 0;
}

void RelayBoth(SOCKET client, SOCKET upstream) {
  auto pump = [](SOCKET from, SOCKET to) {
    std::vector<char> buf(16384);
    while (true) {
      int n = recv(from, buf.data(), static_cast<int>(buf.size()), 0);
      if (n <= 0) {
        break;
      }
      int sent = 0;
      while (sent < n) {
        int w = send(to, buf.data() + sent, n - sent, 0);
        if (w <= 0) {
          shutdown(to, SD_SEND);
          return;
        }
        sent += w;
      }
    }
    shutdown(to, SD_SEND);
  };

  std::thread up(pump, client, upstream);
  pump(upstream, client);
  up.join();
  closesocket(client);
  closesocket(upstream);
}

}  // namespace

Engine::Engine(const Config& config)
    : config_(config), targets_(config.base_port, config.port_count) {}

Engine::~Engine() { Stop(); }

void Engine::Log(const std::string& text) {
  if (log_) {
    log_(text.c_str());
  }
}

void Engine::RefreshProcTable() {
  uint64_t now = NowMilli();
  if (now - proc_stamp_ < kProcRefreshMs) {
    return;
  }
  proc_stamp_ = now;

  std::unordered_map<uint32_t, ProcEntry> table;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return;
  }

  PROCESSENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  if (Process32First(snap, &entry)) {
    do {
      table[entry.th32ProcessID] = ProcEntry{LowerAscii(entry.szExeFile)};
    } while (Process32Next(snap, &entry));
  }
  CloseHandle(snap);

  std::lock_guard<std::mutex> lock(proc_mu_);
  proc_table_.swap(table);
}

bool Engine::IsTargetPid(uint32_t pid) {
  RefreshProcTable();

  std::string name;
  {
    std::lock_guard<std::mutex> lock(proc_mu_);
    auto it = proc_table_.find(pid);
    if (it == proc_table_.end()) {
      return false;
    }
    name = it->second.name;
  }

  for (const std::string& p : config_.processes) {
    if (p == name) {
      return true;
    }
  }
  for (const std::string& p : config_.exclude_processes) {
    if (p == name) {
      return false;
    }
  }
  return false;
}

bool Engine::OpenDivert(std::string* error) {
  divert_ = WinDivertOpen("outbound and tcp", WINDIVERT_LAYER_NETWORK, 0, 0);
  if (divert_ == INVALID_HANDLE_VALUE) {
    divert_ = nullptr;
    *error = "WinDivert 打开失败，需要管理员权限";
    return false;
  }
  WinDivertSetParam(divert_, WINDIVERT_PARAM_QUEUE_LENGTH, 8192);
  return true;
}

bool Engine::Start(std::string* error) {
  if (running_.load()) {
    return true;
  }

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    *error = "WSAStartup 失败";
    return false;
  }

  if (!OpenDivert(error)) {
    WSACleanup();
    return false;
  }

  local_ip_ = PickLocalIpv4();
  if (local_ip_ == htonl(INADDR_LOOPBACK)) {
    *error = "找不到可用网卡的 IPv4";
    Stop();
    return false;
  }

  running_.store(true);
  worker_ = std::thread([this] { Run(); });
  return true;
}

void Engine::Stop() {
  if (!running_.exchange(false)) {
    if (worker_.joinable()) {
      worker_.join();
    }
    return;
  }

  if (divert_) {
    WinDivertClose(divert_);
    divert_ = nullptr;
  }

  if (worker_.joinable()) {
    worker_.join();
  }

  {
    std::lock_guard<std::mutex> lock(listener_mu_);
    for (auto& entry : listeners_) {
      closesocket(entry.second);
    }
    listeners_.clear();
  }

  {
    std::lock_guard<std::mutex> lock(flow_mu_);
    flows_.clear();
  }

  WSACleanup();
}

void Engine::OnPacket(unsigned char* packet, UINT* packet_len) {
  auto* addr = reinterpret_cast<WINDIVERT_ADDRESS*>(
      packet + *packet_len - sizeof(WINDIVERT_ADDRESS));
  UINT raw_len = *packet_len - static_cast<UINT>(sizeof(WINDIVERT_ADDRESS));

  PWINDIVERT_IPHDR iphdr = nullptr;
  PWINDIVERT_TCPHDR tcphdr = nullptr;
  WinDivertHelperParsePacket(packet, raw_len, &iphdr, nullptr, nullptr, nullptr,
                             nullptr, &tcphdr, nullptr, nullptr, nullptr,
                             nullptr, nullptr);
  if (!iphdr || !tcphdr) {
    WinDivertSend(divert_, packet, raw_len, nullptr, addr);
    return;
  }

  const uint32_t src_addr = iphdr->SrcAddr;
  const uint32_t dst_addr = iphdr->DstAddr;
  const uint16_t src_port = ntohs(tcphdr->SrcPort);
  const uint16_t dst_port = ntohs(tcphdr->DstPort);
  const uint16_t start = config_.base_port;
  const uint16_t end = static_cast<uint16_t>(config_.base_port + config_.port_count);

  if (addr->Outbound) {
    FlowKey key{src_addr, src_port, dst_addr, dst_port};

    {
      std::lock_guard<std::mutex> lock(flow_mu_);
      auto it = flows_.find(key);
      if (it != flows_.end()) {
        it->second.last_active = NowSeconds();
        iphdr->DstAddr = local_ip_;
        tcphdr->DstPort = htons(it->second.proxy_port);
        WinDivertHelperCalcChecksums(packet, raw_len, addr, 0);
        WinDivertSend(divert_, packet, raw_len, nullptr, addr);
        return;
      }
    }

    const bool is_syn = (tcphdr->Syn != 0) && (tcphdr->Ack == 0);
    if (is_syn && !(dst_addr == local_ip_ && dst_port >= start && dst_port < end)) {
      uint32_t pid = LookupOwnerPid(src_addr, src_port, dst_addr, dst_port);
      if (pid != 0 && pid != GetCurrentProcessId() && IsTargetPid(pid)) {
        TargetKey target{dst_addr, dst_port};
        uint16_t proxy_port = 0;
        if (targets_.Acquire(target, &proxy_port)) {
          {
            std::lock_guard<std::mutex> lock(flow_mu_);
            flows_[key] = FlowEntry{proxy_port, NowSeconds()};
          }
          EnsureListener(proxy_port);
          iphdr->DstAddr = local_ip_;
          tcphdr->DstPort = htons(proxy_port);
          WinDivertHelperCalcChecksums(packet, raw_len, addr, 0);
        }
      }
    }

    WinDivertSend(divert_, packet, raw_len, nullptr, addr);
    return;
  }

  // 代端口方向的报文还原成原始四元组，客户端状态机才不错乱
  if (src_addr == local_ip_ && src_port >= start && src_port < end) {
    Target target;
    if (targets_.LookupByProxyPort(src_port, &target)) {
      targets_.Touch(src_port, NowSeconds());
      iphdr->SrcAddr = target.key.addr;
      tcphdr->SrcPort = htons(target.key.port);
      WinDivertHelperCalcChecksums(packet, raw_len, addr, 0);
    }
    if (tcphdr->Fin || tcphdr->Rst) {
      std::lock_guard<std::mutex> lock(flow_mu_);
      flows_.erase(FlowKey{dst_addr, dst_port, local_ip_, src_port});
    }
  }

  WinDivertSend(divert_, packet, raw_len, nullptr, addr);
}

void Engine::Sweep(uint64_t now) {
  auto idle = targets_.CollectIdle(now, config_.flow_idle_seconds);
  for (uint16_t port : idle) {
    CloseListener(port);
  }

  std::lock_guard<std::mutex> lock(flow_mu_);
  for (auto it = flows_.begin(); it != flows_.end();) {
    if (now - it->second.last_active >=
        static_cast<uint64_t>(config_.flow_idle_seconds)) {
      it = flows_.erase(it);
    } else {
      ++it;
    }
  }
}

void Engine::Run() {
  std::vector<unsigned char> buffer(WINDIVERT_MTU_MAX + sizeof(WINDIVERT_ADDRESS));
  uint64_t last_sweep = NowMilli();

  while (running_.load()) {
    UINT len = 0;
    WINDIVERT_ADDRESS addr{};
    if (!WinDivertRecv(divert_, buffer.data(),
                       static_cast<UINT>(buffer.size()) - sizeof(addr), &len,
                       &addr)) {
      if (!running_.load()) {
        break;
      }
      if (GetLastError() == ERROR_NO_DATA) {
        continue;
      }
      // 驱动被卸载或句柄失效，退出循环让上层看到非运行态
      running_.store(false);
      break;
    }

    std::memcpy(buffer.data() + len, &addr, sizeof(addr));
    UINT total = len + static_cast<UINT>(sizeof(addr));
    OnPacket(buffer.data(), &total);

    uint64_t now = NowMilli();
    if (now - last_sweep >= kSweepMs) {
      last_sweep = now;
      Sweep(now / 1000);
    }
  }
}

void Engine::EnsureListener(uint16_t proxy_port) {
  std::lock_guard<std::mutex> lock(listener_mu_);
  if (listeners_.find(proxy_port) != listeners_.end()) {
    return;
  }

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    return;
  }

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = local_ip_;
  sa.sin_port = htons(proxy_port);
  if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 ||
      listen(s, SOMAXCONN) != 0) {
    closesocket(s);
    Log("代端口监听失败: " + std::to_string(proxy_port));
    return;
  }

  listeners_[proxy_port] = s;
  std::thread([this, s, proxy_port] { AcceptLoop(s, proxy_port); }).detach();
}

void Engine::CloseListener(uint16_t proxy_port) {
  std::lock_guard<std::mutex> lock(listener_mu_);
  auto it = listeners_.find(proxy_port);
  if (it == listeners_.end()) {
    return;
  }
  closesocket(it->second);
  listeners_.erase(it);
}

void Engine::AcceptLoop(SOCKET listen_socket, uint16_t proxy_port) {
  while (running_.load()) {
    SOCKET client = accept(listen_socket, nullptr, nullptr);
    if (client == INVALID_SOCKET) {
      return;
    }

    Target target;
    if (!targets_.LookupByProxyPort(proxy_port, &target)) {
      closesocket(client);
      continue;
    }

    in_addr ia{};
    ia.s_addr = target.key.addr;
    char ip_buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &ia, ip_buf, sizeof(ip_buf));

    // 首包能取到 SNI 就按域名走，主机名交给代理解析，本机不做 DNS
    std::string host = ip_buf;
    char peek_buf[8192];
    int n = recv(client, peek_buf, sizeof(peek_buf), MSG_PEEK);
    if (n > 0) {
      std::string sni_host;
      bool incomplete = false;
      if (sni::Extract(reinterpret_cast<unsigned char*>(peek_buf),
                       static_cast<size_t>(n), &sni_host, &incomplete) &&
          !sni_host.empty()) {
        host = sni_host;
      }
    }

    SOCKET upstream =
        relay::ConnectUpstream(config_.upstream, host, target.key.port);
    if (upstream == INVALID_SOCKET) {
      closesocket(client);
      continue;
    }

    std::thread([client, upstream] { RelayBoth(client, upstream); }).detach();
  }
}
