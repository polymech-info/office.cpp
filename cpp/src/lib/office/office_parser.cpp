#include "office_parser.h"

#include "office_util.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace office::parser {
namespace {

struct ParserError : std::runtime_error {
    std::size_t pos;
    ParserError(std::size_t p, const std::string &msg) : std::runtime_error(msg), pos(p) {}
};

std::string toString(std::string_view sv) {
    return std::string(sv.begin(), sv.end());
}

bool ieq(std::string_view a, std::string_view b) {
    return lower(std::string(a)) == lower(std::string(b));
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-';
}

bool allDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

bool allLetters(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalpha(c); });
}

std::string upper(std::string_view s) {
    return office::upper(toString(s));
}

bool parseCellRef(std::string_view s, CellRef &cell) {
    std::size_t i = 0;
    while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
    if (i == 0 || i == s.size()) return false;
    auto row_part = s.substr(i);
    if (!allDigits(row_part)) return false;
    cell.col = upper(s.substr(0, i));
    cell.row = std::stoi(toString(row_part));
    return true;
}

AstResult<PathAst> okPath(PathAst ast) {
    return {true, std::move(ast), "", 0};
}

AstResult<Selector> okSelector(Selector selector) {
    return {true, std::move(selector), "", 0};
}

PathPredicate parsePathPredicate(std::string_view raw, std::size_t base_pos) {
    if (raw.empty()) throw ParserError(base_pos, "empty predicate");
    if (allDigits(raw)) {
        if ((raw.size() > 1 && raw[0] == '0') || raw == "0") {
            throw ParserError(base_pos, "index must be a positive 1-based integer");
        }
        return {.kind = PathPredicateKind::Index, .index = std::stoi(toString(raw))};
    }
    if (raw == "last()") {
        return {.kind = PathPredicateKind::Last};
    }
    if (raw[0] == '@') {
        auto eq = raw.find('=');
        if (eq == std::string_view::npos) throw ParserError(base_pos, "attribute predicate must contain '='");
        if (eq <= 1) throw ParserError(base_pos, "attribute predicate must have a non-empty name");
        auto name = raw.substr(1, eq - 1);
        auto value = raw.substr(eq + 1);
        if (value.empty()) throw ParserError(base_pos + eq + 1, "attribute predicate value is empty");
        if (!isIdentStart(name[0])) throw ParserError(base_pos + 1, "attribute name must be a simple identifier");
        for (std::size_t i = 1; i < name.size(); ++i) {
            if (!std::isalnum(static_cast<unsigned char>(name[i])) && name[i] != '_') {
                throw ParserError(base_pos + 1 + i, "attribute name must be a simple identifier");
            }
        }
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        } else {
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (std::isspace(static_cast<unsigned char>(value[i])) || value[i] == '\'' || value[i] == '"') {
                    throw ParserError(base_pos + eq + 1 + i, "attribute value must be bare or double-quoted");
                }
            }
        }
        return {.kind = PathPredicateKind::Attribute, .name = toString(name), .value = toString(value)};
    }
    throw ParserError(base_pos, "stable paths only accept [N], [last()], or [@attr=value]");
}

PathAst parseExcelReferenceAst(std::string_view input) {
    auto bang = input.find('!');
    if (bang == std::string_view::npos || bang == 0 || bang + 1 >= input.size()) {
        throw ParserError(0, "Excel reference must be sheet-scoped, e.g. Sheet1!A1");
    }

    ExcelReference ref;
    ref.input = toString(input);
    ref.sheet = toString(input.substr(0, bang));
    auto target = input.substr(bang + 1);

    auto colon = target.find(':');
    if (colon != std::string_view::npos) {
        auto left = target.substr(0, colon);
        auto right = target.substr(colon + 1);
        CellRef from;
        CellRef to;
        if (parseCellRef(left, from) && parseCellRef(right, to)) {
            ref.reference_kind = ExcelReferenceKind::Range;
            ref.from = std::move(from);
            ref.to = std::move(to);
            return {.kind = PathAstKind::ExcelReference, .input = toString(input), .excel = std::move(ref)};
        }
        if (allDigits(left) && left == right) {
            ref.reference_kind = ExcelReferenceKind::Row;
            ref.row = std::stoi(toString(left));
            return {.kind = PathAstKind::ExcelReference, .input = toString(input), .excel = std::move(ref)};
        }
        if (allLetters(left) && ieq(left, right)) {
            ref.reference_kind = ExcelReferenceKind::Column;
            ref.column = upper(left);
            return {.kind = PathAstKind::ExcelReference, .input = toString(input), .excel = std::move(ref)};
        }
        throw ParserError(bang + 1 + colon, "invalid Excel range reference");
    }

    CellRef cell;
    if (parseCellRef(target, cell)) {
        ref.reference_kind = ExcelReferenceKind::Cell;
        ref.cell = std::move(cell);
        return {.kind = PathAstKind::ExcelReference, .input = toString(input), .excel = std::move(ref)};
    }

    std::size_t i = 0;
    if (i < target.size() && isIdentStart(target[i])) {
        ++i;
        while (i < target.size() && isIdentChar(target[i])) ++i;
        ref.reference_kind = ExcelReferenceKind::Element;
        ref.element = toString(target.substr(0, i));
        ref.tail = toString(target.substr(i));
        return {.kind = PathAstKind::ExcelReference, .input = toString(input), .excel = std::move(ref)};
    }

    throw ParserError(bang + 1, "invalid Excel reference target");
}

