#include "relay.h"

#include <ws2tcpip.h>

#include <vector>

namespace relay {

namespace {

bool SendAll(SOCKET s, const char* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    int n = send(s, data + sent, static_cast<int>(len - sent), 0);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

bool RecvExact(SOCKET s, char* buf, size_t len) {
  size_t got = 0;
  while (got < len) {
    int n = recv(s, buf + got, static_cast<int>(len - got), 0);
    if (n <= 0) {
      return false;
    }
    got += static_cast<size_t>(n);
  }
  return true;
}

SOCKET ConnectTcp(const std::string& host, uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* result = nullptr;
  std::string port_str = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
    return INVALID_SOCKET;
  }

  SOCKET s = INVALID_SOCKET;
  for (addrinfo* it = result; it; it = it->ai_next) {
    s = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (s == INVALID_SOCKET) {
      continue;
    }
    if (connect(s, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
      break;
    }
    closesocket(s);
    s = INVALID_SOCKET;
  }

  freeaddrinfo(result);
  return s;
}

std::string Base64(const std::string& in) {
  static const char* kTable =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  while (i + 2 < in.size()) {
    uint32_t v = (static_cast<uint8_t>(in[i]) << 16) |
                 (static_cast<uint8_t>(in[i + 1]) << 8) |
                 static_cast<uint8_t>(in[i + 2]);
    out += kTable[(v >> 18) & 0x3f];
    out += kTable[(v >> 12) & 0x3f];
    out += kTable[(v >> 6) & 0x3f];
    out += kTable[v & 0x3f];
    i += 3;
  }
  if (i + 1 == in.size()) {
    uint32_t v = static_cast<uint8_t>(in[i]) << 16;
    out += kTable[(v >> 18) & 0x3f];
    out += kTable[(v >> 12) & 0x3f];
    out += "==";
  } else if (i + 2 == in.size()) {
    uint32_t v = (static_cast<uint8_t>(in[i]) << 16) |
                 (static_cast<uint8_t>(in[i + 1]) << 8);
    out += kTable[(v >> 18) & 0x3f];
    out += kTable[(v >> 12) & 0x3f];
    out += kTable[(v >> 6) & 0x3f];
    out += '=';
  }
  return out;
}

// 逐行读到空行为止，用于解析 CONNECT 响应头
bool ReadHttpHeader(SOCKET s, std::string* header) {
  header->clear();
  char c;
  while (header->size() < 16384) {
    int n = recv(s, &c, 1, 0);
    if (n <= 0) {
      return false;
    }
    header->push_back(c);
    if (header->size() >= 4 &&
        header->compare(header->size() - 4, 4, "\r\n\r\n") == 0) {
      return true;
    }
  }
  return false;
}

SOCKET HandshakeHttp(const UpstreamConfig& upstream, const std::string& host,
                     uint16_t port) {
  SOCKET s = ConnectTcp(upstream.host, upstream.port);
  if (s == INVALID_SOCKET) {
    return INVALID_SOCKET;
  }

  std::string target = host + ":" + std::to_string(port);
  std::string req = "CONNECT " + target + " HTTP/1.1\r\nHost: " + target + "\r\n";
  if (!upstream.username.empty()) {
    req += "Proxy-Authorization: Basic " +
           Base64(upstream.username + ":" + upstream.password) + "\r\n";
  }
  req += "\r\n";

  if (!SendAll(s, req.data(), req.size())) {
    closesocket(s);
    return INVALID_SOCKET;
  }

  std::string header;
  if (!ReadHttpHeader(s, &header)) {
    closesocket(s);
    return INVALID_SOCKET;
  }

  if (header.find(" 200") == std::string::npos) {
    closesocket(s);
    return INVALID_SOCKET;
  }

  return s;
}

SOCKET HandshakeSocks5(const UpstreamConfig& upstream, const std::string& host,
                       uint16_t port) {
  SOCKET s = ConnectTcp(upstream.host, upstream.port);
  if (s == INVALID_SOCKET) {
    return INVALID_SOCKET;
  }

  bool use_auth = !upstream.username.empty();

  unsigned char greet[4] = {0x05, static_cast<unsigned char>(use_auth ? 2 : 1),
                            0x00, 0x02};
  if (!SendAll(s, reinterpret_cast<const char*>(greet), use_auth ? 4 : 3)) {
    closesocket(s);
    return INVALID_SOCKET;
  }

  unsigned char resp[2];
  if (!RecvExact(s, reinterpret_cast<char*>(resp), 2)) {
    closesocket(s);
    return INVALID_SOCKET;
  }
  if (resp[0] != 0x05 || resp[1] == 0xff) {
    closesocket(s);
    return INVALID_SOCKET;
  }

  if (resp[1] == 0x02) {
    if (!use_auth) {
      closesocket(s);
      return INVALID_SOCKET;
    }
    std::string user = upstream.username;
    std::string pass = upstream.password;
    if (user.size() > 255 || pass.size() > 255) {
      closesocket(s);
      return INVALID_SOCKET;
    }
    std::vector<char> auth;
    auth.push_back(0x01);
    auth.push_back(static_cast<char>(user.size()));
    auth.insert(auth.end(), user.begin(), user.end());
    auth.push_back(static_cast<char>(pass.size()));
    auth.insert(auth.end(), pass.begin(), pass.end());
    if (!SendAll(s, auth.data(), auth.size())) {
      closesocket(s);
      return INVALID_SOCKET;
    }
    unsigned char auth_resp[2];
    if (!RecvExact(s, reinterpret_cast<char*>(auth_resp), 2) ||
        auth_resp[1] != 0x00) {
      closesocket(s);
      return INVALID_SOCKET;
    }
  }

  // 域名交给代理解析，本地不查 DNS
  std::vector<char> req;
  req.push_back(0x05);
  req.push_back(0x01);
  req.push_back(0x00);
  req.push_back(0x03);
  req.push_back(static_cast<char>(host.size()));
  req.insert(req.end(), host.begin(), host.end());
  req.push_back(static_cast<char>((port >> 8) & 0xff));
  req.push_back(static_cast<char>(port & 0xff));

  if (!SendAll(s, req.data(), req.size())) {
    closesocket(s);
    return INVALID_SOCKET;
  }

  unsigned char head[4];
  if (!RecvExact(s, reinterpret_cast<char*>(head), 4) || head[1] != 0x00) {
    closesocket(s);
    return INVALID_SOCKET;
  }

  size_t skip = 0;
  switch (head[3]) {
    case 0x01:
      skip = 4;
      break;
    case 0x03: {
      unsigned char len = 0;
      if (!RecvExact(s, reinterpret_cast<char*>(&len), 1)) {
        closesocket(s);
        return INVALID_SOCKET;
      }
      skip = static_cast<size_t>(len) + 2;
      break;
    }
    case 0x04:
      skip = 16 + 2;
      break;
    default:
      closesocket(s);
      return INVALID_SOCKET;
  }

  std::vector<char> tail(skip);
  if (skip > 0 && !RecvExact(s, tail.data(), skip)) {
    closesocket(s);
    return INVALID_SOCKET;
  }

  return s;
}

}  // namespace

SOCKET ConnectUpstream(const UpstreamConfig& upstream, const std::string& host,
                       uint16_t port) {
  if (upstream.kind == UpstreamKind::kSocks5) {
    return HandshakeSocks5(upstream, host, port);
  }
  return HandshakeHttp(upstream, host, port);
}

}  // namespace relay
