#include "targets.h"

#include <chrono>

TargetTable::TargetTable(uint16_t base_port, uint16_t port_count)
    : base_port_(base_port), port_count_(port_count) {}

bool TargetTable::Acquire(const TargetKey& key, uint16_t* proxy_port) {
  auto it = by_key_.find(key);
  if (it != by_key_.end()) {
    *proxy_port = it->second;
    return true;
  }

  if (by_port_.size() >= port_count_) {
    return false;
  }

  for (uint16_t i = 0; i < port_count_; ++i) {
    uint16_t candidate = static_cast<uint16_t>(base_port_ + next_slot_);
    next_slot_ = static_cast<uint16_t>((next_slot_ + 1) % port_count_);
    if (by_port_.find(candidate) != by_port_.end()) {
      continue;
    }

    Target target;
    target.key = key;
    target.proxy_port = candidate;
    target.last_active = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());

    by_port_.emplace(candidate, target);
    by_key_.emplace(key, candidate);
    *proxy_port = candidate;
    return true;
  }

  return false;
}

bool TargetTable::LookupByProxyPort(uint16_t proxy_port, Target* out) {
  auto it = by_port_.find(proxy_port);
  if (it == by_port_.end()) {
    return false;
  }
  *out = it->second;
  return true;
}

void TargetTable::Touch(uint16_t proxy_port, uint64_t now) {
  auto it = by_port_.find(proxy_port);
  if (it != by_port_.end()) {
    it->second.last_active = now;
  }
}

void TargetTable::Drop(uint16_t proxy_port) {
  auto it = by_port_.find(proxy_port);
  if (it == by_port_.end()) {
    return;
  }
  by_key_.erase(it->second.key);
  by_port_.erase(it);
}

std::vector<uint16_t> TargetTable::CollectIdle(uint64_t now, int idle_seconds) {
  std::vector<uint16_t> idle;
  for (const auto& entry : by_port_) {
    if (now - entry.second.last_active >= static_cast<uint64_t>(idle_seconds)) {
      idle.push_back(entry.first);
    }
  }
  for (uint16_t port : idle) {
    Drop(port);
  }
  return idle;
}
