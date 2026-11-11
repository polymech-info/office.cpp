#include "office_markdown.h"

#include "office_edit.h"
#include "office_util.hpp"
#include "office_xml.hpp"
#include "ooxml_zip.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

#include <pugixml.hpp>

namespace office {
namespace {

constexpr int kMaxRepeat = 32;
constexpr int kMaxRows = 500;
constexpr int kMaxCols = 64;

std::string mdInline(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '\r') continue;
        if (c == '\n') out.push_back(' ');
        else if (c == '|') out += "\\|";
        else out.push_back(c);
    }
    return out;
}

std::string colName(int index) {
    std::string name;
    int value = index;
    while (value > 0) {
        --value;
        name.insert(name.begin(), static_cast<char>('A' + (value % 26)));
        value /= 26;
    }
    return name.empty() ? "A" : name;
}

int parseRepeat(pugi::xml_node node, const char *name) {
    const auto text = attrByLocalName(node, name);
    if (text.empty()) return 1;
    try {
        const int value = std::stoi(text);
        if (value < 1) return 1;
        return (std::min)(value, kMaxRepeat);
    } catch (...) {
        return 1;
    }
}

void appendPcdata(pugi::xml_node node, std::string &out) {
    for (auto child : node.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            out += child.value();
            continue;
        }
        if (localNameEquals(child.name(), "instrText") || localNameEquals(child.name(), "delInstrText")) {
            continue;
        }
        if (localNameEquals(child.name(), "a")) {
            const auto href = attrByLocalName(child, "href");
            std::string label;
            appendPcdata(child, label);
            if (href.empty() || href.rfind("javascript:", 0) == 0) {
                out += label;
                continue;
            }
            std::string safeHref;
            safeHref.reserve(href.size());
            for (unsigned char c : href) {
                if (c == '(') safeHref += "%28";
                else if (c == ')') safeHref += "%29";
                else if (c == ' ' || c == '\n' || c == '\r') safeHref += "%20";
                else safeHref.push_back(static_cast<char>(c));
            }
            out.push_back('[');
            for (char c : label) {
                if (c == '\n' || c == '\r') out.push_back(' ');
                else if (c != '[' && c != ']') out.push_back(c);
            }
            out += "](";
            out += safeHref;
            out.push_back(')');
            continue;
        }
        if (localNameEquals(child.name(), "tab")) {
            out.push_back('\t');
            continue;
        }
        if (localNameEquals(child.name(), "br") || localNameEquals(child.name(), "line-break")) {
            out.push_back('\n');
            continue;
        }
        if (localNameEquals(child.name(), "s")) {
            int count = 1;
            const auto c = attrByLocalName(child, "c");
            if (!c.empty()) {
                try { count = (std::max)(1, std::stoi(c)); } catch (...) { count = 1; }
            }
            out.append(static_cast<std::size_t>((std::min)(count, 40)), ' ');
            continue;
        }
        appendPcdata(child, out);
    }
}

std::string trimmed(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    return text;
}

struct Writer {
    std::ostringstream out;
    bool fresh = true;

    void gap() {
        if (fresh) return;
        out << "\n\n";
        fresh = true;
    }

    void paragraph(const std::string &text) {
        const auto body = trimmed(text);
        if (body.empty()) return;
        gap();
        out << body;
        fresh = false;
    }

    void heading(int level, const std::string &text) {
        const auto body = trimmed(text);
        if (body.empty()) return;
        gap();
        const int clamped = (std::max)(1, (std::min)(level, 6));
        out << std::string(static_cast<std::size_t>(clamped), '#') << ' ' << body;
        fresh = false;
    }

    void bullet(const std::string &text) {
        const auto body = trimmed(text);
        if (body.empty()) return;
        if (fresh) gap();
        else out << '\n';
        out << "- " << body;
        fresh = false;
    }

    void table(const std::vector<std::vector<std::string>> &rows) {
        if (rows.empty()) return;
        std::size_t width = 0;
        for (const auto &row : rows) width = (std::max)(width, row.size());
        if (width == 0) return;
        gap();
        auto line = [&](const std::vector<std::string> &row, bool rule) {
            out << '|';
            for (std::size_t i = 0; i < width; ++i) {
                out << ' ';
                if (rule) out << "---";
                else out << mdInline(i < row.size() ? row[i] : "");
                out << " |";
            }
            out << '\n';
        };
        line(rows.front(), false);
        line({}, true);
        for (std::size_t i = 1; i < rows.size(); ++i) line(rows[i], false);
        fresh = false;
    }

