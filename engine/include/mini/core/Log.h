#pragma once
#include <cstdio>
#include <string>

namespace mini {

// Minimal logging facade. On Android this forwards to __android_log_print
// (see Log.cpp); on desktop it prints to stderr.
void logInfo(const std::string& msg);
void logWarn(const std::string& msg);
void logError(const std::string& msg);

} // namespace mini