class SelectorParser {
public:
    explicit SelectorParser(std::string_view input) : s(input) {}

    Selector parse() {
        Selector selector;
        selector.input = toString(s);
        while (true) {
            skipWs();
            if (eof()) break;
            selector.alternatives.push_back(parseAlternative());
            skipWs();
            if (eof()) break;
            if (peek() != ',') throw error("expected comma between selector alternatives");
            ++pos;
        }
        if (selector.alternatives.empty()) throw error("empty selector");
        return selector;
    }

private:
    std::string_view s;
    std::size_t pos = 0;

    bool eof() const { return pos >= s.size(); }
    char peek() const { return eof() ? '\0' : s[pos]; }
    void skipWs() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) ++pos;
    }
    ParserError error(const std::string &msg) const { return ParserError(pos, msg); }

    SelectorAlternative parseAlternative() {
        SelectorAlternative alternative;
        SelectorCombinator next_comb = SelectorCombinator::None;
        while (true) {
            skipWs();
            if (eof() || peek() == ',') break;
            alternative.steps.push_back(parseStep(next_comb));
            skipWs();
            if (eof() || peek() == ',') break;
            if (peek() == '>') {
                next_comb = SelectorCombinator::Child;
                ++pos;
            } else {
                next_comb = SelectorCombinator::Descendant;
            }
        }
        if (alternative.steps.empty()) throw error("empty selector alternative");
        return alternative;
    }

    std::string parseIdentifier() {
        if (eof() || !isIdentStart(peek())) throw error("expected identifier");
        auto start = pos++;
        while (!eof() && isIdentChar(peek())) ++pos;
        return toString(s.substr(start, pos - start));
    }

    SelectorStep parseStep(SelectorCombinator combinator) {
        SelectorStep step;
        step.combinator = combinator;
        auto subject = parseIdentifier();
        if (!eof() && peek() == '!') {
            step.sheet = subject;
            ++pos;
            subject = parseIdentifier();
        }
        step.subject = std::move(subject);

        while (!eof()) {
            if (peek() == '[') {
                auto content_start = pos + 1;
                auto content = readBalanced('[', ']');
                auto trimmed = trim(content);
                if (trimmed.empty()) throw ParserError(content_start, "empty selector filter");
                if (allDigits(trimmed)) {
                    step.nth = std::stoi(trimmed);
                } else {
                    step.filters.push_back(parseFilterExpr(trimmed, content_start));
                }
            } else if (peek() == ':') {
                step.pseudos.push_back(parsePseudo());
            } else {
                break;
            }
        }
        return step;
    }

    PseudoClass parsePseudo() {
        ++pos;
        PseudoClass pseudo;
        pseudo.name = parseIdentifier();
        if (!eof() && peek() == '(') {
            pseudo.arg = stripOuterQuotes(trim(readBalanced('(', ')')));
        }
        return pseudo;
    }

    std::string readBalanced(char open, char close) {
        if (peek() != open) throw error("expected balanced block");
        ++pos;
        auto start = pos;
        int depth = 1;
        char quote = '\0';
        bool escaped = false;
        while (!eof()) {
            char c = s[pos++];
            if (quote != '\0') {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == quote) {
                    quote = '\0';
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }
            if (c == open) {
                ++depth;
            } else if (c == close) {
                --depth;
                if (depth == 0) {
                    return toString(s.substr(start, pos - start - 1));
                }
            }
        }
        throw ParserError(start - 1, "unclosed balanced block");
    }

    static std::string trim(std::string_view v) {
        std::size_t a = 0;
        std::size_t b = v.size();
        while (a < b && std::isspace(static_cast<unsigned char>(v[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(v[b - 1]))) --b;
        return toString(v.substr(a, b - a));
    }

    static std::string stripOuterQuotes(const std::string &v) {
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
            return v.substr(1, v.size() - 2);
        }
        return v;
    }

    FilterExpr parseFilterExpr(std::string_view expr, std::size_t base) {
        FilterParser fp(expr, base);
        return fp.parse();
    }

    class FilterParser {
    public:
        FilterParser(std::string_view input, std::size_t offset) : s(input), base(offset) {}

        FilterExpr parse() {
            auto out = parseOr();
            skipWs();
            if (!eof()) throw error("unexpected filter input");
            return out;
        }

    private:
        std::string_view s;
        std::size_t base;
        std::size_t pos = 0;

        bool eof() const { return pos >= s.size(); }
        char peek() const { return eof() ? '\0' : s[pos]; }
        void skipWs() {
            while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) ++pos;
        }
        ParserError error(const std::string &msg) const { return ParserError(base + pos, msg); }

        bool tryKeyword(std::string_view kw) {
            skipWs();
            if (pos + kw.size() > s.size()) return false;
            if (!ieq(s.substr(pos, kw.size()), kw)) return false;
            auto after = pos + kw.size();
            if (after < s.size() && isIdentChar(s[after])) return false;
            pos = after;
            return true;
        }

        FilterExpr parseOr() {
            std::vector<FilterExpr> parts;
            parts.push_back(parseAnd());
            while (tryKeyword("or")) parts.push_back(parseAnd());
            if (parts.size() == 1) return parts.front();
            return {.kind = FilterExprKind::Or, .parts = std::move(parts)};
        }

        FilterExpr parseAnd() {
            std::vector<FilterExpr> parts;
            parts.push_back(parseFactor());
            while (tryKeyword("and")) parts.push_back(parseFactor());
            if (parts.size() == 1) return parts.front();
            return {.kind = FilterExprKind::And, .parts = std::move(parts)};
        }

        FilterExpr parseFactor() {
            skipWs();
            if (peek() == '(') {
                ++pos;
                auto inner = parseOr();
                skipWs();
                if (eof() || peek() != ')') throw error("expected ')'");
                ++pos;
                return inner;
            }
            return parsePredicate();
        }

        FilterExpr parsePredicate() {
            skipWs();
            bool at = false;
            if (peek() == '@') {
                at = true;
                ++pos;
            }
            auto key_start = pos;
            if (eof() || !isIdentStart(peek())) throw error("expected predicate key");
            ++pos;
            while (!eof() && (isIdentChar(peek()) || peek() == '.')) ++pos;
            auto key = s.substr(key_start, pos - key_start);
            skipWs();
            if (eof() || peek() == ')') {
                return {.kind = FilterExprKind::Predicate, .predicate = {.key = toString(key), .op = "exists", .attribute = at}};
            }
            auto op = readOp();
            auto value = readValue();
            if (value.empty()) throw error("expected predicate value");
            return {.kind = FilterExprKind::Predicate, .predicate = {.key = toString(key), .op = std::move(op), .value = std::move(value), .attribute = at}};
        }

        std::string readOp() {
            skipWs();
            for (auto op : {"!=", "~=", ">=", "<=", "=", ">", "<"}) {
                std::string_view osv(op);
                if (pos + osv.size() <= s.size() && s.substr(pos, osv.size()) == osv) {
                    pos += osv.size();
                    return toString(osv);
                }
            }
            throw error("expected operator");
        }

        std::string readValue() {
            skipWs();
            if (eof()) return "";
            if (peek() == 'r' && pos + 1 < s.size() && (s[pos + 1] == '"' || s[pos + 1] == '\'')) {
                auto start = pos;
                ++pos;
                readQuoted();
                return toString(s.substr(start, pos - start));
            }
            if (peek() == '"' || peek() == '\'') {
                return readQuoted();
            }
            auto start = pos;
            while (!eof()) {
                if (peek() == ')') break;
                if (std::isspace(static_cast<unsigned char>(peek()))) {
                    auto save = pos;
                    skipWs();
                    if (startsKeywordAt("and", pos) || startsKeywordAt("or", pos)) {
                        pos = save;
                        break;
                    }
                    pos = save;
                }
                ++pos;
            }
            auto v = trim(s.substr(start, pos - start));
            if (!v.empty() && (v[0] == '=' || v[0] == '~' || v[0] == '!')) {
                throw ParserError(base + start, "malformed predicate operator");
            }
            return v;
        }

        std::string readQuoted() {
            char q = peek();
            auto start = ++pos;
            bool escaped = false;
            while (!eof()) {
                char c = s[pos++];
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == q) {
                    return toString(s.substr(start, pos - start - 1));
                }
            }
            throw ParserError(base + start - 1, "unterminated quoted value");
        }

        bool startsKeywordAt(std::string_view kw, std::size_t at) const {
            if (at + kw.size() > s.size()) return false;
            if (!ieq(s.substr(at, kw.size()), kw)) return false;
            auto after = at + kw.size();
            return after >= s.size() || !isIdentChar(s[after]);
        }

        static std::string trim(std::string_view v) {
            std::size_t a = 0;
            std::size_t b = v.size();
            while (a < b && std::isspace(static_cast<unsigned char>(v[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(v[b - 1]))) --b;
            return toString(v.substr(a, b - a));
        }
    };
};

