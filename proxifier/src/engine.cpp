#include "engine.h"

#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <windivert.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "relay.h"
#include "sni.h"

namespace {

uint64_t NowSeconds() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

uint64_t PackKey(uint32_t src_addr, uint16_t src_port, uint32_t dst_addr,
                 uint16_t dst_port) {
  uint64_t h = src_addr;
  h = h * 16777619u ^ src_port;
  h = h * 16777619u ^ dst_addr;
  h = h * 16777619u ^ dst_port;
  return h;
}

// 环回地址不能作为重定向目标，WinDivert 的报文看不到本机环回，
// 必须挑一张真实网卡的 IPv4
uint32_t PickLocalIpv4() {
  ULONG size = 0;
  const ULONG flags = GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
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
    uint16_t row_local_port =
        static_cast<uint16_t>(ntohs(static_cast<unsigned short>(row.dwLocalPort)));
    if (row_local_port != local_port) {
      continue;
    }
    if (row.dwLocalAddr != local_addr || row.dwRemoteAddr != remote_addr) {
      continue;
    }
    uint16_t row_remote_port =
        static_cast<uint16_t>(ntohs(static_cast<unsigned short>(row.dwRemotePort)));
    if (row_remote_port != remote_port) {
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

Engine::Engine(const Config& config, std::vector<uint32_t> pids)
    : config_(config),
      pids_(std::move(pids)),
      targets_(config.base_port, config.port_count) {}

Engine::~Engine() { Stop(); }

bool Engine::IsTargetPid(uint32_t pid) const {
  for (uint32_t p : pids_) {
    if (p == pid) {
      return true;
    }
  }
  return false;
}

bool Engine::OpenDivert(std::string* error) {
  divert_ = WinDivertOpen("outbound and tcp", WINDIVERT_LAYER_NETWORK, 0, 0);
  if (divert_ == INVALID_HANDLE_VALUE) {
    divert_ = nullptr;
    *error = "WinDivertOpen 打开失败，需要管理员权限并允许加载驱动";
    return false;
  }
  WinDivertSetParam(divert_, WINDIVERT_PARAM_QUEUE_LENGTH, 8192);
  return true;
}

bool Engine::Start(std::string* error) {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    *error = "WSAStartup 失败";
    return false;
  }
  return OpenDivert(error);
}

void Engine::OnPacket(unsigned char* packet, UINT* packet_len) {
  auto* addr =
      reinterpret_cast<WINDIVERT_ADDRESS*>(packet + *packet_len - sizeof(WINDIVERT_ADDRESS));
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
    uint64_t key = PackKey(src_addr, src_port, dst_addr, dst_port);

    {
      std::lock_guard<std::mutex> lock(flow_mu_);
      auto it = flows_.find(key);
      if (it != flows_.end()) {
        iphdr->DstAddr = local_ip_;
        tcphdr->DstPort = htons(it->second);
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
          std::lock_guard<std::mutex> lock(flow_mu_);
          flows_[key] = proxy_port;
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

  // 代端口方向的报文改回原始四元组，客户端状态机才不会错乱
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
      flows_.erase(PackKey(dst_addr, dst_port, local_ip_, src_port));
    }
  }

  WinDivertSend(divert_, packet, raw_len, nullptr, addr);
}

void Engine::Run() {
  local_ip_ = PickLocalIpv4();
  running_ = true;

  std::vector<unsigned char> buffer(WINDIVERT_MTU_MAX + sizeof(WINDIVERT_ADDRESS));
  uint64_t last_sweep = NowSeconds();

  while (running_) {
    UINT len = 0;
    WINDIVERT_ADDRESS addr{};
    if (!WinDivertRecv(divert_, buffer.data(),
                       static_cast<UINT>(buffer.size()) - sizeof(addr), &len,
                       &addr)) {
      break;
    }

    std::memcpy(buffer.data() + len, &addr, sizeof(addr));
    UINT total = len + static_cast<UINT>(sizeof(addr));
    OnPacket(buffer.data(), &total);

    uint64_t now = NowSeconds();
    if (now - last_sweep >= 30) {
      last_sweep = now;
      auto idle = targets_.CollectIdle(now, config_.flow_idle_seconds);
      for (uint16_t port : idle) {
        CloseListener(port);
      }
    }
  }
}

void Engine::Stop() {
  running_ = false;
  if (divert_) {
    WinDivertClose(divert_);
    divert_ = nullptr;
  }
  {
    std::lock_guard<std::mutex> lock(listener_mu_);
    for (auto& entry : listeners_) {
      closesocket(entry.second);
    }
    listeners_.clear();
  }
  WSACleanup();
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
  while (running_) {
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

    // TLS 首包里有 SNI 就按域名走，交给代理解析，
    // 本机不做域名解析，避免 DNS 泄漏
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
