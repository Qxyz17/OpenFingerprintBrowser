#pragma once

#include <winsock2.h>

#include <cstdint>
#include <string>

#include "config.h"

namespace relay {

// 与上游代理建一条到 host:port 的隧道。返回的 socket 已就绪，
// 调用方直接在其上搬运字节。
SOCKET ConnectUpstream(const UpstreamConfig& upstream, const std::string& host,
                       uint16_t port);

}  // namespace relay
