#pragma once

#include <CLI/CLI.hpp>

#include <string>
#include <vector>

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
    CLI::App *create = nullptr;
    CLI::App *add = nullptr;
    CLI::App *set = nullptr;
    CLI::App *remove = nullptr;
    CLI::App *view = nullptr;
    CLI::App *batch = nullptr;
    CLI::App *validate = nullptr;
    CLI::App *move = nullptr;
    CLI::App *swap = nullptr;
    CLI::App *help = nullptr;

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

    std::string edit_file;
    std::string edit_parent = "/";
    std::string edit_type;
    std::string edit_node = "/";
    std::string edit_mode;
    std::string edit_find;
    std::string edit_replace;
    std::string edit_after;
    std::string edit_before;
    std::string edit_to;
    std::string edit_path2;
    std::string edit_commands;
    std::string edit_input;
    std::string help_topic;
    std::vector<std::string> edit_props;
    int edit_index = -1;
    bool edit_stop = false;
    bool json_out = false;

    bool parsed() const;
    int run() const;
};

OfficeCliCommands registerOffice(CLI::App &app);

} // namespace office
