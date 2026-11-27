#include "mini/core/Log.h"
#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace mini {

static void logTo(const char* level, const std::string& msg) {
#if defined(__ANDROID__)
    int prio = level[0] == 'E' ? ANDROID_LOG_ERROR
               : level[0] == 'W' ? ANDROID_LOG_WARN : ANDROID_LOG_INFO;
    __android_log_print(prio, "MiniEngine", "%s", msg.c_str());
#else
    std::fprintf(stderr, "[MiniEngine][%s] %s\n", level, msg.c_str());
#endif
}

void logInfo(const std::string& msg)  { logTo("INFO", msg); }
void logWarn(const std::string& msg)  { logTo("WARN", msg); }
void logError(const std::string& msg) { logTo("ERROR", msg); }

} // namespace mini
