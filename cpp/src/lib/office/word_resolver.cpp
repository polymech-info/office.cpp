#include "word_resolver.h"

#include "office_xml.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <pugixml.hpp>

#include "ooxml_metadata.h"

namespace office {
namespace {

struct WordRun {
    int paragraph_index = 0;
    int run_index = 0;
    std::string text;
    bool bold = false;
    bool italic = false;
};

struct WordParagraph {
    int index = 0;
    std::string style;
    std::string text;
    std::vector<WordRun> runs;
};

struct WordDocument {
    std::vector<WordParagraph> paragraphs;
};

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

void collectText(pugi::xml_node node, std::ostringstream &out) {
    for (auto child : node.children()) {
        if (localNameEquals(child.name(), "t")) {
            out << child.text().get();
        }
        collectText(child, out);
    }
}

bool hasOnProperty(pugi::xml_node props, std::string_view name) {
    auto prop = childByLocalName(props, name);
    if (!prop) return false;
    auto val = attrByLocalName(prop, "val");
    return val.empty() || val == "1" || val == "true" || val == "on";
}

WordDocument loadWordDocument(const XlsxPackage &package, std::string &error) {
    auto xml = package.readText("word/document.xml");
    if (!xml.has_value()) {
        error = "missing required DOCX part: word/document.xml";
        return {};
    }

    pugi::xml_document doc;
    auto parsed = doc.load_string(xml->c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
    if (!parsed) {
        error = std::string("failed to parse word/document.xml: ") + parsed.description();
        return {};
    }

    WordDocument result;
    auto body = childByLocalName(doc.document_element(), "body");
    int p_index = 0;
    for (auto p_node : body.children()) {
        if (!localNameEquals(p_node.name(), "p")) continue;
        WordParagraph paragraph;
        paragraph.index = ++p_index;
        auto ppr = childByLocalName(p_node, "pPr");
        auto pstyle = childByLocalName(ppr, "pStyle");
        paragraph.style = attrByLocalName(pstyle, "val");

        int r_index = 0;
        for (auto r_node : p_node.children()) {
            if (!localNameEquals(r_node.name(), "r")) continue;
            WordRun run;
            run.paragraph_index = paragraph.index;
            run.run_index = ++r_index;
            auto rpr = childByLocalName(r_node, "rPr");
            run.bold = hasOnProperty(rpr, "b");
            run.italic = hasOnProperty(rpr, "i");
            std::ostringstream text;
            collectText(r_node, text);
            run.text = text.str();
            paragraph.text += run.text;
            paragraph.runs.push_back(std::move(run));
        }
        result.paragraphs.push_back(std::move(paragraph));
    }
    return result;
}

DocumentNode makeRunNode(const WordRun &run) {
    DocumentNode node;
    node.path = "/body/p[" + std::to_string(run.paragraph_index) + "]/r[" + std::to_string(run.run_index) + "]";
    node.type = "run";
    node.text = run.text;
    node.preview = run.text;
    node.format["paragraphIndex"] = std::to_string(run.paragraph_index);
    node.format["runIndex"] = std::to_string(run.run_index);
    node.format["bold"] = run.bold ? "true" : "false";
    node.format["italic"] = run.italic ? "true" : "false";
    return node;
}

DocumentNode makeParagraphNode(const WordParagraph &paragraph, int depth) {
    DocumentNode node;
    node.path = "/body/p[" + std::to_string(paragraph.index) + "]";
    node.type = "paragraph";
    node.text = paragraph.text;
    node.preview = paragraph.text;
    node.style = paragraph.style;
    node.format["index"] = std::to_string(paragraph.index);
    node.format["style"] = paragraph.style;
    node.format["text"] = paragraph.text;
    node.child_count = static_cast<int>(paragraph.runs.size());
    if (depth > 0) {
        for (const auto &run : paragraph.runs) {
            node.children.push_back(makeRunNode(run));
        }
    }
    return node;
}

DocumentNode makeBodyNode(const WordDocument &doc, int depth) {
    DocumentNode node;
    node.path = "/body";
    node.type = "body";
    node.child_count = static_cast<int>(doc.paragraphs.size());
    node.format["paragraphCount"] = std::to_string(doc.paragraphs.size());
    if (depth > 0) {
        for (const auto &paragraph : doc.paragraphs) {
            node.children.push_back(makeParagraphNode(paragraph, depth - 1));
        }
    }
    return node;
}

bool predicateMatchesParagraph(const WordParagraph &paragraph, const parser::PathPredicate &predicate, int sibling_count) {
    switch (predicate.kind) {
        case parser::PathPredicateKind::None:
            return true;
        case parser::PathPredicateKind::Index:
            return paragraph.index == predicate.index;
        case parser::PathPredicateKind::Last:
            return paragraph.index == sibling_count;
        case parser::PathPredicateKind::Attribute:
            return (predicate.name == "style" && paragraph.style == predicate.value) ||
                   (predicate.name == "text" && paragraph.text == predicate.value);
    }
    return false;
}

bool predicateMatchesRun(const WordRun &run, const parser::PathPredicate &predicate, int sibling_count) {
    switch (predicate.kind) {
        case parser::PathPredicateKind::None:
            return true;
        case parser::PathPredicateKind::Index:
            return run.run_index == predicate.index;
        case parser::PathPredicateKind::Last:
            return run.run_index == sibling_count;
        case parser::PathPredicateKind::Attribute:
            return (predicate.name == "text" && run.text == predicate.value) ||
                   (predicate.name == "bold" && (run.bold ? "true" : "false") == predicate.value) ||
                   (predicate.name == "italic" && (run.italic ? "true" : "false") == predicate.value);
    }
    return false;
}

std::string paragraphAttr(const WordParagraph &paragraph, std::string_view key) {
    if (key == "style") return paragraph.style;
    if (key == "text" || key == "value") return paragraph.text;
    if (key == "index") return std::to_string(paragraph.index);
    if (key == "type") return "paragraph";
    return {};
}

std::string runAttr(const WordRun &run, std::string_view key) {
    if (key == "text" || key == "value") return run.text;
    if (key == "bold") return run.bold ? "true" : "false";
    if (key == "italic") return run.italic ? "true" : "false";
    if (key == "index") return std::to_string(run.run_index);
    if (key == "type") return "run";
    return {};
}

std::optional<double> parseDouble(std::string_view value) {
    if (value.empty()) return std::nullopt;
    char *end = nullptr;
    auto text = std::string(value);
    auto parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') return std::nullopt;
    return parsed;
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

template <typename T, typename Attr>
bool filterMatches(const T &item, const parser::FilterExpr &expr, Attr attr) {
    if (expr.kind == parser::FilterExprKind::And) {
        return std::all_of(expr.parts.begin(), expr.parts.end(), [&](const auto &part) { return filterMatches(item, part, attr); });
    }
    if (expr.kind == parser::FilterExprKind::Or) {
        return std::any_of(expr.parts.begin(), expr.parts.end(), [&](const auto &part) { return filterMatches(item, part, attr); });
    }
    auto value = attr(item, expr.predicate.key);
    if (expr.predicate.op == "exists") return !value.empty();
    return compareValues(value, expr.predicate.op, expr.predicate.value);
}

bool pseudoMatchesParagraph(const WordParagraph &paragraph, const parser::PseudoClass &pseudo) {
    if (pseudo.name == "contains" && pseudo.arg.has_value()) return paragraph.text.find(*pseudo.arg) != std::string::npos;
    if (pseudo.name == "empty") return paragraph.text.empty();
    return false;
}

bool pseudoMatchesRun(const WordRun &run, const parser::PseudoClass &pseudo) {
    if (pseudo.name == "contains" && pseudo.arg.has_value()) return run.text.find(*pseudo.arg) != std::string::npos;
    if (pseudo.name == "empty") return run.text.empty();
    return false;
}

bool matchesParagraphStep(const WordParagraph &paragraph, const parser::SelectorStep &step) {
    if (!(step.subject == "paragraph" || step.subject == "p")) return false;
    for (const auto &filter : step.filters) {
        if (!filterMatches(paragraph, filter, paragraphAttr)) return false;
    }
    for (const auto &pseudo : step.pseudos) {
        if (!pseudoMatchesParagraph(paragraph, pseudo)) return false;
    }
    return true;
}

void collectFilterKeys(const parser::FilterExpr &expr, std::vector<std::string> &keys) {
    if (expr.kind == parser::FilterExprKind::Predicate) {
        keys.push_back(expr.predicate.key);
        return;
    }
    for (const auto &part : expr.parts) collectFilterKeys(part, keys);
}

bool knownWordAttr(std::string_view subject, std::string_view key) {
    if (subject == "paragraph" || subject == "p") {
        return key == "style" || key == "text" || key == "value" || key == "index" || key == "type";
    }
    if (subject == "run" || subject == "r") {
        return key == "text" || key == "value" || key == "bold" || key == "italic" || key == "index" || key == "type";
    }
    return false;
}

void collectMissingKeys(const parser::SelectorStep &step, std::set<std::string> &warnings) {
    std::vector<std::string> keys;
    for (const auto &filter : step.filters) collectFilterKeys(filter, keys);
    for (const auto &key : keys) {
        if (!knownWordAttr(step.subject, key)) {
            warnings.insert("missing-key:" + step.subject + "." + key);
        }
    }
}

bool matchesRunStep(const WordRun &run, const parser::SelectorStep &step) {
    if (!(step.subject == "run" || step.subject == "r")) return false;
    for (const auto &filter : step.filters) {
        if (!filterMatches(run, filter, runAttr)) return false;
    }
    for (const auto &pseudo : step.pseudos) {
        if (!pseudoMatchesRun(run, pseudo)) return false;
    }
    return true;
}

template <typename T>
std::vector<T> applyNth(std::vector<T> items, std::optional<int> nth) {
    if (!nth.has_value()) return items;
    if (*nth <= 0 || static_cast<std::size_t>(*nth) > items.size()) return {};
    return {items[static_cast<std::size_t>(*nth - 1)]};
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

ResolveResult resolveWordGet(const XlsxPackage &package, const parser::PathAst &path_ast, int depth) {
    if (auto meta = metadataTargetFromPath(path_ast); meta.has_value()) {
        if (*meta == "__invalid__") return fail("unsupported metadata path");
        return resolveOoxmlMetadataGet(package, *meta);
    }

    if (path_ast.kind != parser::PathAstKind::Path) {
        return fail("DOCX get supports stable slash paths only");
    }
    std::string error;
    auto doc = loadWordDocument(package, error);
    if (!error.empty()) return fail(error);

    if (path_ast.segments.empty()) return {.ok = true, .node = makeBodyNode(doc, depth)};
    if (path_ast.segments[0].name != "body") return fail("DOCX path must start with /body");
    if (path_ast.segments.size() == 1) return {.ok = true, .node = makeBodyNode(doc, depth)};
    if (!(path_ast.segments[1].name == "p" || path_ast.segments[1].name == "paragraph")) {
        return fail("DOCX get currently supports /body/p[...] paths");
    }

    std::vector<const WordParagraph *> paragraphs;
    for (const auto &paragraph : doc.paragraphs) {
        if (predicateMatchesParagraph(paragraph, path_ast.segments[1].predicate, static_cast<int>(doc.paragraphs.size()))) {
            paragraphs.push_back(&paragraph);
        }
    }
    if (paragraphs.empty()) return fail("paragraph not found");

    if (path_ast.segments.size() == 2) {
        if (paragraphs.size() == 1) return {.ok = true, .node = makeParagraphNode(*paragraphs.front(), depth)};
        std::vector<DocumentNode> children;
        for (const auto *paragraph : paragraphs) children.push_back(makeParagraphNode(*paragraph, depth));
        return {.ok = true, .node = makeQueryNode("/body/p", std::move(children))};
    }

    if (!(path_ast.segments[2].name == "r" || path_ast.segments[2].name == "run")) {
        return fail("DOCX get currently supports /body/p[...]/r[...] paths");
    }
    std::vector<DocumentNode> runs;
    for (const auto *paragraph : paragraphs) {
        for (const auto &run : paragraph->runs) {
            if (predicateMatchesRun(run, path_ast.segments[2].predicate, static_cast<int>(paragraph->runs.size()))) {
                runs.push_back(makeRunNode(run));
            }
        }
    }
    if (runs.empty()) return fail("run not found");
    if (runs.size() == 1) return {.ok = true, .node = std::move(runs.front())};
    return {.ok = true, .node = makeQueryNode("/body/p/r", std::move(runs))};
}

ResolveResult resolveWordQuery(const XlsxPackage &package, const parser::Selector &selector) {
    std::string error;
    auto doc = loadWordDocument(package, error);
    if (!error.empty()) return fail(error);

    std::vector<DocumentNode> results;
    std::set<std::string> seen;
    std::set<std::string> warnings;

    for (const auto &alternative : selector.alternatives) {
        if (alternative.steps.empty()) continue;
        if (alternative.steps.size() == 1) {
            const auto &step = alternative.steps.front();
            collectMissingKeys(step, warnings);
            if (step.subject == "paragraph" || step.subject == "p") {
                std::vector<const WordParagraph *> matched;
                for (const auto &paragraph : doc.paragraphs) {
                    if (matchesParagraphStep(paragraph, step)) matched.push_back(&paragraph);
                }
                matched = applyNth(std::move(matched), step.nth);
                for (const auto *paragraph : matched) {
                    auto node = makeParagraphNode(*paragraph, 1);
                    if (seen.insert(node.path).second) results.push_back(std::move(node));
                }
            } else if (step.subject == "run" || step.subject == "r") {
                std::vector<const WordRun *> matched;
                for (const auto &paragraph : doc.paragraphs) {
                    for (const auto &run : paragraph.runs) {
                        if (matchesRunStep(run, step)) matched.push_back(&run);
                    }
                }
                matched = applyNth(std::move(matched), step.nth);
                for (const auto *run : matched) {
                    auto node = makeRunNode(*run);
                    if (seen.insert(node.path).second) results.push_back(std::move(node));
                }
            } else {
                return fail("unsupported DOCX selector subject: " + step.subject);
            }
            continue;
        }

        if (alternative.steps.size() == 2) {
            const auto &first = alternative.steps[0];
            const auto &second = alternative.steps[1];
            collectMissingKeys(first, warnings);
            collectMissingKeys(second, warnings);
            if (!((first.subject == "paragraph" || first.subject == "p") && (second.subject == "run" || second.subject == "r"))) {
                return fail("DOCX chained selectors currently support paragraph > run only");
            }
            std::vector<const WordParagraph *> paragraphs;
            for (const auto &paragraph : doc.paragraphs) {
                if (matchesParagraphStep(paragraph, first)) paragraphs.push_back(&paragraph);
            }
            paragraphs = applyNth(std::move(paragraphs), first.nth);

            std::vector<const WordRun *> runs;
            for (const auto *paragraph : paragraphs) {
                for (const auto &run : paragraph->runs) {
                    if (matchesRunStep(run, second)) runs.push_back(&run);
                }
            }
            runs = applyNth(std::move(runs), second.nth);
            for (const auto *run : runs) {
                auto node = makeRunNode(*run);
                if (seen.insert(node.path).second) results.push_back(std::move(node));
            }
            continue;
        }

        return fail("DOCX query currently supports up to paragraph > run selector chains");
    }

    return {.ok = true, .node = makeQueryNode("query:" + selector.input, std::move(results), warnings)};
}

} // namespace office
