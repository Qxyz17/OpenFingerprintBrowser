#pragma once

#include "engine.h"

#if defined(_WIN32)
#define PX_EXPORT extern "C" __declspec(dllexport)
#else
#define PX_EXPORT extern "C"
#endif

// 启动拦截，同步完成驱动打开，流量处理在内部线程里跑。
// 返回 0 表示已启动，非 0 为错误码，具体原因用 px_last_error 取。
PX_EXPORT int px_start(const char* config_path);

// 停止拦截并回收监听
PX_EXPORT void px_stop();

PX_EXPORT int px_is_running();

// 取最近一次错误文本，返回实际写入长度
PX_EXPORT int px_last_error(char* buffer, int buffer_len);

// 注册日志回调，回调在引擎线程里调用
PX_EXPORT void px_set_log(LogSink sink);
