#pragma once

#include <string>

namespace logger {

/// Initialize the default logger (call once at startup).
void init(const std::string &app_name = "app", const std::string &log_level = "info");

/// Log at various levels.
void info(const std::string &msg);
void warn(const std::string &msg);
void error(const std::string &msg);
void debug(const std::string &msg);

} // namespace logger