    std::string str() const { return out.str(); }
};

bool toggleOn(pugi::xml_node props, const char *name) {
    auto node = childByLocalName(props, name);
    if (!node) return false;
    const auto val = lower(attrByLocalName(node, "val"));
    return val != "0" && val != "false" && val != "off" && val != "none";
}

std::string docxRunText(pugi::xml_node run) {
    std::string text;
    appendPcdata(run, text);
    if (trimmed(text).empty()) return {};
    const auto props = childByLocalName(run, "rPr");
    const bool bold = toggleOn(props, "b") || toggleOn(props, "bCs");
    const bool italic = toggleOn(props, "i") || toggleOn(props, "iCs");
    if (italic) text = "*" + text + "*";
    if (bold) text = "**" + text + "**";
    return text;
}

std::string docxParagraph(pugi::xml_node paragraph) {
    std::string text;
    for (auto child : paragraph.children()) {
        if (localNameEquals(child.name(), "r")) text += docxRunText(child);
        else if (localNameEquals(child.name(), "hyperlink")) {
            for (auto run : child.children()) {
                if (localNameEquals(run.name(), "r")) text += docxRunText(run);
            }
        }
    }
    return text;
}

int docxHeading(pugi::xml_node paragraph) {
    const auto style = lower(attrByLocalName(childByLocalName(childByLocalName(paragraph, "pPr"), "pStyle"), "val"));
    if (style == "title") return 1;
    if (style.rfind("heading", 0) != 0) return 0;
    int level = 1;
    for (char c : style) {
        if (c >= '1' && c <= '6') level = c - '0';
    }
    return level;
}

std::vector<std::vector<std::string>> docxTable(pugi::xml_node table) {
    std::vector<std::vector<std::string>> rows;
    for (auto row : table.children()) {
        if (!localNameEquals(row.name(), "tr")) continue;
        std::vector<std::string> cells;
        for (auto cell : row.children()) {
            if (!localNameEquals(cell.name(), "tc")) continue;
            std::string text;
            for (auto paragraph : cell.children()) {
                if (!localNameEquals(paragraph.name(), "p")) continue;
                if (!text.empty()) text.push_back(' ');
                text += trimmed(docxParagraph(paragraph));
            }
            cells.push_back(text);
        }
        if (!cells.empty()) rows.push_back(std::move(cells));
    }
    return rows;
}

void renderDocx(pugi::xml_node body, Writer &writer) {
    for (auto child : body.children()) {
        if (localNameEquals(child.name(), "p")) {
            const int level = docxHeading(child);
            if (level > 0) writer.heading(level, docxParagraph(child));
            else writer.paragraph(docxParagraph(child));
        } else if (localNameEquals(child.name(), "tbl")) {
            writer.table(docxTable(child));
        }
    }
}

std::vector<std::string> sharedStrings(const OoxmlParts &package) {
    std::vector<std::string> strings;
    auto found = package.parts.find("xl/sharedStrings.xml");
    if (found == package.parts.end()) return strings;
    pugi::xml_document doc;
    if (!doc.load_string(found->second.c_str())) return strings;
    for (auto item : doc.document_element().children()) {
        if (!localNameEquals(item.name(), "si")) continue;
        std::string text;
        appendPcdata(item, text);
        strings.push_back(text);
    }
    return strings;
}

std::string xlsxCellValue(pugi::xml_node cell, const std::vector<std::string> &strings) {
    if (attrByLocalName(cell, "t") == "inlineStr") {
        std::string text;
        appendPcdata(cell, text);
        return text;
    }
    if (attrByLocalName(cell, "t") == "s") {
        const auto index = childByLocalName(cell, "v").text().as_int(-1);
        if (index >= 0 && static_cast<std::size_t>(index) < strings.size()) return strings[static_cast<std::size_t>(index)];
        return {};
    }
    auto formula = childByLocalName(cell, "f");
    auto value = childByLocalName(cell, "v");
    if (value) return value.text().get();
    if (formula) return std::string("=") + formula.text().get();
    return {};
}

