#include "api.h"

#include <memory>
#include <mutex>
#include <string>

#include "config.h"

namespace {

std::mutex g_mu;
std::unique_ptr<Engine> g_engine;
std::string g_last_error;
LogSink g_sink = nullptr;

void Writer(const char* text) {
  if (g_sink) {
    g_sink(text);
  }
}

}  // namespace

PX_EXPORT int px_start(const char* config_path) {
  std::lock_guard<std::mutex> lock(g_mu);

  if (g_engine && g_engine->running()) {
    return 0;
  }

  if (!config_path || !*config_path) {
    g_last_error = "配置路径为空";
    return 2;
  }

  Config config;
  std::string error;
  if (!Config::Load(config_path, &config, &error)) {
    g_last_error = error;
    return 3;
  }

  auto engine = std::make_unique<Engine>(config);
  engine->set_log(&Writer);
  if (!engine->Start(&error)) {
    g_last_error = error;
    return 4;
  }

  g_engine = std::move(engine);
  g_last_error.clear();
  return 0;
}

PX_EXPORT void px_stop() {
  std::unique_ptr<Engine> hold;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    hold = std::move(g_engine);
  }
  if (hold) {
    hold->Stop();
  }
}

PX_EXPORT int px_is_running() {
  std::lock_guard<std::mutex> lock(g_mu);
  return (g_engine && g_engine->running()) ? 1 : 0;
}

PX_EXPORT int px_last_error(char* buffer, int buffer_len) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!buffer || buffer_len <= 0) {
    return 0;
  }
  int n = static_cast<int>(g_last_error.size());
  if (n >= buffer_len) {
    n = buffer_len - 1;
  }
  g_last_error.copy(buffer, static_cast<size_t>(n));
  buffer[n] = '\0';
  return n;
}

PX_EXPORT void px_set_log(LogSink sink) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_sink = sink;
}
