#include "logger/logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace logger {

static void apply_log_level(const std::string& level) {
    if (level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (level == "warn") spdlog::set_level(spdlog::level::warn);
    else if (level == "error") spdlog::set_level(spdlog::level::err);
    else spdlog::set_level(spdlog::level::info);
}

void init(const std::string &app_name, const std::string &log_level) {
    auto console = spdlog::stdout_color_mt(app_name);
    spdlog::set_default_logger(console);
    apply_log_level(log_level);
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
}

void info(const std::string &msg) { spdlog::info(msg); }
void warn(const std::string &msg) { spdlog::warn(msg); }
void error(const std::string &msg) { spdlog::error(msg); }
void debug(const std::string &msg) { spdlog::debug(msg); }

} // namespace logger
