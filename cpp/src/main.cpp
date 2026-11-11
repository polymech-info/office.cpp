#include <iostream>
#include <string>
#include <sstream>

#include <CLI/CLI.hpp>
#include <toml++/toml.hpp>

#include "logger/logger.h"
#include "json/json.h"
#include "html/html.h"
#include "ipc/ipc.h"
#include "lib/office/main_office_cli.h"
#include "sys_metrics.h"

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "0.1.0"
#endif

int main(int argc, char *argv[]) {
    CLI::App app{"pm-office — OOXML create, read, and edit", "pm-office"};
    app.allow_windows_style_options(false);
    app.set_version_flag("-v,--version", PROJECT_VERSION);

    std::string log_level = "info";
    app.add_option("--log-level", log_level, "Set log level (debug/info/warn/error)")
        ->default_val("info");

    // Subcommand: hello - Simple greeting
    std::string name = "World";
    auto *hello_cmd = app.add_subcommand("hello", "Print a greeting");
    hello_cmd->add_option("-n,--name", name, "Name to greet")->default_val("World");

    // Subcommand: config - Read TOML file
    std::string config_path;
    auto *config_cmd = app.add_subcommand("config", "Read and display a TOML config file");
    config_cmd->add_option("file", config_path, "Path to TOML file")->required();

    // Subcommand: json - Prettify JSON
    std::string json_input;
    auto *json_cmd = app.add_subcommand("json", "Prettify a JSON string");
    json_cmd->add_option("input", json_input, "JSON string to prettify")->required();

    // Subcommand: html - Parse HTML
    std::string html_input;
    auto *html_cmd = app.add_subcommand("html", "Parse HTML and show elements");
    html_cmd->add_option("input", html_input, "HTML string to parse")->required();

    // Subcommand: html-select - CSS select from HTML
    std::string html_select_input;
    std::string css_selector;
    auto *html_select_cmd = app.add_subcommand("html-select", "CSS-select elements from HTML");
    html_select_cmd->add_option("html", html_select_input, "HTML string")->required();
    html_select_cmd->add_option("selector", css_selector, "CSS selector")->required();

    // Subcommand: metrics - Show system metrics
    auto *metrics_cmd = app.add_subcommand("metrics", "Show system metrics");

    // Subcommand: worker - IPC mode
    auto *worker_cmd = app.add_subcommand("worker", "Run in IPC worker mode (stdin/stdout)");

    auto office_commands = office::registerOffice(app);

    CLI11_PARSE(app, argc, argv);

    // Initialize logger
    logger::init("pm-office", log_level);

    if (hello_cmd->parsed()) {
        logger::info("Hello, " + name + "!");
        std::cout << "Hello, " << name << "!" << std::endl;
        return 0;
    }

    if (config_cmd->parsed()) {
        try {
            auto tbl = toml::parse_file(config_path);
            logger::info("Loaded config: " + config_path);
            std::cout << tbl << "\n";
        } catch (const toml::parse_error &err) {
            logger::error("TOML parse error: " + std::string(err.what()));
            return 1;
        }
        return 0;
    }

    if (json_cmd->parsed()) {
        if (!json::is_valid(json_input)) {
            logger::error("Invalid JSON input");
            return 1;
        }
        std::cout << json::prettify(json_input) << "\n";
        return 0;
    }

    if (html_cmd->parsed()) {
        auto elements = html::parse(html_input);
        logger::info("Parsed " + std::to_string(elements.size()) + " elements");
        for (const auto &el : elements) {
            std::cout << "<" << el.tag << "> " << el.text << "\n";
        }
        return 0;
    }

    if (html_select_cmd->parsed()) {
        auto matches = html::select(html_select_input, css_selector);
        logger::info("Matched " + std::to_string(matches.size()) + " elements");
        for (const auto &m : matches) {
            std::cout << m << "\n";
        }
        return 0;
    }

    if (metrics_cmd->parsed()) {
        auto rss = polymech::get_current_rss_mb();
        auto cpu_time = polymech::get_cpu_time_ms();
        std::cout << "RSS: " << rss << " MB\n";
        std::cout << "CPU time: " << cpu_time << " ms\n";
        return 0;
    }

    if (worker_cmd->parsed()) {
        logger::info("Worker mode: listening on stdin");
        
        // Send ready message
        ipc::write_message({"0", "ready", "{}"});
        
        while (true) {
            ipc::Message req;
            if (!ipc::read_message(req)) {
                logger::info("Worker: stdin closed, exiting");
                break;
            }

            logger::debug("Worker recv: type=" + req.type + " id=" + req.id);

            if (req.type == "ping") {
                ipc::write_message({req.id, "pong", "{}"});
            } else if (req.type == "shutdown") {
                ipc::write_message({req.id, "shutdown_ack", "{}"});
                logger::info("Worker: shutdown requested, exiting");
                break;
            } else {
                // Echo back as job_result for unknown types
                ipc::write_message({req.id, "job_result", req.payload});
            }
        }
        return 0;
    }

    if (office_commands.parsed())
        return office_commands.run();

    // No subcommand - show help
    std::cout << app.help() << "\n";
    return 0;
}
