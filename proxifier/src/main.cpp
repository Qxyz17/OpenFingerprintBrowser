#include <csignal>
#include <cstdio>
#include <string>

#include <windows.h>

#include "api.h"

namespace {

volatile bool g_stop = false;

void OnSignal(int) { g_stop = true; }

void OnLog(const char* text) { std::printf("%s\n", text); }

}  // namespace

int main(int argc, char** argv) {
  std::string config_path;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::printf("pxcli --config <path>\n");
      return 0;
    }
  }

  if (config_path.empty()) {
    std::printf("pxcli --config <path>\n");
    return 1;
  }

  px_set_log(&OnLog);

  int rc = px_start(config_path.c_str());
  if (rc != 0) {
    char err[512] = {};
    px_last_error(err, static_cast<int>(sizeof(err)));
    std::fprintf(stderr, "%s\n", err);
    return 1;
  }

  std::printf("running\n");

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
  while (!g_stop) {
    Sleep(200);
  }

  px_stop();
  return 0;
}
