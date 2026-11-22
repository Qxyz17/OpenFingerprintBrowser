#pragma once

#include <string>

namespace sni {

// 从 TLS ClientHello 里取 SNI。incomplete 为真表示缓冲还不够判断，
// 调用方需要继续读。
bool Extract(const unsigned char* data, size_t len, std::string* host,
             bool* incomplete);

}  // namespace sni
