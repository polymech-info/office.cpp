#include "main_office_cli.h"

#include "office_util.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "logger/logger.h"
#include "office_edit.h"
#include "office_parser.h"
#include "word_resolver.h"
#include "xlsx_package.h"
#include "xlsx_resolver.h"
#include "xlsx_workbook.h"

namespace office {
namespace {

PropMap propsFromPairs(const std::vector<std::string> &pairs, std::string &error) {
    PropMap props;
    for (const auto &pair : pairs) {
        const auto eq = pair.find('=');
        if (eq == std::string::npos) {
            error = "--prop must be key=value: " + pair;
            return {};
        }
        props[pair.substr(0, eq)] = pair.substr(eq + 1);
    }
    return props;
}

int printEdit(const EditResult &result) {
    if (!result.ok) {
        std::cerr << result.error << "\n";
        if (!result.json.empty()) std::cout << result.json << "\n";
        return result.exit_code == 0 ? 1 : result.exit_code;
    }
    if (result.plain) {
        std::cout << result.text;
        if (result.text.empty() || result.text.back() != '\n') std::cout << "\n";
        return 0;
    }
    std::cout << result.json << "\n";
    return 0;
}

const char *editHelp(const std::string &topic) {
    if (topic == "docx") {
        return "docx add types: paragraph, run, table, row, cell\n"
               "set props: text, bold, italic, style, color, size, font\n"
               "paths: /body/p[1], /body/p[1]/r[1], /body/tbl[1]/tr[1]/tc[1]\n";
    }
    if (topic == "xlsx") {
        return "xlsx add types: sheet, row, cell\n"
               "set props: value, bold, italic\n"
               "paths: Sheet1!A1, /Sheet1/A1, /sheet[1]/cell[@ref=A1]\n"
               "row shorthand: --prop c1=Alice --prop c2=10\n";
    }
    if (topic == "pptx") {
        return "pptx add types: slide, shape, picture\n"
               "slide props: title, background, transition\n"
               "shape props: text, x, y, w, h, font, size, color, fill, preset, animation\n"
               "picture props: src, x, y, w, h, name, alt\n"
               "animation: fade-entrance-500, fly-entrance-left-600, zoom-exit-400, none\n"
               "paths: /slide[1], /slide[1]/shape[1], /slide[1]/picture[1]\n";
    }
    return "pm-office edit commands: create, add, set, remove, move, swap, view, batch, validate, get, query\n"
           "formats: docx, xlsx, pptx; odt, ods, odp are read-only (view markdown)\n"
           "view modes: text, outline, stats, markdown\n"
           "charts, pivots, raw XML, morph, emphasis animations, and HTML preview are not in this build\n"
           "try: pm-office office help docx|xlsx|pptx\n";
}

} // namespace

bool OfficeCliCommands::parsed() const {
    return root != nullptr && root->parsed();
}

int OfficeCliCommands::run() const {
    if (help != nullptr && help->parsed()) {
        std::cout << editHelp(help_topic);
        return 0;
    }
    if (create != nullptr && create->parsed()) return printEdit(createDocument(edit_file));
    if (validate != nullptr && validate->parsed()) return printEdit(validateDocument(edit_file));
    if (view != nullptr && view->parsed()) return printEdit(viewDocument(edit_file, edit_mode));
    std::string prop_error;
    const auto props = propsFromPairs(edit_props, prop_error);
    if (!prop_error.empty() && ((add && add->parsed()) || (set && set->parsed()) || (batch && batch->parsed()))) {
        std::cerr << prop_error << "\n";
        return 2;
    }
    if (add != nullptr && add->parsed()) {
        return printEdit(addElement(edit_file, edit_parent, edit_type, props, edit_after, edit_before, edit_index));
    }
    if (set != nullptr && set->parsed()) {
        return printEdit(setElement(edit_file, edit_node, props, edit_find, edit_replace));
    }
    if (remove != nullptr && remove->parsed()) return printEdit(removeElement(edit_file, edit_node));
    if (move != nullptr && move->parsed()) {
        return printEdit(moveElement(edit_file, edit_node, edit_to, edit_after, edit_before, edit_index));
    }
    if (swap != nullptr && swap->parsed()) return printEdit(swapElements(edit_file, edit_node, edit_path2));
    if (batch != nullptr && batch->parsed()) {
        std::string json = edit_commands;
        if (!edit_input.empty()) {
            std::ifstream in(edit_input);
            if (!in) {
                std::cerr << "failed to read batch file: " << edit_input << "\n";
                return 1;
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            json = buffer.str();
        }
        if (json.empty()) {
            std::cerr << "batch needs --commands or --input\n";
            return 2;
        }
        return printEdit(runBatchJson(edit_file, json, edit_stop));
    }

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
        const bool is_docx = endsWithCi(get_file, ".docx");
        const bool is_xlsx = endsWithCi(get_file, ".xlsx");
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
        } else if (endsWithCi(get_file, ".pptx") || endsWithCi(get_file, ".odt")
                   || endsWithCi(get_file, ".ods") || endsWithCi(get_file, ".odp")) {
            return printEdit(documentGet(get_file, get_path, get_depth));
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
        const bool is_docx = endsWithCi(query_file, ".docx");
        const bool is_xlsx = endsWithCi(query_file, ".xlsx");
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
        } else if (endsWithCi(query_file, ".pptx") || endsWithCi(query_file, ".odt")
                   || endsWithCi(query_file, ".ods") || endsWithCi(query_file, ".odp")) {
            return printEdit(documentQuery(query_file, selector));
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

    commands.create = commands.root->add_subcommand("create", "Create a blank .docx, .xlsx, or .pptx");
    commands.create->add_option("file", commands.edit_file, "Document path")->required();

    commands.add = commands.root->add_subcommand("add", "Add a paragraph, run, table, sheet, row, cell, slide, or shape");
    commands.add->add_option("file", commands.edit_file, "Document path")->required();
    commands.add->add_option("parent", commands.edit_parent, "Parent path")->default_val("/");
    commands.add->add_option("--type", commands.edit_type, "Element type")->required();
    commands.add->add_option("--prop", commands.edit_props, "key=value property");
    commands.add->add_option("--after", commands.edit_after, "Insert after this path");
    commands.add->add_option("--before", commands.edit_before, "Insert before this path");
    commands.add->add_option("--index", commands.edit_index, "0-based index (1-based for xlsx rows)");

    commands.set = commands.root->add_subcommand("set", "Set properties or find/replace text");
    commands.set->add_option("file", commands.edit_file, "Document path")->required();
    commands.set->add_option("path", commands.edit_node, "Element path")->default_val("/");
    commands.set->add_option("--prop", commands.edit_props, "key=value property");
    commands.set->add_option("--find", commands.edit_find, "Text to find");
    commands.set->add_option("--replace", commands.edit_replace, "Replacement text");

    commands.remove = commands.root->add_subcommand("remove", "Remove an element");
    commands.remove->add_option("file", commands.edit_file, "Document path")->required();
    commands.remove->add_option("path", commands.edit_node, "Element path")->required();

    commands.move = commands.root->add_subcommand("move", "Move a docx paragraph or table");
    commands.move->add_option("file", commands.edit_file, "Document path")->required();
    commands.move->add_option("path", commands.edit_node, "Element path")->required();
    commands.move->add_option("--to", commands.edit_to, "Destination parent");
    commands.move->add_option("--after", commands.edit_after, "Insert after this path");
    commands.move->add_option("--before", commands.edit_before, "Insert before this path");
    commands.move->add_option("--index", commands.edit_index, "0-based destination index");

    commands.swap = commands.root->add_subcommand("swap", "Swap two docx paragraphs or tables");
    commands.swap->add_option("file", commands.edit_file, "Document path")->required();
    commands.swap->add_option("path", commands.edit_node, "First path")->required();
    commands.swap->add_option("path2", commands.edit_path2, "Second path")->required();

    commands.view = commands.root->add_subcommand("view", "View text, outline, or stats");
    commands.view->add_option("file", commands.edit_file, "Document path")->required();
    commands.view->add_option("mode", commands.edit_mode, "text, outline, or stats")->required();

    commands.batch = commands.root->add_subcommand("batch", "Run add/set/remove commands from JSON");
    commands.batch->add_option("file", commands.edit_file, "Document path")->required();
    commands.batch->add_option("--commands", commands.edit_commands, "JSON command array");
    commands.batch->add_option("--input", commands.edit_input, "JSON file");
    commands.batch->add_flag("--stop-on-error", commands.edit_stop, "Stop after the first failure");

    commands.validate = commands.root->add_subcommand("validate", "Check that the package opens and its main part parses");
    commands.validate->add_option("file", commands.edit_file, "Document path")->required();

    commands.help = commands.root->add_subcommand("help", "Show the C++ edit surface");
    commands.help->add_option("topic", commands.help_topic, "docx, xlsx, or pptx");

    for (CLI::App *cmd : {commands.get, commands.query, commands.create, commands.add, commands.set,
                          commands.remove, commands.move, commands.swap, commands.view, commands.batch,
                          commands.validate}) {
        cmd->add_flag("--json", commands.json_out, "JSON output (already the default for edit commands)");
    }

    return commands;
}

}