std::string relTarget(const OoxmlParts &package, const std::string &rels, const std::string &id) {
    auto found = package.parts.find(rels);
    if (found == package.parts.end()) return {};
    pugi::xml_document doc;
    if (!doc.load_string(found->second.c_str())) return {};
    for (auto rel : doc.document_element().children()) {
        if (!localNameEquals(rel.name(), "Relationship")) continue;
        if (attrByLocalName(rel, "Id") != id) continue;
        auto target = attrByLocalName(rel, "Target");
        if (target.rfind("/", 0) == 0) return target.substr(1);
        if (target.rfind("xl/", 0) == 0) return target;
        return "xl/" + target;
    }
    return {};
}

void renderXlsx(const OoxmlParts &package, Writer &writer, std::vector<MarkdownCell> &cells, std::string &error) {
    auto book = package.parts.find("xl/workbook.xml");
    if (book == package.parts.end()) {
        error = "missing xl/workbook.xml";
        return;
    }
    pugi::xml_document workbook;
    if (!workbook.load_string(book->second.c_str())) {
        error = "failed to parse xl/workbook.xml";
        return;
    }
    const auto strings = sharedStrings(package);
    int sheet_index = 0;
    for (auto sheet : workbook.document_element().children()) {
        if (!localNameEquals(sheet.name(), "sheets")) continue;
        for (auto node : sheet.children()) {
            if (!localNameEquals(node.name(), "sheet")) continue;
            if (++sheet_index > 32) return;
            const auto name = attrByLocalName(node, "name");
            std::string rel;
            for (auto attr : node.attributes()) {
                if (localNameEquals(attr.name(), "id") && std::string(attr.name()).find(':') != std::string::npos)
                    rel = attr.value();
            }
            const auto part = relTarget(package, "xl/_rels/workbook.xml.rels", rel);
            auto worksheet_part = package.parts.find(part);
            if (part.empty() || worksheet_part == package.parts.end()) continue;
            pugi::xml_document worksheet;
            if (!worksheet.load_string(worksheet_part->second.c_str())) continue;
            writer.heading(2, name.empty() ? "Sheet" + std::to_string(sheet_index) : name);
            std::vector<std::vector<std::string>> rows;
            int row_count = 0;
            for (auto row : worksheet.document_element().children()) {
                if (!localNameEquals(row.name(), "sheetData")) continue;
                for (auto row_node : row.children()) {
                    if (!localNameEquals(row_node.name(), "row") || ++row_count > kMaxRows) break;
                    const int row_number = row_node.attribute("r") ? row_node.attribute("r").as_int(row_count) : row_count;
                    std::vector<std::string> values;
                    int col = 0;
                    for (auto cell : row_node.children()) {
                        if (!localNameEquals(cell.name(), "c") || ++col > kMaxCols) break;
                        const auto value = xlsxCellValue(cell, strings);
                        auto ref = attrByLocalName(cell, "r");
                        if (ref.empty()) ref = colName(col) + std::to_string(row_number);
                        values.push_back(value);
                        if (!trimmed(value).empty()) cells.push_back({name, ref, value});
                    }
                    if (!values.empty()) rows.push_back(std::move(values));
                }
            }
            if (!rows.empty()) {
                std::vector<std::string> header;
                const auto width = rows.front().size();
                for (std::size_t i = 0; i < width; ++i) header.push_back(colName(static_cast<int>(i) + 1));
                rows.insert(rows.begin(), header);
                writer.table(rows);
            }
        }
    }
}

void renderPptx(const OoxmlParts &package, Writer &writer, std::string &error) {
    auto presentation_part = package.parts.find("ppt/presentation.xml");
    auto rels_part = package.parts.find("ppt/_rels/presentation.xml.rels");
    if (presentation_part == package.parts.end() || rels_part == package.parts.end()) {
        error = "missing ppt/presentation.xml";
        return;
    }
    pugi::xml_document presentation;
    pugi::xml_document rels;
    if (!presentation.load_string(presentation_part->second.c_str()) || !rels.load_string(rels_part->second.c_str())) {
        error = "failed to parse presentation";
        return;
    }
    std::map<std::string, std::string> rel_map;
    for (auto rel : rels.document_element().children()) {
        if (!localNameEquals(rel.name(), "Relationship")) continue;
        auto target = attrByLocalName(rel, "Target");
        if (target.find("slides/") == std::string::npos) continue;
        if (target.rfind("../", 0) == 0) target = target.substr(3);
        if (target.rfind("ppt/", 0) != 0) target = "ppt/" + target;
        rel_map[attrByLocalName(rel, "Id")] = target;
    }
    int index = 0;
    for (auto list : presentation.document_element().children()) {
        if (!localNameEquals(list.name(), "sldIdLst")) continue;
        for (auto id_node : list.children()) {
            if (!localNameEquals(id_node.name(), "sldId")) continue;
            std::string rel;
            for (auto attr : id_node.attributes()) {
                if (localNameEquals(attr.name(), "id") && std::string(attr.name()).find(':') != std::string::npos)
                    rel = attr.value();
            }
            auto part = rel_map.find(rel);
            if (part == rel_map.end()) continue;
            auto slide_part = package.parts.find(part->second);
            if (slide_part == package.parts.end()) continue;
            pugi::xml_document slide;
            if (!slide.load_string(slide_part->second.c_str())) continue;
            writer.heading(2, "Slide " + std::to_string(++index));
            for (auto shape : slide.document_element().children()) {
                std::string text;
                appendPcdata(shape, text);
                writer.paragraph(text);
            }
        }
    }
}

