#include <windows.h>
#include <tlhelp32.h>

#include <cctype>
#include <string>
#include <vector>

#include "config.h"

namespace {

std::string LowerAscii(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

struct ProcessEntry {
  DWORD pid;
  std::string name;
};

std::vector<ProcessEntry> SnapshotProcesses() {
  std::vector<ProcessEntry> out;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return out;
  }

  PROCESSENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  if (Process32First(snap, &entry)) {
    do {
      out.push_back({entry.th32ProcessID, LowerAscii(entry.szExeFile)});
    } while (Process32Next(snap, &entry));
  }

  CloseHandle(snap);
  return out;
}

bool MatchName(const std::vector<std::string>& patterns, const std::string& name) {
  for (const std::string& p : patterns) {
    if (p == name) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::vector<uint32_t> CollectTargetPids(const Config& config) {
  std::vector<uint32_t> pids;
  for (const ProcessEntry& entry : SnapshotProcesses()) {
    if (!MatchName(config.processes, entry.name)) {
      continue;
    }
    if (MatchName(config.exclude_processes, entry.name)) {
      continue;
    }
    pids.push_back(entry.pid);
  }
  return pids;
}
