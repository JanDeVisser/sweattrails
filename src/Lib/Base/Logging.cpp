/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctime>
#include <mutex>

#include <Logging.h>
#include <Options.h>

namespace ST {

std::mutex g_logging_mutex;

char const *LogLevel_name(LogLevel level)
{
    switch (level) {
#undef S
#define S(level, cardinal) \
    case LogLevel::level:  \
        return #level;
        ENUMERATE_LOG_LEVELS(S)
#undef S
    default:
        fatal("Unreachable {}", "LogLevel_name");
    }
}

std::optional<LogLevel> LogLevel_by_name(std::string_view const &name)
{
#undef S
#define S(level, cardinal)      \
    if (name == #level) {       \
        return LogLevel::level; \
    }
    ENUMERATE_LOG_LEVELS(S)
#undef S
    return { };
}

LogCategory::LogCategory(std::string_view name) noexcept
    : name(name)
{
}

std::clock_t LogCategory::start()
{
    return std::clock();
}

Logger::Logger()
{
    auto add_category = [this](std::string_view const &category) {
        if (category == "all") {
            m_all_enabled = true;
        } else {
            m_categories.insert(category);
        }
    };
    // "Garmin,Wahoo": any of ";,:" separates them. The views are into the
    // environment or the options, which live as long as the program.
    auto add_categories = [add_category](std::string_view categories) {
        while (!categories.empty()) {
            auto const sep = categories.find_first_of(";,:");
            if (auto const category = categories.substr(0, sep); !category.empty()) {
                add_category(category);
            }
            categories = (sep == std::string_view::npos) ? std::string_view { } : categories.substr(sep + 1);
        }
    };
    auto set_level = [this](std::string_view level) {
        auto lvl = LogLevel_by_name(level);
        if (lvl.has_value()) {
            m_level = lvl.value();
        }
    };
    if (auto logfile = get_option("logfile"); logfile.has_value()) {
        m_logfile = logfile.value();
    } else if (auto env_logfile = getenv("LOGFILE"); env_logfile) {
        m_logfile = env_logfile;
    }
    if (auto loglevel = get_option("loglevel"); loglevel.has_value()) {
        set_level(loglevel.value());
    } else if (auto env_level = getenv("LOGLEVEL"); env_level && *env_level) {
        set_level(env_level);
    }
    auto categories = get_option_values("trace");
    for (auto cat : categories) {
        if (cat == "true") {
            cat = "all";
        }
        add_categories(cat);
    }
    if (auto env_trace = getenv("TRACE"); env_trace != nullptr) {
        add_categories(env_trace);
    }
}

Logger &Logger::get_logger()
{
    static Logger                    *logger = nullptr;
    std::lock_guard<std::mutex> const lock(g_logging_mutex);
    if (!logger)
        logger = new Logger();
    return *logger;
}

}
