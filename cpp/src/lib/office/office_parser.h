#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace office::parser {

struct ParseError {
    std::string message;
    std::size_t pos = 0;
};

template <typename T>
struct AstResult {
    bool ok = false;
    T ast{};
    std::string error;
    std::size_t error_pos = 0;
};

struct ParseResult {
    bool ok = false;
    std::string json;
    std::string error;
    std::size_t error_pos = 0;
};

enum class PathPredicateKind {
    None,
    Index,
    Last,
    Attribute
};

struct PathPredicate {
    PathPredicateKind kind = PathPredicateKind::None;
    int index = 0;
    std::string name;
    std::string value;
};

struct PathSegment {
    std::string name;
    PathPredicate predicate;
};

enum class ExcelReferenceKind {
    Cell,
    Range,
    Row,
    Column,
    Element
};

struct CellRef {
    std::string col;
    int row = 0;
};

struct ExcelReference {
    std::string input;
    std::string sheet;
    ExcelReferenceKind reference_kind = ExcelReferenceKind::Cell;
    CellRef cell;
    CellRef from;
    CellRef to;
    int row = 0;
    std::string column;
    std::string element;
    std::string tail;
};

enum class PathAstKind {
    Path,
    ExcelReference
};

struct PathAst {
    PathAstKind kind = PathAstKind::Path;
    std::string input;
    std::vector<PathSegment> segments;
    ExcelReference excel;
};

enum class FilterExprKind {
    Predicate,
    And,
    Or
};

struct FilterPredicate {
    std::string key;
    std::string op;
    std::string value;
    bool attribute = false;
};

struct FilterExpr {
    FilterExprKind kind = FilterExprKind::Predicate;
    FilterPredicate predicate;
    std::vector<FilterExpr> parts;
};

struct PseudoClass {
    std::string name;
    std::optional<std::string> arg;
};

enum class SelectorCombinator {
    None,
    Descendant,
    Child
};

struct SelectorStep {
    SelectorCombinator combinator = SelectorCombinator::None;
    std::string subject;
    std::optional<std::string> sheet;
    std::vector<FilterExpr> filters;
    std::vector<PseudoClass> pseudos;
    std::optional<int> nth;
};

struct SelectorAlternative {
    std::vector<SelectorStep> steps;
};

struct Selector {
    std::string input;
    std::vector<SelectorAlternative> alternatives;
};

AstResult<PathAst> parsePath(std::string_view input, std::string_view format = "auto");
AstResult<Selector> parseSelector(std::string_view input);

std::string toJson(const PathAst &ast);
std::string toJson(const Selector &selector);

ParseResult parsePathToJson(std::string_view input, std::string_view format = "auto");
ParseResult parseSelectorToJson(std::string_view input);

} // namespace office::parser
