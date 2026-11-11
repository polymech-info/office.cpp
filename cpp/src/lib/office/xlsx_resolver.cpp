#include "xlsx_resolver.h"

#include "office_xml.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <pugixml.hpp>

#include "ooxml_metadata.h"
#include "xlsx_workbook.h"

namespace office {
namespace {

struct XlsxCell {
    std::string sheet;
    std::string ref;
    std::string col;
    int row = 0;
    int col_index = 0;
    std::string type;
    std::string raw_value;
    std::string value;
    std::string formula;
    std::string style_id;
};

struct QueryState {
    std::vector<XlsxCell> cells;
};

std::optional<int> parseInt(std::string_view value) {
    if (value.empty()) return std::nullopt;
    char *end = nullptr;
    auto text = std::string(value);
    auto parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') return std::nullopt;
    return static_cast<int>(parsed);
}

std::optional<double> parseDouble(std::string_view value) {
    if (value.empty()) return std::nullopt;
    char *end = nullptr;
    auto text = std::string(value);
    auto parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') return std::nullopt;
    return parsed;
}

int colToIndex(std::string_view col) {
    int value = 0;
    for (char ch : col) {
        if (!std::isalpha(static_cast<unsigned char>(ch))) return 0;
        value = value * 26 + (std::toupper(static_cast<unsigned char>(ch)) - 'A' + 1);
    }
    return value;
}

bool parseRef(std::string_view ref, std::string &col, int &row) {
    std::size_t i = 0;
    while (i < ref.size() && std::isalpha(static_cast<unsigned char>(ref[i]))) ++i;
    if (i == 0 || i == ref.size()) return false;
    auto row_text = ref.substr(i);
    auto parsed_row = parseInt(row_text);
    if (!parsed_row.has_value()) return false;
    col = std::string(ref.substr(0, i));
    std::transform(col.begin(), col.end(), col.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    row = *parsed_row;
    return true;
}

std::string cellRef(const parser::CellRef &cell) {
    return cell.col + std::to_string(cell.row);
}

const XlsxSheetInfo *findSheet(const XlsxWorkbook &workbook, std::string_view name) {
    for (const auto &sheet : workbook.sheets) {
        if (sheet.name == name) return &sheet;
    }
    return nullptr;
}

const XlsxSheetInfo *findSheetByIndex(const XlsxWorkbook &workbook, int index) {
    if (index <= 0 || static_cast<std::size_t>(index) > workbook.sheets.size()) return nullptr;
    return &workbook.sheets[static_cast<std::size_t>(index - 1)];
}

ResolveResult fail(std::string message) {
    return {.error = std::move(message)};
}

std::optional<std::string> metadataTargetFromPath(const parser::PathAst &path_ast) {
    if (path_ast.kind != parser::PathAstKind::Path || path_ast.segments.empty()) return std::nullopt;
    if (path_ast.segments[0].name != "meta") return std::nullopt;
    if (path_ast.segments.size() == 1) return std::string("meta");
    if (path_ast.segments.size() == 2) return path_ast.segments[1].name;
    return std::string("__invalid__");
}

std::optional<std::string> attrPredicateValue(const parser::PathSegment &segment, std::string_view name) {
    if (segment.predicate.kind == parser::PathPredicateKind::Attribute && segment.predicate.name == name) {
        return segment.predicate.value;
    }
    return std::nullopt;
}

std::vector<XlsxCell> loadCells(const XlsxPackage &package, const XlsxWorkbook &workbook, const XlsxSheetInfo &sheet, std::string &error) {
    auto xml = package.readText(sheet.worksheet_part);
    if (!xml.has_value()) {
        error = "missing worksheet part: " + sheet.worksheet_part;
        return {};
    }

    pugi::xml_document doc;
    auto parsed = doc.load_string(xml->c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
    if (!parsed) {
        error = "failed to parse worksheet part " + sheet.worksheet_part + ": " + parsed.description();
        return {};
    }

    std::vector<XlsxCell> cells;
    auto sheet_data = childByLocalName(doc.document_element(), "sheetData");
    for (auto row_node : sheet_data.children()) {
        if (!localNameEquals(row_node.name(), "row")) continue;
        for (auto cell_node : row_node.children()) {
            if (!localNameEquals(cell_node.name(), "c")) continue;
            XlsxCell cell;
            cell.sheet = sheet.name;
            cell.ref = cell_node.attribute("r").value();
            if (!parseRef(cell.ref, cell.col, cell.row)) continue;
            cell.col_index = colToIndex(cell.col);
            cell.type = cell_node.attribute("t").value();
            cell.style_id = cell_node.attribute("s").value();
            cell.formula = childByLocalName(cell_node, "f").text().get();
            cell.raw_value = childByLocalName(cell_node, "v").text().get();
            cell.value = cell.raw_value;

            if (cell.type == "s") {
                auto index = parseInt(cell.raw_value);
                if (index.has_value() && *index >= 0 && static_cast<std::size_t>(*index) < workbook.shared_strings.size()) {
                    cell.value = workbook.shared_strings[static_cast<std::size_t>(*index)];
                }
            } else if (cell.type == "inlineStr") {
                std::ostringstream text;
                auto is = childByLocalName(cell_node, "is");
                for (auto t : is.children()) {
                    if (localNameEquals(t.name(), "t")) text << t.text().get();
                }
                cell.value = text.str();
            }

            cells.push_back(std::move(cell));
        }
    }
    return cells;
}

DocumentNode makeCellNode(const XlsxCell &cell) {
    DocumentNode node;
    node.path = cell.sheet + "!" + cell.ref;
    node.type = "cell";
    node.text = cell.value;
    node.preview = cell.value;
    node.child_count = 0;
    node.format["sheet"] = cell.sheet;
    node.format["ref"] = cell.ref;
    node.format["row"] = std::to_string(cell.row);
    node.format["col"] = cell.col;
    node.format["type"] = cell.type;
    node.format["rawValue"] = cell.raw_value;
    node.format["value"] = cell.value;
    if (!cell.formula.empty()) node.format["formula"] = cell.formula;
    if (!cell.style_id.empty()) node.format["styleId"] = cell.style_id;
    return node;
}

DocumentNode makeCollectionNode(std::string path, std::string type, std::vector<XlsxCell> cells) {
    DocumentNode node;
    node.path = std::move(path);
    node.type = std::move(type);
    node.child_count = static_cast<int>(cells.size());
    node.format["count"] = std::to_string(cells.size());
    for (const auto &cell : cells) {
        node.children.push_back(makeCellNode(cell));
    }
    node.child_count = static_cast<int>(node.children.size());
    if (!node.children.empty()) {
        node.preview = node.children.front().preview;
        node.text = node.children.front().text;
    }
    return node;
}

DocumentNode makeSheetNode(const std::string &sheet_name, std::vector<XlsxCell> cells) {
    return makeCollectionNode(sheet_name, "sheet", std::move(cells));
}

DocumentNode makeRowNode(const std::string &sheet_name, int row, std::vector<XlsxCell> cells) {
    return makeCollectionNode(sheet_name + "!" + std::to_string(row) + ":" + std::to_string(row), "row", std::move(cells));
}

DocumentNode makeColumnNode(const std::string &sheet_name, const std::string &col, std::vector<XlsxCell> cells) {
    return makeCollectionNode(sheet_name + "!" + col + ":" + col, "column", std::move(cells));
}

bool compareValues(std::string_view left, std::string_view op, std::string_view right) {
    auto ln = parseDouble(left);
    auto rn = parseDouble(right);
    if (op == ">" || op == "<" || op == ">=" || op == "<=") {
        if (!ln.has_value() || !rn.has_value()) return false;
        if (op == ">") return *ln > *rn;
        if (op == "<") return *ln < *rn;
        if (op == ">=") return *ln >= *rn;
        if (op == "<=") return *ln <= *rn;
    }
    if (op == "=") return left == right;
    if (op == "!=") return left != right;
    if (op == "~=") return std::string(left).find(std::string(right)) != std::string::npos;
    return false;
}

std::string cellAttr(const XlsxCell &cell, std::string_view key) {
    if (key == "value" || key == "text") return cell.value;
    if (key == "rawValue") return cell.raw_value;
    if (key == "ref") return cell.ref;
    if (key == "sheet") return cell.sheet;
    if (key == "row") return std::to_string(cell.row);
    if (key == "col") return cell.col;
    if (key == "type") return cell.type;
    if (key == "formula") return cell.formula;
    if (key == "styleId") return cell.style_id;
    return {};
}

std::string sheetAttr(const XlsxCell &cell, std::string_view key) {
    if (key == "name" || key == "sheet" || key == "value" || key == "text") return cell.sheet;
    return {};
}

std::string rowAttr(const XlsxCell &cell, std::string_view key) {
    if (key == "row" || key == "index" || key == "number" || key == "value" || key == "text") return std::to_string(cell.row);
    if (key == "sheet") return cell.sheet;
    return {};
}

std::string columnAttr(const XlsxCell &cell, std::string_view key) {
    if (key == "col" || key == "column" || key == "name" || key == "value" || key == "text") return cell.col;
    if (key == "index") return std::to_string(cell.col_index);
    if (key == "sheet") return cell.sheet;
    return {};
}

using AttrGetter = std::string (*)(const XlsxCell &, std::string_view);

bool filterMatches(const XlsxCell &cell, const parser::FilterExpr &expr, AttrGetter attr_getter) {
    if (expr.kind == parser::FilterExprKind::And) {
        return std::all_of(expr.parts.begin(), expr.parts.end(), [&](const auto &part) { return filterMatches(cell, part, attr_getter); });
    }
    if (expr.kind == parser::FilterExprKind::Or) {
        return std::any_of(expr.parts.begin(), expr.parts.end(), [&](const auto &part) { return filterMatches(cell, part, attr_getter); });
    }
    auto value = attr_getter(cell, expr.predicate.key);
    if (expr.predicate.op == "exists") return !value.empty();
    return compareValues(value, expr.predicate.op, expr.predicate.value);
}

bool pseudoMatches(const XlsxCell &cell, const parser::PseudoClass &pseudo, std::string_view subject) {
    if (pseudo.name == "contains") {
        if (!pseudo.arg.has_value()) return false;
        if (subject == "sheet" || subject == "worksheet") return cell.sheet.find(*pseudo.arg) != std::string::npos;
        if (subject == "row") return std::to_string(cell.row).find(*pseudo.arg) != std::string::npos;
        if (subject == "column" || subject == "col") return cell.col.find(*pseudo.arg) != std::string::npos;
        return cell.value.find(*pseudo.arg) != std::string::npos;
    }
    if (pseudo.name == "empty") {
        return subject == "cell" && cell.value.empty() && cell.formula.empty();
    }
    if (pseudo.name == "has") {
        return pseudo.arg.has_value() && *pseudo.arg == "formula" && !cell.formula.empty();
    }
    if (pseudo.name == "no-alt") {
        return false;
    }
    return false;
}

void collectFilterKeys(const parser::FilterExpr &expr, std::vector<std::string> &keys) {
    if (expr.kind == parser::FilterExprKind::Predicate) {
        keys.push_back(expr.predicate.key);
        return;
    }
    for (const auto &part : expr.parts) collectFilterKeys(part, keys);
}

bool knownAttrForSubject(std::string_view subject, std::string_view key) {
    if (subject == "cell") {
        return key == "value" || key == "text" || key == "rawValue" || key == "ref" || key == "sheet" ||
               key == "row" || key == "col" || key == "type" || key == "formula" || key == "styleId";
    }
    if (subject == "sheet" || subject == "worksheet") {
        return key == "name" || key == "sheet" || key == "value" || key == "text";
    }
    if (subject == "row") {
        return key == "row" || key == "index" || key == "number" || key == "value" || key == "text" || key == "sheet";
    }
    if (subject == "column" || subject == "col") {
        return key == "col" || key == "column" || key == "name" || key == "value" || key == "text" || key == "index" || key == "sheet";
    }
    return false;
}

void collectMissingKeys(const parser::SelectorStep &step, std::set<std::string> &warnings) {
    std::vector<std::string> keys;
    for (const auto &filter : step.filters) collectFilterKeys(filter, keys);
    for (const auto &key : keys) {
        if (!knownAttrForSubject(step.subject, key)) {
            warnings.insert("missing-key:" + step.subject + "." + key);
        }
    }
}

bool subjectMatches(const XlsxCell &cell, const parser::SelectorStep &step, AttrGetter attr_getter) {
    if (step.sheet.has_value() && *step.sheet != cell.sheet) return false;
    for (const auto &filter : step.filters) {
        if (!filterMatches(cell, filter, attr_getter)) return false;
    }
    for (const auto &pseudo : step.pseudos) {
        if (!pseudoMatches(cell, pseudo, step.subject)) return false;
    }
    return true;
}

std::vector<XlsxCell> uniqueByRef(std::vector<XlsxCell> cells) {
    std::vector<XlsxCell> unique;
    std::set<std::string> seen;
    for (auto &cell : cells) {
        auto key = cell.sheet + "!" + cell.ref;
        if (seen.insert(key).second) {
            unique.push_back(std::move(cell));
        }
    }
    return unique;
}

std::vector<XlsxCell> applyNthByCellOrder(std::vector<XlsxCell> cells, std::optional<int> nth) {
    if (!nth.has_value()) return cells;
    if (*nth <= 0 || static_cast<std::size_t>(*nth) > cells.size()) return {};
    return {cells[static_cast<std::size_t>(*nth - 1)]};
}

std::vector<XlsxCell> applyNthByGroup(std::vector<XlsxCell> cells, std::optional<int> nth, const std::string &subject) {
    if (!nth.has_value()) return cells;

    std::vector<std::string> order;
    std::set<std::string> seen;
    for (const auto &cell : cells) {
        std::string key;
        if (subject == "sheet" || subject == "worksheet") key = cell.sheet;
        else if (subject == "row") key = cell.sheet + "!" + std::to_string(cell.row);
        else if (subject == "column" || subject == "col") key = cell.sheet + "!" + cell.col;
        else key = cell.sheet + "!" + cell.ref;
        if (seen.insert(key).second) order.push_back(std::move(key));
    }
    if (*nth <= 0 || static_cast<std::size_t>(*nth) > order.size()) return {};
    const auto &wanted = order[static_cast<std::size_t>(*nth - 1)];

    std::vector<XlsxCell> narrowed;
    for (auto &cell : cells) {
        std::string key;
        if (subject == "sheet" || subject == "worksheet") key = cell.sheet;
        else if (subject == "row") key = cell.sheet + "!" + std::to_string(cell.row);
        else if (subject == "column" || subject == "col") key = cell.sheet + "!" + cell.col;
        else key = cell.sheet + "!" + cell.ref;
        if (key == wanted) narrowed.push_back(std::move(cell));
    }
    return narrowed;
}

std::optional<AttrGetter> attrGetterForSubject(std::string_view subject) {
    if (subject == "cell") return cellAttr;
    if (subject == "sheet" || subject == "worksheet") return sheetAttr;
    if (subject == "row") return rowAttr;
    if (subject == "column" || subject == "col") return columnAttr;
    return std::nullopt;
}

std::vector<XlsxCell> applyStep(std::vector<XlsxCell> cells, const parser::SelectorStep &step, std::string &error, std::set<std::string> &warnings) {
    auto attr_getter = attrGetterForSubject(step.subject);
    if (!attr_getter.has_value()) {
        error = "unsupported XLSX selector subject: " + step.subject;
        return {};
    }
    collectMissingKeys(step, warnings);

    std::vector<XlsxCell> matched;
    for (auto &cell : cells) {
        if (subjectMatches(cell, step, *attr_getter)) matched.push_back(std::move(cell));
    }

    if (step.subject == "cell") {
        return applyNthByCellOrder(std::move(matched), step.nth);
    }
    return applyNthByGroup(std::move(matched), step.nth, step.subject);
}

std::vector<XlsxCell> loadAllCells(const XlsxPackage &package, const XlsxWorkbook &workbook, std::string &error) {
    std::vector<XlsxCell> all;
    for (const auto &sheet : workbook.sheets) {
        auto cells = loadCells(package, workbook, sheet, error);
        if (!error.empty()) return {};
        all.insert(all.end(), std::make_move_iterator(cells.begin()), std::make_move_iterator(cells.end()));
    }
    return all;
}

std::vector<DocumentNode> makeGroupedResultNodes(std::vector<XlsxCell> cells, const std::string &subject) {
    std::vector<DocumentNode> nodes;
    if (subject == "cell") {
        for (const auto &cell : cells) nodes.push_back(makeCellNode(cell));
        return nodes;
    }

    std::vector<std::string> order;
    std::set<std::string> seen;
    for (const auto &cell : cells) {
        std::string key;
        if (subject == "sheet" || subject == "worksheet") key = cell.sheet;
        else if (subject == "row") key = cell.sheet + "!" + std::to_string(cell.row);
        else key = cell.sheet + "!" + cell.col;
        if (seen.insert(key).second) order.push_back(std::move(key));
    }

    for (const auto &key : order) {
        std::vector<XlsxCell> group;
        for (const auto &cell : cells) {
            if ((subject == "sheet" || subject == "worksheet") && key == cell.sheet) group.push_back(cell);
            else if (subject == "row" && key == cell.sheet + "!" + std::to_string(cell.row)) group.push_back(cell);
            else if ((subject == "column" || subject == "col") && key == cell.sheet + "!" + cell.col) group.push_back(cell);
        }
        if (group.empty()) continue;
        const auto sheet_name = group.front().sheet;
        const auto row = group.front().row;
        const auto col = group.front().col;
        if (subject == "sheet" || subject == "worksheet") nodes.push_back(makeSheetNode(sheet_name, std::move(group)));
        else if (subject == "row") nodes.push_back(makeRowNode(sheet_name, row, std::move(group)));
        else nodes.push_back(makeColumnNode(sheet_name, col, std::move(group)));
    }
    return nodes;
}

DocumentNode makeQueryNode(std::string path, std::vector<DocumentNode> children, const std::set<std::string> &warnings = {}) {
    DocumentNode node;
    node.path = std::move(path);
    node.type = "query";
    node.children = std::move(children);
    node.child_count = static_cast<int>(node.children.size());
    node.format["count"] = std::to_string(node.children.size());
    if (!warnings.empty()) {
        std::string joined;
        for (const auto &warning : warnings) {
            if (!joined.empty()) joined += ";";
            joined += warning;
        }
        node.format["warnings"] = joined;
    }
    if (!node.children.empty()) {
        node.text = node.children.front().text;
        node.preview = node.children.front().preview;
    }
    return node;
}

} // namespace

ResolveResult resolveXlsxGet(const XlsxPackage &package, const parser::PathAst &path_ast, int depth) {
    (void)depth;
    if (auto meta = metadataTargetFromPath(path_ast); meta.has_value()) {
        if (*meta == "__invalid__") return fail("unsupported metadata path");
        return resolveOoxmlMetadataGet(package, *meta);
    }

    if (path_ast.kind == parser::PathAstKind::Path) {
        if (path_ast.segments.size() != 2 ||
            !(path_ast.segments[0].name == "sheet" || path_ast.segments[0].name == "worksheet") ||
            path_ast.segments[1].name != "cell") {
            return fail("XLSX slash get currently supports /sheet[N]/cell[@ref=A1]");
        }

        auto workbook_result = loadWorkbook(package);
        if (!workbook_result.ok) return fail(workbook_result.error);
        const XlsxSheetInfo *sheet = nullptr;
        if (path_ast.segments[0].predicate.kind == parser::PathPredicateKind::Index) {
            sheet = findSheetByIndex(workbook_result.workbook, path_ast.segments[0].predicate.index);
        } else if (path_ast.segments[0].predicate.kind == parser::PathPredicateKind::Attribute && path_ast.segments[0].predicate.name == "name") {
            sheet = findSheet(workbook_result.workbook, path_ast.segments[0].predicate.value);
        } else {
            return fail("XLSX slash sheet segment requires [N] or [@name=Sheet1]");
        }
        if (sheet == nullptr) return fail("sheet not found");
        auto wanted = attrPredicateValue(path_ast.segments[1], "ref");
        if (!wanted.has_value()) return fail("XLSX slash cell segment requires [@ref=A1]");

        std::string error;
        auto cells = loadCells(package, workbook_result.workbook, *sheet, error);
        if (!error.empty()) return fail(error);
        auto it = std::find_if(cells.begin(), cells.end(), [&](const auto &cell) { return cell.ref == *wanted; });
        if (it == cells.end()) return fail("cell not found: " + sheet->name + "!" + *wanted);
        return {.ok = true, .node = makeCellNode(*it)};
    }

    if (path_ast.kind != parser::PathAstKind::ExcelReference) {
        return fail("XLSX get currently supports native Excel references only");
    }
    const auto &ref = path_ast.excel;
    if (ref.reference_kind == parser::ExcelReferenceKind::Element) {
        return fail("XLSX get for non-cell elements is not supported yet");
    }

    auto workbook_result = loadWorkbook(package);
    if (!workbook_result.ok) return fail(workbook_result.error);
    auto *sheet = findSheet(workbook_result.workbook, ref.sheet);
    if (sheet == nullptr) return fail("sheet not found: " + ref.sheet);

    std::string error;
    auto cells = loadCells(package, workbook_result.workbook, *sheet, error);
    if (!error.empty()) return fail(error);

    if (ref.reference_kind == parser::ExcelReferenceKind::Cell) {
        const auto wanted = cellRef(ref.cell);
        auto it = std::find_if(cells.begin(), cells.end(), [&](const auto &cell) { return cell.ref == wanted; });
        if (it == cells.end()) return fail("cell not found: " + ref.sheet + "!" + wanted);
        return {.ok = true, .node = makeCellNode(*it)};
    }

    std::vector<XlsxCell> matched;
    if (ref.reference_kind == parser::ExcelReferenceKind::Range) {
        const auto min_row = std::min(ref.from.row, ref.to.row);
        const auto max_row = std::max(ref.from.row, ref.to.row);
        const auto min_col = std::min(colToIndex(ref.from.col), colToIndex(ref.to.col));
        const auto max_col = std::max(colToIndex(ref.from.col), colToIndex(ref.to.col));
        std::copy_if(cells.begin(), cells.end(), std::back_inserter(matched), [&](const auto &cell) {
            return cell.row >= min_row && cell.row <= max_row && cell.col_index >= min_col && cell.col_index <= max_col;
        });
        return {.ok = true, .node = makeCollectionNode(ref.input, "range", std::move(matched))};
    }

    if (ref.reference_kind == parser::ExcelReferenceKind::Row) {
        std::copy_if(cells.begin(), cells.end(), std::back_inserter(matched), [&](const auto &cell) { return cell.row == ref.row; });
        return {.ok = true, .node = makeCollectionNode(ref.input, "row", std::move(matched))};
    }

    if (ref.reference_kind == parser::ExcelReferenceKind::Column) {
        std::copy_if(cells.begin(), cells.end(), std::back_inserter(matched), [&](const auto &cell) { return cell.col == ref.column; });
        return {.ok = true, .node = makeCollectionNode(ref.input, "column", std::move(matched))};
    }

    return fail("unsupported XLSX reference kind");
}

ResolveResult resolveXlsxQuery(const XlsxPackage &package, const parser::Selector &selector) {
    auto workbook_result = loadWorkbook(package);
    if (!workbook_result.ok) return fail(workbook_result.error);

    std::string error;
    auto all_cells = loadAllCells(package, workbook_result.workbook, error);
    if (!error.empty()) return fail(error);

    std::vector<DocumentNode> result_nodes;
    std::set<std::string> seen_nodes;
    std::set<std::string> warnings;
    for (const auto &alternative : selector.alternatives) {
        if (alternative.steps.empty()) continue;

        auto scoped_cells = all_cells;
        for (const auto &step : alternative.steps) {
            scoped_cells = applyStep(std::move(scoped_cells), step, error, warnings);
            if (!error.empty()) return fail(error);
        }

        const auto &terminal = alternative.steps.back();
        auto grouped = makeGroupedResultNodes(std::move(scoped_cells), terminal.subject);
        for (auto &node : grouped) {
            if (seen_nodes.insert(node.path + "|" + node.type).second) {
                result_nodes.push_back(std::move(node));
            }
        }
    }

    return {.ok = true, .node = makeQueryNode("query:" + selector.input, std::move(result_nodes), warnings)};
}

} // namespace office
