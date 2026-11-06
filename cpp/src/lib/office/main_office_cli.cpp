#include "main_office_cli.h"

#include <cctype>
#include <iostream>
#include <string>

#include "logger/logger.h"
#include "office_parser.h"
#include "word_resolver.h"
#include "xlsx_package.h"
#include "xlsx_resolver.h"
#include "xlsx_workbook.h"

namespace office {
namespace {

bool endsWithInsensitive(const std::string &value, const std::string &suffix) {
    if (suffix.size() > value.size()) return false;
    const auto offset = value.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const auto a = static_cast<unsigned char>(value[offset + i]);
        const auto b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

} // namespace

bool OfficeCliCommands::parsed() const {
    return root != nullptr && root->parsed();
}

int OfficeCliCommands::run() const {
    if (parse_path != nullptr && parse_path->parsed()) {
        auto result = parser::parsePathToJson(parse_path_input, parse_path_format);
        std::cout << result.json << "\n";
        return result.ok ? 0 : 1;
    }

    if (parse_selector != nullptr && parse_selector->parsed()) {
        auto result = parser::parseSelectorToJson(parse_selector_input);
        std::cout << result.json << "\n";
        return result.ok ? 0 : 1;
    }

    if (package_list != nullptr && package_list->parsed()) {
        try {
            XlsxPackage package(package_file);
            for (const auto &part : package.listParts()) {
                std::cout << part << "\n";
            }
            return 0;
        } catch (const std::exception &e) {
            std::cerr << "office package-list failed: " << e.what() << "\n";
            return 1;
        }
    }

    if (package_read != nullptr && package_read->parsed()) {
        try {
            XlsxPackage package(package_file);
            auto text = package.readText(package_part);
            if (!text.has_value()) {
                std::cerr << "office package-read failed: part not found: " << package_part << "\n";
                return 1;
            }
            std::cout << *text;
            return 0;
        } catch (const std::exception &e) {
            std::cerr << "office package-read failed: " << e.what() << "\n";
            return 1;
        }
    }

    if (xlsx_info != nullptr && xlsx_info->parsed()) {
        XlsxPackage package(xlsx_info_file);
        auto result = loadWorkbook(package);
        if (!result.ok) {
            std::cerr << "office xlsx-info failed: " << result.error << "\n";
            return 1;
        }
        std::cout << toJson(result.workbook) << "\n";
        return 0;
    }

    if (get != nullptr && get->parsed()) {
        const bool is_docx = endsWithInsensitive(get_file, ".docx");
        const bool is_xlsx = endsWithInsensitive(get_file, ".xlsx");
        auto parsed_path = parser::parsePath(get_path, is_docx ? "docx" : "xlsx");
        if (!parsed_path.ok) {
            std::cerr << "office get parse failed: " << parsed_path.error << "\n";
            return 1;
        }
        XlsxPackage package(get_file);
        ResolveResult result;
        if (is_docx) {
            result = resolveWordGet(package, parsed_path.ast, get_depth);
        } else if (is_xlsx) {
            result = resolveXlsxGet(package, parsed_path.ast, get_depth);
        } else {
            std::cerr << "office get failed: unsupported document extension\n";
            return 1;
        }
        if (!result.ok) {
            std::cerr << "office get failed: " << result.error << "\n";
            return 1;
        }
        std::cout << toJson(result.node) << "\n";
        return 0;
    }

    if (query != nullptr && query->parsed()) {
        const bool is_docx = endsWithInsensitive(query_file, ".docx");
        const bool is_xlsx = endsWithInsensitive(query_file, ".xlsx");
        auto parsed_selector = parser::parseSelector(selector);
        if (!parsed_selector.ok) {
            std::cerr << "office query parse failed: " << parsed_selector.error << "\n";
            return 1;
        }
        XlsxPackage package(query_file);
        ResolveResult result;
        if (is_docx) {
            result = resolveWordQuery(package, parsed_selector.ast);
        } else if (is_xlsx) {
            result = resolveXlsxQuery(package, parsed_selector.ast);
        } else {
            std::cerr << "office query failed: unsupported document extension\n";
            return 1;
        }
        if (!result.ok) {
            std::cerr << "office query failed: " << result.error << "\n";
            return 1;
        }
        std::cout << toJson(result.node) << "\n";
        return 0;
    }

    if (root != nullptr) {
        std::cout << root->help() << "\n";
    }
    return 0;
}

OfficeCliCommands registerOffice(CLI::App &app) {
    OfficeCliCommands commands;
    commands.root = app.add_subcommand("office", "Office document parsing experiments");

    commands.get = commands.root->add_subcommand("get", "Parse and resolve an OfficeCLI get path");
    commands.get->add_option("file", commands.get_file, "Office document path")->required();
    commands.get->add_option("path", commands.get_path, "OfficeCLI path")->default_val("/");
    commands.get->add_option("--depth", commands.get_depth, "Child depth to include")->default_val(1);

    commands.query = commands.root->add_subcommand("query", "Parse and resolve an OfficeCLI selector");
    commands.query->add_option("file", commands.query_file, "Office document path")->required();
    commands.query->add_option("selector", commands.selector, "OfficeCLI selector")->required();

    commands.parse_path = commands.root->add_subcommand("parse-path", "Parse an OfficeCLI get path and print AST JSON");
    commands.parse_path->add_option("input", commands.parse_path_input, "OfficeCLI path or XLSX-native reference")->required();
    commands.parse_path->add_option("--format", commands.parse_path_format, "Input format hint (auto/xlsx/docx)")->default_val("auto");

    commands.parse_selector = commands.root->add_subcommand("parse-selector", "Parse an OfficeCLI query selector and print AST JSON");
    commands.parse_selector->add_option("input", commands.parse_selector_input, "OfficeCLI selector")->required();

    commands.package_list = commands.root->add_subcommand("package-list", "List parts in an OOXML zip package");
    commands.package_list->add_option("file", commands.package_file, "OOXML package path")->required();

    commands.package_read = commands.root->add_subcommand("package-read", "Read a text part from an OOXML zip package");
    commands.package_read->add_option("file", commands.package_file, "OOXML package path")->required();
    commands.package_read->add_option("part", commands.package_part, "Part name such as xl/workbook.xml")->required();

    commands.xlsx_info = commands.root->add_subcommand("xlsx-info", "Read XLSX workbook sheet/shared-string index as JSON");
    commands.xlsx_info->add_option("file", commands.xlsx_info_file, "XLSX package path")->required();

    return commands;
}

}
