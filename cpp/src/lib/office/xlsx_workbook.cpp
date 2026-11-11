#include "xlsx_workbook.h"

#include "office_xml.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

#include <pugixml.hpp>

namespace office {
namespace {

pugi::xml_document parseXmlPart(const XlsxPackage &package, std::string_view part_name, std::string &error) {
    pugi::xml_document doc;
    auto xml = package.readText(part_name);
    if (!xml.has_value()) {
        error = "missing required XLSX part: " + std::string(part_name);
        return doc;
    }
    auto parsed = doc.load_string(xml->c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
    if (!parsed) {
        error = "failed to parse " + std::string(part_name) + ": " + parsed.description();
        return {};
    }
    return doc;
}

std::string normalizePartPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }

    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        auto slash = path.find('/', start);
        auto end = slash == std::string::npos ? path.size() : slash;
        auto part = path.substr(start, end - start);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (!parts.empty()) parts.pop_back();
            } else {
                parts.push_back(std::move(part));
            }
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }

    std::ostringstream out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out << "/";
        out << parts[i];
    }
    return out.str();
}

std::string workbookRelationshipTargetToPart(std::string_view target) {
    if (target.empty()) return {};
    if (target.front() == '/') {
        return normalizePartPath(std::string(target));
    }
    return normalizePartPath("xl/" + std::string(target));
}

std::map<std::string, std::string> loadWorkbookRelationships(const XlsxPackage &package, std::string &error) {
    auto rels_doc = parseXmlPart(package, "xl/_rels/workbook.xml.rels", error);
    if (!error.empty()) return {};

    std::map<std::string, std::string> rels;
    auto root = rels_doc.document_element();
    for (auto rel : root.children()) {
        if (!localNameEquals(rel.name(), "Relationship")) continue;
        auto id = attrByLocalName(rel, "Id");
        auto target = attrByLocalName(rel, "Target");
        if (!id.empty() && !target.empty()) {
            rels[id] = workbookRelationshipTargetToPart(target);
        }
    }
    return rels;
}

void collectTextNodes(pugi::xml_node node, std::ostringstream &out) {
    for (auto child : node.children()) {
        if (localNameEquals(child.name(), "t")) {
            out << child.text().get();
        }
        collectTextNodes(child, out);
    }
}

std::vector<std::string> loadSharedStrings(const XlsxPackage &package, std::string &error) {
    if (!package.hasPart("xl/sharedStrings.xml")) {
        return {};
    }

    auto shared_doc = parseXmlPart(package, "xl/sharedStrings.xml", error);
    if (!error.empty()) return {};

    std::vector<std::string> shared_strings;
    auto root = shared_doc.document_element();
    for (auto si : root.children()) {
        if (!localNameEquals(si.name(), "si")) continue;
        std::ostringstream text;
        collectTextNodes(si, text);
        shared_strings.push_back(text.str());
    }
    return shared_strings;
}

} // namespace

XlsxWorkbookResult loadWorkbook(const XlsxPackage &package) {
    try {
        std::string error;
        auto rels = loadWorkbookRelationships(package, error);
        if (!error.empty()) return {.error = error};

        auto workbook_doc = parseXmlPart(package, "xl/workbook.xml", error);
        if (!error.empty()) return {.error = error};

        XlsxWorkbook workbook;
        auto sheets = childByLocalName(workbook_doc.document_element(), "sheets");
        for (auto sheet_node : sheets.children()) {
            if (!localNameEquals(sheet_node.name(), "sheet")) continue;
            XlsxSheetInfo sheet;
            sheet.name = attrByLocalName(sheet_node, "name");
            sheet.sheet_id = sheet_node.attribute("sheetId").as_int();
            sheet.relationship_id = attrByLocalName(sheet_node, "id");
            auto rel = rels.find(sheet.relationship_id);
            if (rel != rels.end()) {
                sheet.worksheet_part = rel->second;
                sheet.worksheet_part_exists = package.hasPart(sheet.worksheet_part);
            }
            workbook.sheets.push_back(std::move(sheet));
        }

        workbook.shared_strings = loadSharedStrings(package, error);
        if (!error.empty()) return {.error = error};

        return {.ok = true, .workbook = std::move(workbook)};
    } catch (const std::exception &e) {
        return {.error = e.what()};
    }
}

std::string toJson(const XlsxWorkbook &workbook) {
    std::ostringstream out;
    out << "{\"kind\":\"xlsxWorkbook\",\"sheets\":[";
    for (std::size_t i = 0; i < workbook.sheets.size(); ++i) {
        if (i) out << ",";
        const auto &sheet = workbook.sheets[i];
        out << "{\"index\":" << (i + 1)
            << ",\"name\":" << jstr(sheet.name)
            << ",\"sheetId\":" << sheet.sheet_id
            << ",\"relationshipId\":" << jstr(sheet.relationship_id)
            << ",\"worksheetPart\":" << jstr(sheet.worksheet_part)
            << ",\"worksheetPartExists\":" << (sheet.worksheet_part_exists ? "true" : "false")
            << "}";
    }
    out << "],\"sharedStrings\":[";
    for (std::size_t i = 0; i < workbook.shared_strings.size(); ++i) {
        if (i) out << ",";
        out << jstr(workbook.shared_strings[i]);
    }
    out << "],\"sharedStringCount\":" << workbook.shared_strings.size() << "}";
    return out.str();
}

} // namespace office
