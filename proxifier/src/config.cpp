#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "config.h"

namespace {

std::string Trim(const std::string& s) {
  size_t begin = s.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(begin, end - begin + 1);
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::vector<std::string> SplitList(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item = Trim(item);
    if (!item.empty()) {
      out.push_back(Lower(item));
    }
  }
  return out;
}

// 极简 INI，只支持本节用到的一级键值
bool ParseLine(const std::string& line, std::string* key, std::string* value) {
  if (line.empty() || line[0] == '#' || line[0] == '[') {
    return false;
  }
  size_t eq = line.find('=');
  if (eq == std::string::npos) {
    return false;
  }
  *key = Lower(Trim(line.substr(0, eq)));
  *value = Trim(line.substr(eq + 1));
  return true;
}

}  // namespace

bool Config::Load(const std::string& path, Config* out, std::string* error) {
  std::ifstream in(path);
  if (!in) {
    *error = "读取配置失败: " + path;
    return false;
  }

  std::string line;
  while (std::getline(in, line)) {
    std::string key;
    std::string value;
    if (!ParseLine(line, &key, &value)) {
      continue;
    }

    if (key == "upstream.type") {
      out->upstream.kind =
          Lower(value) == "socks5" ? UpstreamKind::kSocks5 : UpstreamKind::kHttp;
    } else if (key == "upstream.host") {
      out->upstream.host = value;
    } else if (key == "upstream.port") {
      out->upstream.port = static_cast<uint16_t>(std::stoi(value));
    } else if (key == "upstream.username") {
      out->upstream.username = value;
    } else if (key == "upstream.password") {
      out->upstream.password = value;
    } else if (key == "processes") {
      out->processes = SplitList(value);
    } else if (key == "exclude_processes") {
      out->exclude_processes = SplitList(value);
    } else if (key == "base_port") {
      out->base_port = static_cast<uint16_t>(std::stoi(value));
    } else if (key == "port_count") {
      out->port_count = static_cast<uint16_t>(std::stoi(value));
    } else if (key == "flow_idle_seconds") {
      out->flow_idle_seconds = std::stoi(value);
    }
  }

  if (out->processes.empty()) {
    *error = "processes 不能为空";
    return false;
  }
  return true;
}