std::string combinatorJson(SelectorCombinator combinator) {
    switch (combinator) {
        case SelectorCombinator::None: return "\"none\"";
        case SelectorCombinator::Descendant: return "\"descendant\"";
        case SelectorCombinator::Child: return "\"child\"";
    }
    return "\"none\"";
}

std::string filterJson(const FilterExpr &expr) {
    std::ostringstream out;
    if (expr.kind == FilterExprKind::Predicate) {
        out << "{\"kind\":\"predicate\",\"key\":" << jstr(expr.predicate.key)
            << ",\"op\":" << jstr(expr.predicate.op)
            << ",\"value\":" << jstr(expr.predicate.value)
            << ",\"attribute\":" << (expr.predicate.attribute ? "true" : "false") << "}";
        return out.str();
    }

    out << "{\"kind\":" << jstr(expr.kind == FilterExprKind::And ? "and" : "or") << ",\"parts\":[";
    for (std::size_t i = 0; i < expr.parts.size(); ++i) {
        if (i) out << ",";
        out << filterJson(expr.parts[i]);
    }
    out << "]}";
    return out.str();
}

ParseResult errJson(const ParserError &err) {
    std::ostringstream out;
    out << "{\"success\":false,\"error\":{\"code\":\"invalid_parser_input\",\"pos\":" << err.pos
        << ",\"message\":" << jstr(err.what()) << "}}";
    return {false, out.str(), err.what(), err.pos};
}

} // namespace