std::string odfText(pugi::xml_node node) {
    std::string text;
    appendPcdata(node, text);
    return text;
}

void renderOdfBlocks(pugi::xml_node parent, Writer &writer) {
    for (auto child : parent.children()) {
        if (localNameEquals(child.name(), "h")) {
            int level = 1;
            try { level = std::stoi(attrByLocalName(child, "outline-level")); } catch (...) { level = 1; }
            writer.heading(level, odfText(child));
        } else if (localNameEquals(child.name(), "p")) {
            writer.paragraph(odfText(child));
        } else if (localNameEquals(child.name(), "list")) {
            for (auto item : child.children()) {
                if (!localNameEquals(item.name(), "list-item")) continue;
                std::string text;
                for (auto paragraph : item.children()) {
                    if (!localNameEquals(paragraph.name(), "p") && !localNameEquals(paragraph.name(), "h")) continue;
                    if (!text.empty()) text.push_back(' ');
                    text += trimmed(odfText(paragraph));
                }
                writer.bullet(text);
            }
        } else if (localNameEquals(child.name(), "section")) {
            renderOdfBlocks(child, writer);
        } else if (localNameEquals(child.name(), "table")) {
            std::vector<std::vector<std::string>> rows;
            int row_count = 0;
            for (auto row : child.children()) {
                if (!localNameEquals(row.name(), "table-row") || ++row_count > kMaxRows) continue;
                const int row_repeat = parseRepeat(row, "number-rows-repeated");
                std::vector<std::string> values;
                int col = 0;
                bool any = false;
                for (auto cell : row.children()) {
                    if (!localNameEquals(cell.name(), "table-cell") && !localNameEquals(cell.name(), "covered-table-cell")) continue;
                    if (++col > kMaxCols) break;
                    std::string text;
                    for (auto paragraph : cell.children()) {
                        if (!localNameEquals(paragraph.name(), "p") && !localNameEquals(paragraph.name(), "h")) continue;
                        if (!text.empty()) text.push_back(' ');
                        text += trimmed(odfText(paragraph));
                    }
                    if (text.empty()) {
                        const auto value = attrByLocalName(cell, "value");
                        if (!value.empty()) text = value;
                    }
                    const int repeat = text.empty() ? 1 : parseRepeat(cell, "number-columns-repeated");
                    for (int n = 0; n < repeat && static_cast<int>(values.size()) < kMaxCols; ++n) values.push_back(text);
                    if (!trimmed(text).empty()) any = true;
                }
                if (!any && row_repeat > 1) continue;
                const int copies = any ? row_repeat : 1;
                for (int n = 0; n < copies && row_count + n <= kMaxRows; ++n) rows.push_back(values);
            }
            writer.table(rows);
        }
    }
}

