#include "sni.h"

#include <cstdint>

namespace sni {

namespace {

constexpr size_t kRecordHeader = 5;
constexpr size_t kHandshakeHeader = 4;
constexpr size_t kMinClientHello = kRecordHeader + kHandshakeHeader;

bool ReadU16(const unsigned char* p, uint16_t* v) {
  *v = static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
  return true;
}

bool ReadU24(const unsigned char* p, uint32_t* v) {
  *v = (static_cast<uint32_t>(p[0]) << 16) |
       (static_cast<uint32_t>(p[1]) << 8) | static_cast<uint32_t>(p[2]);
  return true;
}

}  // namespace

bool Extract(const unsigned char* data, size_t len, std::string* host,
             bool* incomplete) {
  *incomplete = false;
  host->clear();

  if (len < kMinClientHello) {
    *incomplete = true;
    return false;
  }

  // TLS record: type=22(handshake)
  if (data[0] != 0x16) {
    return false;
  }

  uint16_t record_len = 0;
  ReadU16(data + 3, &record_len);

  if (len < kRecordHeader + record_len) {
    *incomplete = true;
    return false;
  }

  const unsigned char* hs = data + kRecordHeader;

  // handshake type=1(client_hello)
  if (hs[0] != 0x01) {
    return false;
  }

  uint32_t hs_len = 0;
  ReadU24(hs + 1, &hs_len);

  size_t off = kHandshakeHeader;
  size_t limit = kRecordHeader + record_len;
  if (hs_len + kRecordHeader > limit) {
    limit = hs_len + kRecordHeader;
  }

  // client_version(2) + random(32)
  off += 34;
  if (off + 1 > limit) {
    *incomplete = true;
    return false;
  }

  // session_id
  size_t session_len = hs[off];
  off += 1 + session_len;
  if (off + 2 > limit) {
    *incomplete = true;
    return false;
  }

  // cipher_suites
  uint16_t cipher_len = 0;
  ReadU16(hs + off, &cipher_len);
  off += 2 + cipher_len;
  if (off + 1 > limit) {
    *incomplete = true;
    return false;
  }

  // compression_methods
  size_t comp_len = hs[off];
  off += 1 + comp_len;
  if (off + 2 > limit) {
    return false;
  }

  uint16_t ext_total = 0;
  ReadU16(hs + off, &ext_total);
  off += 2;
  size_t ext_end = off + ext_total;
  if (ext_end > limit) {
    *incomplete = true;
    return false;
  }

  while (off + 4 <= ext_end) {
    uint16_t ext_type = 0;
    uint16_t ext_len = 0;
    ReadU16(hs + off, &ext_type);
    ReadU16(hs + off + 2, &ext_len);
    off += 4;
    if (off + ext_len > ext_end) {
      return false;
    }

    // server_name = 0
    if (ext_type == 0 && ext_len >= 5) {
      const unsigned char* ext = hs + off;
      uint16_t list_len = 0;
      ReadU16(ext, &list_len);
      if (list_len + 2 > ext_len) {
        return false;
      }
      size_t p = 2;
      while (p + 3 <= static_cast<size_t>(list_len) + 2) {
        unsigned char name_type = ext[p];
        uint16_t name_len = 0;
        ReadU16(ext + p + 1, &name_len);
        p += 3;
        if (p + name_len > static_cast<size_t>(list_len) + 2) {
          return false;
        }
        if (name_type == 0 && name_len > 0) {
          host->assign(reinterpret_cast<const char*>(ext + p), name_len);
          return true;
        }
        p += name_len;
      }
    }

    off += ext_len;
  }

  return false;
}

}  // namespace sni