AstResult<PathAst> parsePath(std::string_view input, std::string_view format) {
    try {
        if (input.empty()) throw ParserError(0, "path cannot be empty");
        if ((format == "xlsx" || format == "auto") && input.find('!') != std::string_view::npos && input.front() != '/') {
            return okPath(parseExcelReferenceAst(input));
        }
        if (input == "/") return okPath({.kind = PathAstKind::Path, .input = "/"});
        if (input.front() != '/') throw ParserError(0, "stable path must start with '/'");
        if (input.size() > 1 && input[1] == '/') throw ParserError(0, "path must start with exactly one '/'");
        if (input.back() == '/') throw ParserError(input.size() - 1, "trailing '/' is not allowed");
        if (input.find("//") != std::string_view::npos) throw ParserError(input.find("//"), "empty path segment is not allowed");
        if (input.find(':') != std::string_view::npos || input.find(',') != std::string_view::npos || input.find('>') != std::string_view::npos) {
            throw ParserError(0, "stable path contains selector syntax");
        }

        PathAst ast;
        ast.kind = PathAstKind::Path;
        ast.input = toString(input);
        std::size_t pos = 1;
        while (pos < input.size()) {
            auto slash = input.find('/', pos);
            auto end = slash == std::string_view::npos ? input.size() : slash;
            auto part = input.substr(pos, end - pos);
            if (part.empty()) throw ParserError(pos, "empty path segment");
            auto bracket = part.find('[');
            auto name = bracket == std::string_view::npos ? part : part.substr(0, bracket);
            if (name.empty() || !isIdentStart(name[0])) throw ParserError(pos, "invalid path segment name");
            for (std::size_t i = 1; i < name.size(); ++i) {
                if (!isIdentChar(name[i])) throw ParserError(pos + i, "invalid path segment name");
            }

            PathSegment segment;
            segment.name = toString(name);
            if (bracket != std::string_view::npos) {
                if (part.back() != ']') throw ParserError(pos + bracket, "path predicate must close at segment end");
                if (part.find('[', bracket + 1) != std::string_view::npos) throw ParserError(pos + bracket + 1, "multiple path predicates are not supported");
                auto raw = part.substr(bracket + 1, part.size() - bracket - 2);
                segment.predicate = parsePathPredicate(raw, pos + bracket + 1);
            }
            ast.segments.push_back(std::move(segment));

            if (slash == std::string_view::npos) break;
            pos = slash + 1;
        }
        return okPath(std::move(ast));
    } catch (const ParserError &e) {
        return {false, {}, e.what(), e.pos};
    }
}