void renderOds(pugi::xml_node spreadsheet, Writer &writer, std::vector<MarkdownCell> &cells) {
    int sheet_index = 0;
    for (auto table : spreadsheet.children()) {
        if (!localNameEquals(table.name(), "table") || ++sheet_index > 32) continue;
        const auto name = attrByLocalName(table, "name");
        const auto sheet = name.empty() ? "Sheet" + std::to_string(sheet_index) : name;
        writer.heading(2, sheet);
        std::vector<std::vector<std::string>> rows;
        int row_number = 0;
        for (auto row : table.children()) {
            if (!localNameEquals(row.name(), "table-row") || ++row_number > kMaxRows) continue;
            std::vector<std::string> values;
            int col = 0;
            bool any = false;
            for (auto cell : row.children()) {
                if (!localNameEquals(cell.name(), "table-cell")) continue;
                if (++col > kMaxCols) break;
                std::string text;
                for (auto paragraph : cell.children()) {
                    if (!localNameEquals(paragraph.name(), "p")) continue;
                    if (!text.empty()) text.push_back(' ');
                    text += trimmed(odfText(paragraph));
                }
                if (text.empty()) text = attrByLocalName(cell, "value");
                const int repeat = text.empty() ? 1 : parseRepeat(cell, "number-columns-repeated");
                for (int n = 0; n < repeat && col + n - 1 < kMaxCols; ++n) {
                    const auto ref = colName(col + n) + std::to_string(row_number);
                    values.push_back(text);
                    if (!trimmed(text).empty()) {
                        cells.push_back({sheet, ref, text});
                        any = true;
                    }
                }
                col += repeat - 1;
            }
            if (any || !values.empty()) rows.push_back(std::move(values));
        }
        if (!rows.empty()) {
            std::vector<std::string> header;
            for (std::size_t i = 0; i < rows.front().size(); ++i) header.push_back(colName(static_cast<int>(i) + 1));
            rows.insert(rows.begin(), std::move(header));
            writer.table(rows);
        }
    }
}

void renderOdp(pugi::xml_node presentation, Writer &writer) {
    int index = 0;
    for (auto page : presentation.children()) {
        if (!localNameEquals(page.name(), "page")) continue;
        const auto name = attrByLocalName(page, "name");
        writer.heading(2, name.empty() ? "Slide " + std::to_string(++index) : name);
        if (!name.empty()) ++index;
        for (auto frame : page.children()) {
            if (!localNameEquals(frame.name(), "frame") && !localNameEquals(frame.name(), "custom-shape")) continue;
            for (auto box : frame.children()) {
                if (!localNameEquals(box.name(), "text-box") && !localNameEquals(box.name(), "p")) continue;
                if (localNameEquals(box.name(), "p")) writer.paragraph(odfText(box));
                else renderOdfBlocks(box, writer);
            }
        }
    }
}

bool loadXml(const std::string &xml, pugi::xml_document &doc) {
    return static_cast<bool>(doc.load_string(xml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata));
}

} // namespace

MarkdownDocument markdownFromPackage(const OoxmlParts &package, const std::string &format) {
    MarkdownDocument result;
    result.format = format;
    Writer writer;
    if (format == "docx") {
        auto part = package.parts.find("word/document.xml");
        if (part == package.parts.end()) return {false, "missing word/document.xml", format, {}, {}};
        pugi::xml_document doc;
        if (!loadXml(part->second, doc)) return {false, "failed to parse word/document.xml", format, {}, {}};
        auto body = childByLocalName(doc.document_element(), "body");
        if (!body) return {false, "document has no body", format, {}, {}};
        renderDocx(body, writer);
    } else if (format == "xlsx") {
        renderXlsx(package, writer, result.cells, result.error);
        if (!result.error.empty()) return result;
    } else if (format == "pptx") {
        renderPptx(package, writer, result.error);
        if (!result.error.empty()) return result;
    } else {
        auto part = package.parts.find("content.xml");
        if (part == package.parts.end()) return {false, "missing content.xml", format, {}, {}};
        pugi::xml_document doc;
        if (!loadXml(part->second, doc)) return {false, "failed to parse content.xml", format, {}, {}};
        auto body = childByLocalName(doc.document_element(), "body");
        if (!body) return {false, "OpenDocument has no office:body", format, {}, {}};
        if (auto text = childByLocalName(body, "text")) renderOdfBlocks(text, writer);
        else if (auto sheet = childByLocalName(body, "spreadsheet")) renderOds(sheet, writer, result.cells);
        else if (auto deck = childByLocalName(body, "presentation")) renderOdp(deck, writer);
        else return {false, "OpenDocument body is not text, spreadsheet, or presentation", format, {}, {}};
    }
    result.ok = true;
    result.markdown = writer.str();
    return result;
}

MarkdownDocument readMarkdown(const std::filesystem::path &file) {
    const auto format = documentFormat(file);
    if (format.empty()) return {false, "unsupported document extension", {}, {}, {}};
    std::string error;
    const auto package = readOoxmlParts(file, error);
    if (!error.empty()) return {false, error, format, {}, {}};
    return markdownFromPackage(package, format);
}

} // namespace office
