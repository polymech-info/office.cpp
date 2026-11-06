#pragma once

#include <CLI/CLI.hpp>
#include <string>

namespace office {

struct OfficeCliCommands {
    CLI::App *root = nullptr;
    CLI::App *get = nullptr;
    CLI::App *query = nullptr;
    CLI::App *parse_path = nullptr;
    CLI::App *parse_selector = nullptr;
    CLI::App *package_list = nullptr;
    CLI::App *package_read = nullptr;
    CLI::App *xlsx_info = nullptr;

    std::string get_file;
    std::string get_path = "/";
    int get_depth = 1;

    std::string query_file;
    std::string selector;

    std::string parse_path_input;
    std::string parse_path_format = "auto";
    std::string parse_selector_input;
    std::string package_file;
    std::string package_part;
    std::string xlsx_info_file;

    bool parsed() const;
    int run() const;
};

OfficeCliCommands registerOffice(CLI::App &app);

}