AstResult<Selector> parseSelector(std::string_view input) {
    try {
        SelectorParser parser(input);
        return okSelector(parser.parse());
    } catch (const ParserError &e) {
        return {false, {}, e.what(), e.pos};
    }
}

std::string toJson(const PathAst &ast) {
    std::ostringstream out;
    if (ast.kind == PathAstKind::ExcelReference) {
        const auto &ref = ast.excel;
        out << "{\"kind\":\"excelReference\",\"input\":" << jstr(ref.input)
            << ",\"sheet\":" << jstr(ref.sheet) << ",";
        switch (ref.reference_kind) {
            case ExcelReferenceKind::Cell:
                out << "\"referenceKind\":\"cell\",\"cell\":{\"col\":" << jstr(ref.cell.col) << ",\"row\":" << ref.cell.row << "}}";
                break;
            case ExcelReferenceKind::Range:
                out << "\"referenceKind\":\"range\",\"from\":{\"col\":" << jstr(ref.from.col) << ",\"row\":" << ref.from.row
                    << "},\"to\":{\"col\":" << jstr(ref.to.col) << ",\"row\":" << ref.to.row << "}}";
                break;
            case ExcelReferenceKind::Row:
                out << "\"referenceKind\":\"row\",\"row\":" << ref.row << "}";
                break;
            case ExcelReferenceKind::Column:
                out << "\"referenceKind\":\"column\",\"column\":" << jstr(ref.column) << "}";
                break;
            case ExcelReferenceKind::Element:
                out << "\"referenceKind\":\"element\",\"element\":" << jstr(ref.element) << ",\"tail\":" << jstr(ref.tail) << "}";
                break;
        }
        return out.str();
    }

    out << "{\"kind\":\"path\",\"input\":" << jstr(ast.input) << ",\"segments\":[";
    for (std::size_t i = 0; i < ast.segments.size(); ++i) {
        if (i) out << ",";
        const auto &segment = ast.segments[i];
        out << "{\"name\":" << jstr(segment.name) << ",\"predicate\":";
        switch (segment.predicate.kind) {
            case PathPredicateKind::None:
                out << "{\"kind\":\"none\"}";
                break;
            case PathPredicateKind::Index:
                out << "{\"kind\":\"index\",\"value\":" << segment.predicate.index << "}";
                break;
            case PathPredicateKind::Last:
                out << "{\"kind\":\"last\"}";
                break;
            case PathPredicateKind::Attribute:
                out << "{\"kind\":\"attribute\",\"name\":" << jstr(segment.predicate.name)
                    << ",\"value\":" << jstr(segment.predicate.value) << "}";
                break;
        }
        out << "}";
    }
    out << "]}";
    return out.str();
}

std::string toJson(const Selector &selector) {
    std::ostringstream out;
    out << "{\"kind\":\"selector\",\"input\":" << jstr(selector.input) << ",\"alternatives\":[";
    for (std::size_t i = 0; i < selector.alternatives.size(); ++i) {
        if (i) out << ",";
        out << "{\"steps\":[";
        const auto &steps = selector.alternatives[i].steps;
        for (std::size_t j = 0; j < steps.size(); ++j) {
            if (j) out << ",";
            const auto &step = steps[j];
            out << "{\"combinator\":" << combinatorJson(step.combinator)
                << ",\"subject\":" << jstr(step.subject);
            if (step.sheet.has_value()) out << ",\"sheet\":" << jstr(*step.sheet);
            out << ",\"filters\":[";
            for (std::size_t k = 0; k < step.filters.size(); ++k) {
                if (k) out << ",";
                out << filterJson(step.filters[k]);
            }
            out << "],\"pseudos\":[";
            for (std::size_t k = 0; k < step.pseudos.size(); ++k) {
                if (k) out << ",";
                out << "{\"name\":" << jstr(step.pseudos[k].name);
                if (step.pseudos[k].arg.has_value()) out << ",\"arg\":" << jstr(*step.pseudos[k].arg);
                out << "}";
            }
            out << "]";
            if (step.nth.has_value()) out << ",\"nth\":" << *step.nth;
            out << "}";
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

ParseResult parsePathToJson(std::string_view input, std::string_view format) {
    auto result = parsePath(input, format);
    if (!result.ok) return errJson(ParserError(result.error_pos, result.error));
    return {true, toJson(result.ast), "", 0};
}

ParseResult parseSelectorToJson(std::string_view input) {
    auto result = parseSelector(input);
    if (!result.ok) return errJson(ParserError(result.error_pos, result.error));
    return {true, toJson(result.ast), "", 0};
}

} // namespace office::parser
