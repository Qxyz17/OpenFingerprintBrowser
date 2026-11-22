#include <windows.h>

#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#include "config.h"
#include "engine.h"

namespace {

volatile bool g_stop = false;

void OnSignal(int) { g_stop = true; }

void PrintUsage() {
  std::cerr << "proxifier --config <path>\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string config_path;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    }
  }

  if (config_path.empty()) {
    PrintUsage();
    return 1;
  }

  Config config;
  std::string error;
  if (!Config::Load(config_path, &config, &error)) {
    std::cerr << error << "\n";
    return 1;
  }

  std::vector<uint32_t> pids = CollectTargetPids(config);
  if (pids.empty()) {
    std::cerr << "未匹配到目标进程\n";
    return 1;
  }

  Engine engine(config, pids);
  if (!engine.Start(&error)) {
    std::cerr << error << "\n";
    return 1;
  }

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  std::thread stopper([] {
    while (!g_stop) {
      Sleep(200);
    }
  });

  engine.Run();
  g_stop = true;
  stopper.join();
  engine.Stop();
  return 0;
}
