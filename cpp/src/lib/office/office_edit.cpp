#include "office_edit.h"

#include "document_node.h"
#include "office_parser.h"
#include "ooxml_zip.h"
#include "word_resolver.h"
#include "xlsx_package.h"
#include "xlsx_resolver.h"
#include "xlsx_workbook.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <map>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include <pugixml.hpp>

namespace office {
namespace {

constexpr const char *kGap =
    "not in this C++ port (charts, pivots, raw XML, animations, HTML preview stay on officecli)";

std::string lower(std::string value) {
    for (char &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool endsWith(std::string_view value, std::string_view suffix) {
    if (suffix.size() > value.size()) return false;
    const auto offset = value.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const auto a = static_cast<unsigned char>(value[offset + i]);
        const auto b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

std::string jsonEscape(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    const char *hex = "0123456789ABCDEF";
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0xF]);
                    out.push_back(hex[c & 0xF]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string jstr(std::string_view value) { return "\"" + jsonEscape(value) + "\""; }

EditResult okJson(std::string json) {
    EditResult result;
    result.ok = true;
    result.exit_code = 0;
    result.json = std::move(json);
    return result;
}

EditResult fail(std::string message) {
    EditResult result;
    result.error = std::move(message);
    result.json = "{\"ok\":false,\"error\":" + jstr(result.error) + "}";
    return result;
}

EditResult plainText(std::string text) {
    EditResult result;
    result.ok = true;
    result.exit_code = 0;
    result.plain = true;
    result.text = std::move(text);
    result.json = result.text;
    return result;
}

bool localNameEquals(const char *name, std::string_view expected) {
    if (name == nullptr) return false;
    std::string_view full(name);
    const auto colon = full.find(':');
    const auto local = colon == std::string_view::npos ? full : full.substr(colon + 1);
    return local == expected;
}

pugi::xml_node childByLocalName(pugi::xml_node parent, std::string_view name) {
    for (auto child : parent.children()) {
        if (localNameEquals(child.name(), name)) return child;
    }
    return {};
}

std::string attrByLocalName(pugi::xml_node node, std::string_view name) {
    for (auto attr : node.attributes()) {
        if (localNameEquals(attr.name(), name)) return attr.value();
    }
    return {};
}

void setAttr(pugi::xml_node node, const char *name, const std::string &value) {
    auto attr = node.attribute(name);
    if (!attr) attr = node.append_attribute(name);
    attr.set_value(value.c_str());
}

void removeAttrLocal(pugi::xml_node node, std::string_view name) {
    for (auto attr : node.attributes()) {
        if (localNameEquals(attr.name(), name)) {
            node.remove_attribute(attr);
            return;
        }
    }
}

std::vector<pugi::xml_node> childrenNamed(pugi::xml_node parent, std::string_view name) {
    std::vector<pugi::xml_node> nodes;
    for (auto child : parent.children()) {
        if (localNameEquals(child.name(), name)) nodes.push_back(child);
    }
    return nodes;
}

std::vector<pugi::xml_node> childrenNamedAny(pugi::xml_node parent, std::initializer_list<std::string_view> names) {
    std::vector<pugi::xml_node> nodes;
    for (auto child : parent.children()) {
        for (auto name : names) {
            if (localNameEquals(child.name(), name)) {
                nodes.push_back(child);
                break;
            }
        }
    }
    return nodes;
}

bool loadXml(const std::string &xml, pugi::xml_document &doc, std::string &error, const char *part) {
    const auto parsed = doc.load_string(xml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
    if (!parsed) {
        error = std::string("failed to parse ") + part + ": " + parsed.description();
        return false;
    }
    return true;
}

std::string saveXml(pugi::xml_document &doc) {
    if (!doc.first_child() || doc.first_child().type() != pugi::node_declaration) {
        auto decl = doc.prepend_child(pugi::node_declaration);
        decl.append_attribute("version") = "1.0";
        decl.append_attribute("encoding") = "UTF-8";
        decl.append_attribute("standalone") = "yes";
    }
    std::ostringstream out;
    doc.save(out, "", pugi::format_raw);
    return out.str();
}

const std::string *prop(const PropMap &props, std::string_view key) {
    auto found = props.find(std::string(key));
    if (found == props.end()) return nullptr;
    return &found->second;
}

bool truthy(std::string_view value) {
    const auto v = lower(std::string(value));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

int toInt(const PropMap &props, const char *key, int fallback) {
    const auto *value = prop(props, key);
    if (value == nullptr || value->empty()) return fallback;
    try {
        return std::stoi(*value);
    } catch (...) {
        return fallback;
    }
}

bool isNumber(std::string_view value) {
    if (value.empty()) return false;
    std::size_t i = 0;
    if (value[i] == '-' || value[i] == '+') ++i;
    if (i >= value.size()) return false;
    bool digit = false;
    bool dot = false;
    for (; i < value.size(); ++i) {
        const auto c = value[i];
        if (c >= '0' && c <= '9') digit = true;
        else if (c == '.' && !dot) dot = true;
        else return false;
    }
    return digit;
}

std::string hexColor(std::string_view value, std::string &error) {
    std::string raw(value);
    if (!raw.empty() && raw.front() == '#') raw.erase(raw.begin());
    const auto named = lower(raw);
    if (named == "red") return "FF0000";
    if (named == "green") return "00FF00";
    if (named == "blue") return "0000FF";
    if (named == "white") return "FFFFFF";
    if (named == "black") return "000000";
    if (named == "yellow") return "FFFF00";
    if (raw.size() == 6) {
        for (unsigned char c : raw) {
            if (!std::isxdigit(c)) {
                error = "color must be hex or a named color";
                return {};
            }
        }
        return raw;
    }
    error = "color must be hex or a named color";
    return {};
}

int pointsToHalfPoints(std::string_view value) {
    std::string raw(value);
    if (endsWith(raw, "pt")) raw.resize(raw.size() - 2);
    try {
        return static_cast<int>(std::stod(raw) * 2.0);
    } catch (...) {
        return 22;
    }
}

long long toEmu(std::string_view value) {
    std::string raw(value);
    double scale = 1.0;
    if (endsWith(raw, "cm")) {
        raw.resize(raw.size() - 2);
        scale = 360000.0;
    } else if (endsWith(raw, "in")) {
        raw.resize(raw.size() - 2);
        scale = 914400.0;
    } else if (endsWith(raw, "pt")) {
        raw.resize(raw.size() - 2);
        scale = 12700.0;
    } else if (endsWith(raw, "px")) {
        raw.resize(raw.size() - 2);
        scale = 9525.0;
    }
    try {
        return static_cast<long long>(std::stod(raw) * scale);
    } catch (...) {
        return 0;
    }
}

int wordCount(std::string_view text) {
    int count = 0;
    bool in_word = false;
    for (unsigned char c : text) {
        if (std::isspace(c)) in_word = false;
        else if (!in_word) {
            in_word = true;
            ++count;
        }
    }
    return count;
}

std::string replaceAll(std::string_view haystack, std::string_view needle, std::string_view replacement, int &matched) {
    matched = 0;
    if (needle.empty()) return std::string(haystack);
    std::string out;
    std::size_t cursor = 0;
    while (cursor <= haystack.size()) {
        const auto found = haystack.find(needle, cursor);
        if (found == std::string_view::npos) {
            out.append(haystack.substr(cursor));
            break;
        }
        out.append(haystack.substr(cursor, found - cursor));
        out.append(replacement);
        cursor = found + needle.size();
        ++matched;
    }
    return out;
}

struct Session {
    std::filesystem::path file;
    OoxmlParts package;
    std::string format;
    std::string error;

    bool load() {
        format = documentFormat(file);
        if (format.empty()) {
            error = "unsupported document extension (use .docx, .xlsx, or .pptx)";
            return false;
        }
        package = readOoxmlParts(file, error);
        return error.empty();
    }

    bool save() { return writeOoxmlParts(file, package, error); }

    std::string *part(const std::string &name) {
        auto found = package.parts.find(name);
        if (found == package.parts.end()) return nullptr;
        return &found->second;
    }
};

pugi::xml_node appendBeforeSect(pugi::xml_node body, const char *name) {
    auto sect = childByLocalName(body, "sectPr");
    if (sect) return body.insert_child_before(name, sect);
    return body.append_child(name);
}

void clearChildren(pugi::xml_node node) {
    while (auto child = node.first_child()) node.remove_child(child);
}

std::string nodeText(pugi::xml_node node) {
    std::ostringstream out;
    std::vector<pugi::xml_node> stack;
    stack.push_back(node);
    while (!stack.empty()) {
        auto current = stack.back();
        stack.pop_back();
        if (localNameEquals(current.name(), "t")) out << current.text().get();
        std::vector<pugi::xml_node> kids;
        for (auto child : current.children()) kids.push_back(child);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    }
    return out.str();
}

int pathIndex(const parser::PathPredicate &predicate, int count, std::string &error) {
    if (predicate.kind == parser::PathPredicateKind::Last) {
        if (count <= 0) {
            error = "no matching element";
            return 0;
        }
        return count;
    }
    if (predicate.kind == parser::PathPredicateKind::Index) {
        if (predicate.index < 1 || predicate.index > count) {
            error = "index out of range";
            return 0;
        }
        return predicate.index;
    }
    error = "expected an index predicate";
    return 0;
}

bool sameAlias(std::string_view name, std::initializer_list<std::string_view> aliases) {
    for (auto alias : aliases) {
        if (name == alias) return true;
    }
    return false;
}

// ── blank packages ──────────────────────────────────────────────────────────

const char *kContentTypesDocx = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
  <Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
  <Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
  <Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>
</Types>
)XML";

const char *kRelsRootDocx = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
  <Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>
</Relationships>
)XML";

const char *kDocumentXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:sectPr>
      <w:pgSz w:w="12240" w:h="15840"/>
      <w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440"/>
    </w:sectPr>
  </w:body>
</w:document>
)XML";

const char *kDocumentRels = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>
)XML";

const char *kStylesXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>
  <w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/></w:style>
  <w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/></w:style>
</w:styles>
)XML";

const char *kCoreXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/">
  <dc:title></dc:title>
  <dc:creator>pm-office</dc:creator>
</cp:coreProperties>
)XML";

const char *kAppDocx = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties">
  <Application>pm-office</Application>
</Properties>
)XML";

const char *kContentTypesXlsx = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
  <Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
  <Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>
  <Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
  <Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>
</Types>
)XML";

const char *kRelsRootXlsx = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
  <Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>
</Relationships>
)XML";

const char *kWorkbookXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
  <sheets>
    <sheet name="Sheet1" sheetId="1" r:id="rId1"/>
  </sheets>
</workbook>
)XML";

const char *kWorkbookRels = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>
)XML";

const char *kSheetXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <sheetData/>
</worksheet>
)XML";

const char *kXlsxStyles = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <fonts count="1"><font><sz val="11"/><name val="Calibri"/></font></fonts>
  <fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>
  <borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>
  <cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellXfs>
</styleSheet>
)XML";

const char *kContentTypesPptx = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/ppt/presentation.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/>
  <Override PartName="/ppt/slideMasters/slideMaster1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml"/>
  <Override PartName="/ppt/slideLayouts/slideLayout1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml"/>
  <Override PartName="/ppt/theme/theme1.xml" ContentType="application/vnd.openxmlformats-officedocument.theme+xml"/>
  <Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
  <Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>
</Types>
)XML";

const char *kRelsRootPptx = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="ppt/presentation.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
  <Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>
</Relationships>
)XML";

const char *kPresentationXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:presentation xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">
  <p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst>
  <p:sldIdLst/>
  <p:sldSz cx="12192000" cy="6858000"/>
  <p:notesSz cx="6858000" cy="9144000"/>
</p:presentation>
)XML";

const char *kPresentationRels = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster" Target="slideMasters/slideMaster1.xml"/>
</Relationships>
)XML";

const char *kSlideMaster = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sldMaster xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">
  <p:cSld><p:spTree>
    <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
    <p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>
  </p:spTree></p:cSld>
  <p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/>
  <p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/></p:sldLayoutIdLst>
</p:sldMaster>
)XML";

const char *kSlideMasterRels = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout" Target="../slideLayouts/slideLayout1.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme" Target="../theme/theme1.xml"/>
</Relationships>
)XML";

const char *kSlideLayout = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sldLayout xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" type="blank">
  <p:cSld name="Blank"><p:spTree>
    <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
    <p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>
  </p:spTree></p:cSld>
</p:sldLayout>
)XML";

const char *kSlideLayoutRels = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster" Target="../slideMasters/slideMaster1.xml"/>
</Relationships>
)XML";

const char *kThemeXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Office Theme">
  <a:themeElements>
    <a:clrScheme name="Office">
      <a:dk1><a:sysClr val="windowText" lastClr="000000"/></a:dk1>
      <a:lt1><a:sysClr val="window" lastClr="FFFFFF"/></a:lt1>
      <a:dk2><a:srgbClr val="1F497D"/></a:dk2>
      <a:lt2><a:srgbClr val="EEECE1"/></a:lt2>
      <a:accent1><a:srgbClr val="4F81BD"/></a:accent1>
      <a:accent2><a:srgbClr val="C0504D"/></a:accent2>
      <a:accent3><a:srgbClr val="9BBB59"/></a:accent3>
      <a:accent4><a:srgbClr val="8064A2"/></a:accent4>
      <a:accent5><a:srgbClr val="4BACC6"/></a:accent5>
      <a:accent6><a:srgbClr val="F79646"/></a:accent6>
      <a:hlink><a:srgbClr val="0000FF"/></a:hlink>
      <a:folHlink><a:srgbClr val="800080"/></a:folHlink>
    </a:clrScheme>
    <a:fontScheme name="Office">
      <a:majorFont><a:latin typeface="Calibri"/><a:ea typeface=""/><a:cs typeface=""/></a:majorFont>
      <a:minorFont><a:latin typeface="Calibri"/><a:ea typeface=""/><a:cs typeface=""/></a:minorFont>
    </a:fontScheme>
    <a:fmtScheme name="Office">
      <a:fillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:fillStyleLst>
      <a:lnStyleLst><a:ln w="9525"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln></a:lnStyleLst>
      <a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst>
      <a:bgFillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:bgFillStyleLst>
    </a:fmtScheme>
  </a:themeElements>
</a:theme>
)XML";

OoxmlParts blankDocx() {
    OoxmlParts parts;
    parts.parts["[Content_Types].xml"] = kContentTypesDocx;
    parts.parts["_rels/.rels"] = kRelsRootDocx;
    parts.parts["word/document.xml"] = kDocumentXml;
    parts.parts["word/_rels/document.xml.rels"] = kDocumentRels;
    parts.parts["word/styles.xml"] = kStylesXml;
    parts.parts["docProps/core.xml"] = kCoreXml;
    parts.parts["docProps/app.xml"] = kAppDocx;
    return parts;
}

OoxmlParts blankXlsx() {
    OoxmlParts parts;
    parts.parts["[Content_Types].xml"] = kContentTypesXlsx;
    parts.parts["_rels/.rels"] = kRelsRootXlsx;
    parts.parts["xl/workbook.xml"] = kWorkbookXml;
    parts.parts["xl/_rels/workbook.xml.rels"] = kWorkbookRels;
    parts.parts["xl/worksheets/sheet1.xml"] = kSheetXml;
    parts.parts["xl/styles.xml"] = kXlsxStyles;
    parts.parts["docProps/core.xml"] = kCoreXml;
    parts.parts["docProps/app.xml"] = kAppDocx;
    return parts;
}

OoxmlParts blankPptx() {
    OoxmlParts parts;
    parts.parts["[Content_Types].xml"] = kContentTypesPptx;
    parts.parts["_rels/.rels"] = kRelsRootPptx;
    parts.parts["ppt/presentation.xml"] = kPresentationXml;
    parts.parts["ppt/_rels/presentation.xml.rels"] = kPresentationRels;
    parts.parts["ppt/slideMasters/slideMaster1.xml"] = kSlideMaster;
    parts.parts["ppt/slideMasters/_rels/slideMaster1.xml.rels"] = kSlideMasterRels;
    parts.parts["ppt/slideLayouts/slideLayout1.xml"] = kSlideLayout;
    parts.parts["ppt/slideLayouts/_rels/slideLayout1.xml.rels"] = kSlideLayoutRels;
    parts.parts["ppt/theme/theme1.xml"] = kThemeXml;
    parts.parts["docProps/core.xml"] = kCoreXml;
    parts.parts["docProps/app.xml"] = kAppDocx;
    return parts;
}

// ── docx mutation ───────────────────────────────────────────────────────────

pugi::xml_node ensureRunProps(pugi::xml_node run) {
    auto props = childByLocalName(run, "rPr");
    if (!props) props = run.prepend_child("w:rPr");
    return props;
}

void applyRunProps(pugi::xml_node run, const PropMap &props, std::string &error) {
    const bool any = prop(props, "bold") || prop(props, "italic") || prop(props, "color") ||
                     prop(props, "size") || prop(props, "font");
    if (!any) return;
    auto r_pr = ensureRunProps(run);
    if (const auto *bold = prop(props, "bold")) {
        auto node = childByLocalName(r_pr, "b");
        if (truthy(*bold)) {
            if (!node) r_pr.append_child("w:b");
        } else if (node) {
            r_pr.remove_child(node);
        }
    }
    if (const auto *italic = prop(props, "italic")) {
        auto node = childByLocalName(r_pr, "i");
        if (truthy(*italic)) {
            if (!node) r_pr.append_child("w:i");
        } else if (node) {
            r_pr.remove_child(node);
        }
    }
    if (const auto *color = prop(props, "color")) {
        auto hex = hexColor(*color, error);
        if (!error.empty()) return;
        auto node = childByLocalName(r_pr, "color");
        if (!node) node = r_pr.append_child("w:color");
        setAttr(node, "w:val", hex);
    }
    if (const auto *size = prop(props, "size")) {
        auto node = childByLocalName(r_pr, "sz");
        if (!node) node = r_pr.append_child("w:sz");
        setAttr(node, "w:val", std::to_string(pointsToHalfPoints(*size)));
    }
    if (const auto *font = prop(props, "font")) {
        auto node = childByLocalName(r_pr, "rFonts");
        if (!node) node = r_pr.append_child("w:rFonts");
        setAttr(node, "w:ascii", *font);
        setAttr(node, "w:hAnsi", *font);
    }
}

void setRunText(pugi::xml_node run, const std::string &text) {
    auto text_node = childByLocalName(run, "t");
    if (!text_node) text_node = run.append_child("w:t");
    if (text.empty() || std::isspace(static_cast<unsigned char>(text.front())) ||
        std::isspace(static_cast<unsigned char>(text.back()))) {
        setAttr(text_node, "xml:space", "preserve");
    }
    text_node.text().set(text.c_str());
}

pugi::xml_node makeRun(pugi::xml_node paragraph, const std::string &text, const PropMap &props, std::string &error) {
    auto run = paragraph.append_child("w:r");
    applyRunProps(run, props, error);
    setRunText(run, text);
    return run;
}

void applyParagraphStyle(pugi::xml_node paragraph, const PropMap &props) {
    const auto *style = prop(props, "style");
    if (style == nullptr) return;
    auto p_pr = childByLocalName(paragraph, "pPr");
    if (!p_pr) p_pr = paragraph.prepend_child("w:pPr");
    auto p_style = childByLocalName(p_pr, "pStyle");
    if (!p_style) p_style = p_pr.append_child("w:pStyle");
    setAttr(p_style, "w:val", *style);
}

void rewriteParagraphText(pugi::xml_node paragraph, const std::string &text, const PropMap &props, std::string &error) {
    applyParagraphStyle(paragraph, props);
    std::vector<pugi::xml_node> runs = childrenNamed(paragraph, "r");
    for (auto run : runs) paragraph.remove_child(run);
    makeRun(paragraph, text, props, error);
}

int rewriteParagraphFind(pugi::xml_node paragraph, std::string_view find, std::string_view replacement,
                         bool replace, const PropMap &props, std::string &error) {
    const auto full = nodeText(paragraph);
    if (find.empty() || full.find(find) == std::string::npos) return 0;
    if (!replace) {
        applyParagraphStyle(paragraph, props);
        for (auto run : childrenNamed(paragraph, "r")) applyRunProps(run, props, error);
        return 1;
    }
    int matched = 0;
    const auto next = replaceAll(full, find, replacement, matched);
    rewriteParagraphText(paragraph, next, props, error);
    return matched;
}

pugi::xml_document *loadPart(Session &session, const std::string &name, pugi::xml_document &doc) {
    auto *xml = session.part(name);
    if (xml == nullptr) {
        session.error = "missing part: " + name;
        return nullptr;
    }
    if (!loadXml(*xml, doc, session.error, name.c_str())) return nullptr;
    return &doc;
}

void storePart(Session &session, const std::string &name, pugi::xml_document &doc) {
    session.package.parts[name] = saveXml(doc);
}

pugi::xml_node docxBody(pugi::xml_document &doc, std::string &error) {
    auto body = childByLocalName(doc.document_element(), "body");
    if (!body) error = "word/document.xml has no body";
    return body;
}

struct DocxPlace {
    pugi::xml_node parent;
    std::string kind;
};

DocxPlace locateDocx(pugi::xml_node body, const parser::PathAst &path, std::string &error) {
    DocxPlace place;
    place.parent = body;
    place.kind = "body";
    std::size_t start = 0;
    if (!path.segments.empty() && path.segments[0].name == "body") start = 1;
    if (path.segments.empty() || (path.segments.size() == 1 && path.segments[0].name == "body")) return place;
    pugi::xml_node current = body;
    for (std::size_t i = start; i < path.segments.size(); ++i) {
        const auto &segment = path.segments[i];
        const char *xml_name = nullptr;
        std::string kind;
        if (sameAlias(segment.name, {"p", "paragraph"})) {
            xml_name = "p";
            kind = "paragraph";
        } else if (sameAlias(segment.name, {"r", "run"})) {
            xml_name = "r";
            kind = "run";
        } else if (sameAlias(segment.name, {"tbl", "table"})) {
            xml_name = "tbl";
            kind = "table";
        } else if (sameAlias(segment.name, {"tr", "row"})) {
            xml_name = "tr";
            kind = "row";
        } else if (sameAlias(segment.name, {"tc", "td", "cell"})) {
            xml_name = "tc";
            kind = "cell";
        } else {
            error = "unsupported docx path segment: " + segment.name;
            return {};
        }
        auto nodes = childrenNamed(current, xml_name);
        int index = 0;
        if (segment.predicate.kind == parser::PathPredicateKind::None) {
            error = "docx path segment needs an index: " + segment.name;
            return {};
        }
        if (segment.predicate.kind == parser::PathPredicateKind::Attribute) {
            error = "docx attribute paths are not edited in this build";
            return {};
        }
        index = pathIndex(segment.predicate, static_cast<int>(nodes.size()), error);
        if (!error.empty()) return {};
        current = nodes[static_cast<std::size_t>(index - 1)];
        place.parent = current;
        place.kind = kind;
    }
    return place;
}

pugi::xml_node insertNamed(pugi::xml_node parent, const char *name, std::initializer_list<std::string_view> sibling_names,
                           std::string_view after, std::string_view before, int index, pugi::xml_document &doc,
                           const std::string &format, std::string &error) {
    auto place_anchor = [&](std::string_view anchor) -> pugi::xml_node {
        if (anchor.empty()) return {};
        if (anchor.rfind("find:", 0) == 0) {
            error = "text-anchored insert is not in this build";
            return {};
        }
        auto parsed = parser::parsePath(anchor, format);
        if (!parsed.ok) {
            error = parsed.error;
            return {};
        }
        auto body = docxBody(doc, error);
        if (!body) return {};
        auto place = locateDocx(body, parsed.ast, error);
        return place.parent;
    };
    pugi::xml_node anchor;
    bool after_anchor = false;
    if (!after.empty()) {
        anchor = place_anchor(after);
        after_anchor = true;
    } else if (!before.empty()) {
        anchor = place_anchor(before);
    }
    if (!error.empty()) return {};
    if (anchor) {
        if (after_anchor) return parent.insert_child_after(name, anchor);
        return parent.insert_child_before(name, anchor);
    }
    if (index >= 0) {
        auto nodes = childrenNamedAny(parent, sibling_names);
        if (index < static_cast<int>(nodes.size())) return parent.insert_child_before(name, nodes[static_cast<std::size_t>(index)]);
    }
    if (childByLocalName(parent, "sectPr")) return appendBeforeSect(parent, name);
    return parent.append_child(name);
}

EditResult addDocx(Session &session, std::string_view parent_path, std::string_view type, const PropMap &props,
                   std::string_view after, std::string_view before, int index) {
    pugi::xml_document doc;
    if (!loadPart(session, "word/document.xml", doc)) return fail(session.error);
    auto body = docxBody(doc, session.error);
    if (!body) return fail(session.error);
    auto parsed = parser::parsePath(parent_path.empty() ? "/" : parent_path, "docx");
    if (!parsed.ok) return fail(parsed.error);
    auto place = locateDocx(body, parsed.ast, session.error);
    if (!session.error.empty()) return fail(session.error);
    const auto kind = lower(std::string(type));
    std::string created;
    if (kind == "paragraph" || kind == "p") {
        if (place.kind != "body" && place.kind != "cell") return fail("paragraph parent must be /body or a cell");
        auto paragraph = insertNamed(place.parent, "w:p", {"p"}, after, before, index, doc, "docx", session.error);
        if (!session.error.empty()) return fail(session.error);
        const auto *text = prop(props, "text");
        applyParagraphStyle(paragraph, props);
        if (text != nullptr) makeRun(paragraph, *text, props, session.error);
        if (!session.error.empty()) return fail(session.error);
        auto paragraphs = childrenNamed(place.kind == "body" ? body : place.parent, "p");
        int n = 1;
        for (auto node : paragraphs) {
            if (node == paragraph) break;
            ++n;
        }
        created = place.kind == "body" ? "/body/p[" + std::to_string(n) + "]" : std::string(parent_path) + "/p[" + std::to_string(n) + "]";
    } else if (kind == "run" || kind == "r") {
        if (place.kind != "paragraph") return fail("run parent must be a paragraph");
        auto run = insertNamed(place.parent, "w:r", {"r"}, after, before, index, doc, "docx", session.error);
        if (!session.error.empty()) return fail(session.error);
        const auto *text = prop(props, "text");
        applyRunProps(run, props, session.error);
        setRunText(run, text != nullptr ? *text : "");
        if (!session.error.empty()) return fail(session.error);
        created = std::string(parent_path) + "/r[" + std::to_string(static_cast<int>(childrenNamed(place.parent, "r").size())) + "]";
    } else if (kind == "table" || kind == "tbl") {
        if (place.kind != "body") return fail("table parent must be /body");
        const int rows = std::max(1, toInt(props, "rows", 1));
        const int cols = std::max(1, toInt(props, "cols", 1));
        if (rows > 200 || cols > 40) return fail("table is limited to 200 rows and 40 columns");
        auto table = insertNamed(place.parent, "w:tbl", {"tbl"}, after, before, index, doc, "docx", session.error);
        if (!session.error.empty()) return fail(session.error);
        for (int r = 0; r < rows; ++r) {
            auto row = table.append_child("w:tr");
            for (int c = 0; c < cols; ++c) {
                auto cell = row.append_child("w:tc");
                cell.append_child("w:p");
            }
        }
        created = "/body/tbl[" + std::to_string(static_cast<int>(childrenNamed(body, "tbl").size())) + "]";
    } else if (kind == "row" || kind == "tr") {
        if (place.kind != "table") return fail("row parent must be a table");
        const int cols = std::max(1, static_cast<int>(childrenNamed(childByLocalName(place.parent, "tr"), "tc").size()));
        auto row = insertNamed(place.parent, "w:tr", {"tr"}, after, before, index, doc, "docx", session.error);
        if (!session.error.empty()) return fail(session.error);
        const int width = std::max(1, cols);
        for (int c = 0; c < width; ++c) row.append_child("w:tc").append_child("w:p");
        created = std::string(parent_path) + "/tr[" + std::to_string(static_cast<int>(childrenNamed(place.parent, "tr").size())) + "]";
    } else if (kind == "cell" || kind == "tc" || kind == "td") {
        if (place.kind != "row") return fail("cell parent must be a row");
        auto cell = insertNamed(place.parent, "w:tc", {"tc"}, after, before, index, doc, "docx", session.error);
        if (!session.error.empty()) return fail(session.error);
        auto paragraph = cell.append_child("w:p");
        if (const auto *text = prop(props, "text")) makeRun(paragraph, *text, props, session.error);
        if (!session.error.empty()) return fail(session.error);
        created = std::string(parent_path) + "/tc[" + std::to_string(static_cast<int>(childrenNamed(place.parent, "tc").size())) + "]";
    } else {
        return fail(std::string("unsupported docx type '") + std::string(type) + "'; supported: paragraph, run, table, row, cell. " + kGap);
    }
    storePart(session, "word/document.xml", doc);
    return okJson("{\"ok\":true,\"format\":\"docx\",\"path\":" + jstr(created) + "}");
}

EditResult setDocx(Session &session, std::string_view node, const PropMap &props, std::string_view find, std::string_view replacement) {
    pugi::xml_document doc;
    if (!loadPart(session, "word/document.xml", doc)) return fail(session.error);
    auto body = docxBody(doc, session.error);
    if (!body) return fail(session.error);
    const bool whole = node.empty() || node == "/";
    int matched = 0;
    if (!find.empty()) {
        std::vector<pugi::xml_node> paragraphs;
        if (whole) {
            paragraphs = childrenNamed(body, "p");
            for (auto table : childrenNamed(body, "tbl")) {
                for (auto row : childrenNamed(table, "tr")) {
                    for (auto cell : childrenNamed(row, "tc")) {
                        for (auto paragraph : childrenNamed(cell, "p")) paragraphs.push_back(paragraph);
                    }
                }
            }
        } else {
            auto parsed = parser::parsePath(node, "docx");
            if (!parsed.ok) return fail(parsed.error);
            auto place = locateDocx(body, parsed.ast, session.error);
            if (!session.error.empty()) return fail(session.error);
            if (place.kind == "paragraph") paragraphs.push_back(place.parent);
            else if (place.kind == "body" || place.kind == "cell" || place.kind == "table") {
                for (auto paragraph : childrenNamed(place.parent, "p")) paragraphs.push_back(paragraph);
            } else {
                return fail("find/replace parent must be /, a paragraph, a cell, or a table");
            }
        }
        const bool replace = !replacement.empty() || prop(props, "replace") != nullptr;
        std::string_view repl = replacement;
        std::string repl_store;
        if (const auto *prop_repl = prop(props, "replace")) {
            repl_store = *prop_repl;
            repl = repl_store;
        }
        for (auto paragraph : paragraphs) {
            matched += rewriteParagraphFind(paragraph, find, repl, replace || !replacement.empty() || prop(props, "replace"), props, session.error);
            if (!session.error.empty()) return fail(session.error);
        }
    } else {
        auto parsed = parser::parsePath(node, "docx");
        if (!parsed.ok) return fail(parsed.error);
        auto place = locateDocx(body, parsed.ast, session.error);
        if (!session.error.empty()) return fail(session.error);
        if (place.kind == "paragraph" || place.kind == "cell") {
            auto paragraph = place.kind == "paragraph" ? place.parent : childByLocalName(place.parent, "p");
            if (!paragraph) paragraph = place.parent.append_child("w:p");
            if (const auto *text = prop(props, "text")) rewriteParagraphText(paragraph, *text, props, session.error);
            else {
                applyParagraphStyle(paragraph, props);
                auto runs = childrenNamed(paragraph, "r");
                if (runs.empty() && (prop(props, "bold") || prop(props, "italic") || prop(props, "color"))) {
                    makeRun(paragraph, "", props, session.error);
                } else {
                    for (auto run : runs) applyRunProps(run, props, session.error);
                }
            }
            if (!session.error.empty()) return fail(session.error);
        } else if (place.kind == "run") {
            if (const auto *text = prop(props, "text")) setRunText(place.parent, *text);
            applyRunProps(place.parent, props, session.error);
            if (!session.error.empty()) return fail(session.error);
        } else {
            return fail("set a paragraph, run, or cell, or use / with --find");
        }
        matched = 1;
    }
    storePart(session, "word/document.xml", doc);
    return okJson("{\"ok\":true,\"format\":\"docx\",\"matched\":" + std::to_string(matched) + "}");
}

EditResult removeDocx(Session &session, std::string_view node) {
    pugi::xml_document doc;
    if (!loadPart(session, "word/document.xml", doc)) return fail(session.error);
    auto body = docxBody(doc, session.error);
    if (!body) return fail(session.error);
    auto parsed = parser::parsePath(node, "docx");
    if (!parsed.ok) return fail(parsed.error);
    if (parsed.ast.segments.empty()) return fail("remove needs an element path");
    auto place = locateDocx(body, parsed.ast, session.error);
    if (!session.error.empty()) return fail(session.error);
    if (place.kind == "body") return fail("cannot remove the document body");
    auto parent = place.parent.parent();
    parent.remove_child(place.parent);
    storePart(session, "word/document.xml", doc);
    return okJson("{\"ok\":true,\"format\":\"docx\",\"removed\":" + jstr(node) + "}");
}

std::string docxPlainText(pugi::xml_node body) {
    std::ostringstream out;
    bool first = true;
    auto emit = [&](pugi::xml_node paragraph) {
        if (!first) out << "\n";
        first = false;
        out << nodeText(paragraph);
    };
    for (auto paragraph : childrenNamed(body, "p")) emit(paragraph);
    for (auto table : childrenNamed(body, "tbl")) {
        for (auto row : childrenNamed(table, "tr")) {
            for (auto cell : childrenNamed(row, "tc")) {
                for (auto paragraph : childrenNamed(cell, "p")) emit(paragraph);
            }
        }
    }
    return out.str();
}

EditResult moveDocx(Session &session, std::string_view node, std::string_view after, std::string_view before, int index) {
    pugi::xml_document doc;
    if (!loadPart(session, "word/document.xml", doc)) return fail(session.error);
    auto body = docxBody(doc, session.error);
    if (!body) return fail(session.error);
    auto parsed = parser::parsePath(node, "docx");
    if (!parsed.ok) return fail(parsed.error);
    auto place = locateDocx(body, parsed.ast, session.error);
    if (!session.error.empty()) return fail(session.error);
    if (place.kind != "paragraph" && place.kind != "table") return fail("move supports docx paragraphs and tables");
    auto parent = place.parent.parent();
    pugi::xml_node moved = place.parent;
    if (!after.empty() || !before.empty()) {
        auto anchor_path = !after.empty() ? after : before;
        auto anchor_parsed = parser::parsePath(anchor_path, "docx");
        if (!anchor_parsed.ok) return fail(anchor_parsed.error);
        auto anchor = locateDocx(body, anchor_parsed.ast, session.error);
        if (!session.error.empty()) return fail(session.error);
        if (!after.empty()) parent.insert_move_after(moved, anchor.parent);
        else parent.insert_move_before(moved, anchor.parent);
    } else if (index >= 0) {
        auto siblings = place.kind == "paragraph" ? childrenNamed(parent, "p") : childrenNamed(parent, "tbl");
        if (index >= static_cast<int>(siblings.size())) parent.append_move(moved);
        else parent.insert_move_before(moved, siblings[static_cast<std::size_t>(index)]);
    } else {
        return fail("move needs --after, --before, or --index");
    }
    storePart(session, "word/document.xml", doc);
    return okJson("{\"ok\":true,\"format\":\"docx\",\"moved\":" + jstr(node) + "}");
}

// ── xlsx mutation ───────────────────────────────────────────────────────────

struct CellAddr {
    std::string sheet;
    std::string col;
    int row = 0;
    std::string ref;
};

int colToIndex(std::string_view col) {
    int value = 0;
    for (unsigned char c : col) {
        if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
        if (c < 'A' || c > 'Z') return 0;
        value = value * 26 + (c - 'A' + 1);
    }
    return value;
}

std::string indexToCol(int index) {
    std::string col;
    while (index > 0) {
        --index;
        col.insert(col.begin(), static_cast<char>('A' + (index % 26)));
        index /= 26;
    }
    return col;
}

bool splitRef(std::string_view ref, std::string &col, int &row) {
    std::size_t i = 0;
    while (i < ref.size() && std::isalpha(static_cast<unsigned char>(ref[i]))) ++i;
    if (i == 0 || i == ref.size()) return false;
    col = std::string(ref.substr(0, i));
    try {
        row = std::stoi(std::string(ref.substr(i)));
    } catch (...) {
        return false;
    }
    return row > 0 && colToIndex(col) > 0;
}

bool parseCellTarget(std::string_view node, CellAddr &addr, std::string &error) {
    auto parsed = parser::parsePath(node, "xlsx");
    if (!parsed.ok) {
        error = parsed.error;
        return false;
    }
    if (parsed.ast.kind == parser::PathAstKind::ExcelReference) {
        const auto &excel = parsed.ast.excel;
        if (excel.reference_kind != parser::ExcelReferenceKind::Cell) {
            error = "set a single cell (ranges are read-only)";
            return false;
        }
        addr.sheet = excel.sheet;
        addr.col = excel.cell.col;
        addr.row = excel.cell.row;
        addr.ref = addr.col + std::to_string(addr.row);
        return !addr.sheet.empty() && addr.row > 0;
    }
    if (parsed.ast.segments.size() == 2 && parsed.ast.segments[1].predicate.kind == parser::PathPredicateKind::None &&
        splitRef(parsed.ast.segments[1].name, addr.col, addr.row)) {
        addr.sheet = parsed.ast.segments[0].name;
        addr.ref = addr.col + std::to_string(addr.row);
        return true;
    }
    std::string ref;
    for (const auto &segment : parsed.ast.segments) {
        if (sameAlias(segment.name, {"sheet", "worksheet"})) {
            if (segment.predicate.kind == parser::PathPredicateKind::Attribute && segment.predicate.name == "name") {
                addr.sheet = segment.predicate.value;
            } else if (segment.predicate.kind == parser::PathPredicateKind::Index) {
                addr.sheet = "#" + std::to_string(segment.predicate.index);
            }
        } else if (sameAlias(segment.name, {"cell", "c"})) {
            if (segment.predicate.kind == parser::PathPredicateKind::Attribute &&
                (segment.predicate.name == "ref" || segment.predicate.name == "r")) {
                ref = segment.predicate.value;
            }
        } else if (splitRef(segment.name, addr.col, addr.row)) {
            addr.ref = segment.name;
        }
    }
    if (!ref.empty() && !splitRef(ref, addr.col, addr.row)) {
        error = "cell ref is not A1";
        return false;
    }
    if (addr.sheet.empty() || addr.row <= 0) {
        error = "cell path needs a sheet and an A1 ref";
        return false;
    }
    addr.ref = addr.col + std::to_string(addr.row);
    for (char &c : addr.col) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    addr.ref = addr.col + std::to_string(addr.row);
    return true;
}

struct SheetLoc {
    pugi::xml_node sheet;
    std::string name;
    std::string part;
    int index = 0;
};

std::map<std::string, std::string> workbookRels(pugi::xml_document &rels) {
    std::map<std::string, std::string> out;
    for (auto rel : rels.document_element().children()) {
        if (!localNameEquals(rel.name(), "Relationship")) continue;
        auto id = attrByLocalName(rel, "Id");
        auto target = attrByLocalName(rel, "Target");
        if (id.empty() || target.empty()) continue;
        if (target.front() != '/') target = "xl/" + target;
        else target.erase(target.begin());
        out[id] = target;
    }
    return out;
}

SheetLoc findSheet(pugi::xml_document &workbook, const std::map<std::string, std::string> &rels, const std::string &name_or_index, std::string &error) {
    auto sheets = childByLocalName(workbook.document_element(), "sheets");
    int index = 0;
    const bool by_index = !name_or_index.empty() && name_or_index.front() == '#';
    int wanted = 0;
    if (by_index) {
        try {
            wanted = std::stoi(name_or_index.substr(1));
        } catch (...) {
            wanted = 0;
        }
    }
    for (auto sheet : sheets.children()) {
        if (!localNameEquals(sheet.name(), "sheet")) continue;
        ++index;
        const auto name = attrByLocalName(sheet, "name");
        if ((by_index && index == wanted) || (!by_index && name == name_or_index)) {
            SheetLoc loc;
            loc.sheet = sheet;
            loc.name = name;
            loc.index = index;
            auto id = attrByLocalName(sheet, "id");
            auto rel = rels.find(id);
            if (rel == rels.end()) {
                error = "sheet has no worksheet relationship: " + name;
                return {};
            }
            loc.part = rel->second;
            return loc;
        }
    }
    error = "sheet not found: " + name_or_index;
    return {};
}

int ensureFontStyle(pugi::xml_document &styles, bool bold, bool italic) {
    auto root = styles.document_element();
    auto fonts = childByLocalName(root, "fonts");
    if (!fonts) return 0;
    int font_index = 0;
    int match = -1;
    for (auto font : fonts.children()) {
        if (!localNameEquals(font.name(), "font")) continue;
        const bool has_bold = static_cast<bool>(childByLocalName(font, "b"));
        const bool has_italic = static_cast<bool>(childByLocalName(font, "i"));
        if (has_bold == bold && has_italic == italic) {
            match = font_index;
            break;
        }
        ++font_index;
    }
    if (match < 0) {
        auto font = fonts.append_child("font");
        if (bold) font.append_child("b");
        if (italic) font.append_child("i");
        setAttr(font.append_child("sz"), "val", "11");
        setAttr(font.append_child("name"), "val", "Calibri");
        match = font_index;
        setAttr(fonts, "count", std::to_string(match + 1));
    }
    auto xfs = childByLocalName(root, "cellXfs");
    if (!xfs) return 0;
    int xf_index = 0;
    for (auto xf : xfs.children()) {
        if (!localNameEquals(xf.name(), "xf")) continue;
        if (xf.attribute("fontId").as_int() == match) return xf_index;
        ++xf_index;
    }
    auto xf = xfs.append_child("xf");
    setAttr(xf, "numFmtId", "0");
    setAttr(xf, "fontId", std::to_string(match));
    setAttr(xf, "fillId", "0");
    setAttr(xf, "borderId", "0");
    setAttr(xf, "applyFont", "1");
    setAttr(xfs, "count", std::to_string(xf_index + 1));
    return xf_index;
}

void writeCellValue(pugi::xml_node cell, const std::string &value) {
    clearChildren(cell);
    removeAttrLocal(cell, "t");
    if (!value.empty() && value.front() == '=') {
        cell.append_child("f").text().set(value.c_str() + 1);
        return;
    }
    if (isNumber(value)) {
        cell.append_child("v").text().set(value.c_str());
        return;
    }
    setAttr(cell, "t", "inlineStr");
    auto text = cell.append_child("is").append_child("t");
    if (!value.empty() && (std::isspace(static_cast<unsigned char>(value.front())) ||
                           std::isspace(static_cast<unsigned char>(value.back())))) {
        setAttr(text, "xml:space", "preserve");
    }
    text.text().set(value.c_str());
}

std::string cellDisplay(pugi::xml_node cell) {
    if (attrByLocalName(cell, "t") == "inlineStr") return nodeText(cell);
    auto formula = childByLocalName(cell, "f");
    if (formula) return std::string("=") + formula.text().get();
    auto value = childByLocalName(cell, "v");
    if (value) return value.text().get();
    return {};
}

pugi::xml_node ensureRow(pugi::xml_node sheet_data, int row_number) {
    pugi::xml_node insert_before;
    for (auto row : childrenNamed(sheet_data, "row")) {
        int current = 0;
        try {
            current = std::stoi(attrByLocalName(row, "r"));
        } catch (...) {
            current = 0;
        }
        if (current == row_number) return row;
        if (current > row_number) {
            insert_before = row;
            break;
        }
    }
    pugi::xml_node row = insert_before ? sheet_data.insert_child_before("row", insert_before) : sheet_data.append_child("row");
    setAttr(row, "r", std::to_string(row_number));
    return row;
}

pugi::xml_node ensureCell(pugi::xml_node row, const std::string &ref, int col_index) {
    pugi::xml_node insert_before;
    for (auto cell : childrenNamed(row, "c")) {
        std::string col;
        int cell_row = 0;
        if (!splitRef(attrByLocalName(cell, "r"), col, cell_row)) continue;
        const int current = colToIndex(col);
        if (current == col_index) return cell;
        if (current > col_index) {
            insert_before = cell;
            break;
        }
    }
    pugi::xml_node cell = insert_before ? row.insert_child_before("c", insert_before) : row.append_child("c");
    setAttr(cell, "r", ref);
    return cell;
}

int nextRelationshipId(pugi::xml_document &rels) {
    int max_id = 0;
    for (auto rel : rels.document_element().children()) {
        if (!localNameEquals(rel.name(), "Relationship")) continue;
        auto id = attrByLocalName(rel, "Id");
        if (id.rfind("rId", 0) == 0) {
            try {
                max_id = std::max(max_id, std::stoi(id.substr(3)));
            } catch (...) {
            }
        }
    }
    return max_id + 1;
}

void addContentOverride(Session &session, const std::string &part, const char *content_type) {
    pugi::xml_document types;
    if (!loadPart(session, "[Content_Types].xml", types)) return;
    const auto part_name = "/" + part;
    for (auto node : types.document_element().children()) {
        if (localNameEquals(node.name(), "Override") && attrByLocalName(node, "PartName") == part_name) {
            storePart(session, "[Content_Types].xml", types);
            return;
        }
    }
    auto over = types.document_element().append_child("Override");
    setAttr(over, "PartName", part_name);
    setAttr(over, "ContentType", content_type);
    storePart(session, "[Content_Types].xml", types);
    session.error.clear();
}

EditResult setXlsxCell(Session &session, const CellAddr &addr, const PropMap &props) {
    pugi::xml_document workbook;
    pugi::xml_document rels;
    if (!loadPart(session, "xl/workbook.xml", workbook)) return fail(session.error);
    if (!loadPart(session, "xl/_rels/workbook.xml.rels", rels)) return fail(session.error);
    auto sheet = findSheet(workbook, workbookRels(rels), addr.sheet, session.error);
    if (!session.error.empty()) return fail(session.error);
    pugi::xml_document worksheet;
    if (!loadPart(session, sheet.part, worksheet)) return fail(session.error);
    auto sheet_data = childByLocalName(worksheet.document_element(), "sheetData");
    if (!sheet_data) sheet_data = worksheet.document_element().append_child("sheetData");
    auto row = ensureRow(sheet_data, addr.row);
    auto cell = ensureCell(row, addr.ref, colToIndex(addr.col));
    if (const auto *value = prop(props, "value")) writeCellValue(cell, *value);
    else if (const auto *text = prop(props, "text")) writeCellValue(cell, *text);
    if (prop(props, "bold") || prop(props, "italic")) {
        pugi::xml_document styles;
        if (!session.part("xl/styles.xml")) session.package.parts["xl/styles.xml"] = kXlsxStyles;
        if (!loadPart(session, "xl/styles.xml", styles)) return fail(session.error);
        const bool bold = prop(props, "bold") && truthy(*prop(props, "bold"));
        const bool italic = prop(props, "italic") && truthy(*prop(props, "italic"));
        if (!bold && !italic) removeAttrLocal(cell, "s");
        else setAttr(cell, "s", std::to_string(ensureFontStyle(styles, bold, italic)));
        storePart(session, "xl/styles.xml", styles);
    }
    storePart(session, sheet.part, worksheet);
    return okJson("{\"ok\":true,\"format\":\"xlsx\",\"path\":" + jstr(sheet.name + "!" + addr.ref) + "}");
}

EditResult addXlsx(Session &session, std::string_view parent_path, std::string_view type, const PropMap &props, int index) {
    const auto kind = lower(std::string(type));
    if (kind == "sheet") {
        pugi::xml_document workbook;
        pugi::xml_document rels;
        if (!loadPart(session, "xl/workbook.xml", workbook)) return fail(session.error);
        if (!loadPart(session, "xl/_rels/workbook.xml.rels", rels)) return fail(session.error);
        auto sheets = childByLocalName(workbook.document_element(), "sheets");
        int count = static_cast<int>(childrenNamed(sheets, "sheet").size());
        std::string name = prop(props, "name") ? *prop(props, "name") : "Sheet" + std::to_string(count + 1);
        for (auto sheet : childrenNamed(sheets, "sheet")) {
            if (attrByLocalName(sheet, "name") == name) return fail("sheet already exists: " + name);
        }
        const auto part = "xl/worksheets/sheet" + std::to_string(count + 1) + ".xml";
        const auto rel_id = "rId" + std::to_string(nextRelationshipId(rels));
        auto rel = rels.document_element().append_child("Relationship");
        setAttr(rel, "Id", rel_id);
        setAttr(rel, "Type", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet");
        setAttr(rel, "Target", "worksheets/sheet" + std::to_string(count + 1) + ".xml");
        auto sheet = sheets.append_child("sheet");
        setAttr(sheet, "name", name);
        setAttr(sheet, "sheetId", std::to_string(count + 1));
        setAttr(sheet, "r:id", rel_id);
        session.package.parts[part] = kSheetXml;
        storePart(session, "xl/workbook.xml", workbook);
        storePart(session, "xl/_rels/workbook.xml.rels", rels);
        session.error.clear();
        addContentOverride(session, part, "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
        if (!session.error.empty()) return fail(session.error);
        return okJson("{\"ok\":true,\"format\":\"xlsx\",\"path\":" + jstr("/" + name) + "}");
    }
    if (kind == "cell") {
        CellAddr addr;
        const auto *ref = prop(props, "ref");
        std::string node = ref ? std::string(parent_path) + "/" + *ref : std::string(parent_path);
        if (ref && !parent_path.empty() && parent_path.front() != '/') node = std::string(parent_path) + "!" + *ref;
        if (!parseCellTarget(node, addr, session.error)) return fail(session.error.empty() ? "cell needs ref" : session.error);
        return setXlsxCell(session, addr, props);
    }
    if (kind == "row") {
        std::string sheet_name(parent_path);
        if (!sheet_name.empty() && sheet_name.front() == '/') sheet_name.erase(sheet_name.begin());
        pugi::xml_document workbook;
        pugi::xml_document rels;
        if (!loadPart(session, "xl/workbook.xml", workbook)) return fail(session.error);
        if (!loadPart(session, "xl/_rels/workbook.xml.rels", rels)) return fail(session.error);
        auto sheet = findSheet(workbook, workbookRels(rels), sheet_name, session.error);
        if (!session.error.empty()) return fail(session.error);
        pugi::xml_document worksheet;
        if (!loadPart(session, sheet.part, worksheet)) return fail(session.error);
        auto sheet_data = childByLocalName(worksheet.document_element(), "sheetData");
        if (!sheet_data) sheet_data = worksheet.document_element().append_child("sheetData");
        int row_number = index;
        if (row_number < 1) {
            row_number = 1;
            for (auto row : childrenNamed(sheet_data, "row")) {
                try {
                    row_number = std::max(row_number, std::stoi(attrByLocalName(row, "r")) + 1);
                } catch (...) {
                }
            }
        }
        auto row = ensureRow(sheet_data, row_number);
        for (const auto &[key, value] : props) {
            if (key.size() > 1 && key[0] == 'c' && isNumber(key.substr(1))) {
                const int col = std::stoi(key.substr(1));
                const auto ref = indexToCol(col) + std::to_string(row_number);
                writeCellValue(ensureCell(row, ref, col), value);
            }
        }
        storePart(session, sheet.part, worksheet);
        return okJson("{\"ok\":true,\"format\":\"xlsx\",\"path\":" + jstr(sheet.name + "!" + std::to_string(row_number) + ":" + std::to_string(row_number)) + "}");
    }
    return fail(std::string("unsupported xlsx type '") + std::string(type) + "'; supported: sheet, row, cell. " + kGap);
}

EditResult setXlsx(Session &session, std::string_view node, const PropMap &props, std::string_view find, std::string_view replacement) {
    if (!find.empty()) {
        pugi::xml_document workbook;
        pugi::xml_document rels;
        if (!loadPart(session, "xl/workbook.xml", workbook)) return fail(session.error);
        if (!loadPart(session, "xl/_rels/workbook.xml.rels", rels)) return fail(session.error);
        const auto rel_map = workbookRels(rels);
        int matched = 0;
        for (auto sheet_node : childrenNamed(childByLocalName(workbook.document_element(), "sheets"), "sheet")) {
            auto id = attrByLocalName(sheet_node, "id");
            auto part = rel_map.find(id);
            if (part == rel_map.end()) continue;
            pugi::xml_document worksheet;
            if (!loadPart(session, part->second, worksheet)) return fail(session.error);
            auto sheet_data = childByLocalName(worksheet.document_element(), "sheetData");
            bool dirty = false;
            for (auto row : childrenNamed(sheet_data, "row")) {
                for (auto cell : childrenNamed(row, "c")) {
                    auto display = cellDisplay(cell);
                    if (display.find(find) == std::string::npos) continue;
                    int local = 0;
                    if (!replacement.empty()) display = replaceAll(display, find, replacement, local);
                    else local = 1;
                    writeCellValue(cell, display);
                    if (prop(props, "bold") || prop(props, "italic")) {
                        pugi::xml_document styles;
                        if (!session.part("xl/styles.xml")) session.package.parts["xl/styles.xml"] = kXlsxStyles;
                        if (!loadPart(session, "xl/styles.xml", styles)) return fail(session.error);
                        const bool bold = prop(props, "bold") && truthy(*prop(props, "bold"));
                        const bool italic = prop(props, "italic") && truthy(*prop(props, "italic"));
                        setAttr(cell, "s", std::to_string(ensureFontStyle(styles, bold, italic)));
                        storePart(session, "xl/styles.xml", styles);
                    }
                    matched += local;
                    dirty = true;
                }
            }
            if (dirty) storePart(session, part->second, worksheet);
        }
        return okJson("{\"ok\":true,\"format\":\"xlsx\",\"matched\":" + std::to_string(matched) + "}");
    }
    CellAddr addr;
    if (!parseCellTarget(node, addr, session.error)) return fail(session.error);
    return setXlsxCell(session, addr, props);
}

EditResult removeXlsx(Session &session, std::string_view node) {
    auto parsed = parser::parsePath(node, "xlsx");
    if (!parsed.ok) return fail(parsed.error);
    if (parsed.ast.kind == parser::PathAstKind::Path && parsed.ast.segments.size() == 1 &&
        !sameAlias(parsed.ast.segments[0].name, {"sheet", "worksheet"})) {
        pugi::xml_document workbook;
        pugi::xml_document rels;
        if (!loadPart(session, "xl/workbook.xml", workbook)) return fail(session.error);
        if (!loadPart(session, "xl/_rels/workbook.xml.rels", rels)) return fail(session.error);
        auto sheet = findSheet(workbook, workbookRels(rels), parsed.ast.segments[0].name, session.error);
        if (!session.error.empty()) return fail(session.error);
        auto sheets = childByLocalName(workbook.document_element(), "sheets");
        sheets.remove_child(sheet.sheet);
        session.package.parts.erase(sheet.part);
        storePart(session, "xl/workbook.xml", workbook);
        return okJson("{\"ok\":true,\"format\":\"xlsx\",\"removed\":" + jstr(node) + "}");
    }
    CellAddr addr;
    if (!parseCellTarget(node, addr, session.error)) return fail(session.error);
    pugi::xml_document workbook;
    pugi::xml_document rels;
    if (!loadPart(session, "xl/workbook.xml", workbook)) return fail(session.error);
    if (!loadPart(session, "xl/_rels/workbook.xml.rels", rels)) return fail(session.error);
    auto sheet = findSheet(workbook, workbookRels(rels), addr.sheet, session.error);
    if (!session.error.empty()) return fail(session.error);
    pugi::xml_document worksheet;
    if (!loadPart(session, sheet.part, worksheet)) return fail(session.error);
    auto sheet_data = childByLocalName(worksheet.document_element(), "sheetData");
    for (auto row : childrenNamed(sheet_data, "row")) {
        for (auto cell : childrenNamed(row, "c")) {
            if (attrByLocalName(cell, "r") == addr.ref) row.remove_child(cell);
        }
    }
    storePart(session, sheet.part, worksheet);
    return okJson("{\"ok\":true,\"format\":\"xlsx\",\"removed\":" + jstr(sheet.name + "!" + addr.ref) + "}");
}

std::string xlsxPlainText(Session &session, std::string &error) {
    pugi::xml_document workbook;
    pugi::xml_document rels;
    if (!loadPart(session, "xl/workbook.xml", workbook)) return {};
    if (!loadPart(session, "xl/_rels/workbook.xml.rels", rels)) return {};
    const auto rel_map = workbookRels(rels);
    std::ostringstream out;
    bool first_sheet = true;
    for (auto sheet_node : childrenNamed(childByLocalName(workbook.document_element(), "sheets"), "sheet")) {
        auto part = rel_map.find(attrByLocalName(sheet_node, "id"));
        if (part == rel_map.end()) continue;
        pugi::xml_document worksheet;
        if (!loadPart(session, part->second, worksheet)) return {};
        if (!first_sheet) out << "\n";
        first_sheet = false;
        out << attrByLocalName(sheet_node, "name");
        for (auto row : childrenNamed(childByLocalName(worksheet.document_element(), "sheetData"), "row")) {
            out << "\n";
            bool first = true;
            for (auto cell : childrenNamed(row, "c")) {
                if (!first) out << "\t";
                first = false;
                out << cellDisplay(cell);
            }
        }
    }
    error.clear();
    return out.str();
}

// ── pptx mutation ───────────────────────────────────────────────────────────

struct SlideLoc {
    int index = 0;
    std::string id;
    std::string part;
    pugi::xml_node id_node;
};

std::vector<SlideLoc> listSlides(pugi::xml_document &presentation, pugi::xml_document &rels) {
    std::map<std::string, std::string> rel_map;
    for (auto rel : rels.document_element().children()) {
        if (!localNameEquals(rel.name(), "Relationship")) continue;
        auto target = attrByLocalName(rel, "Target");
        if (target.rfind("..", 0) == 0) continue;
        if (target.find("slides/") == std::string::npos && target.rfind("slide", 0) != 0) {
            if (target.find("slideMasters/") != std::string::npos) continue;
        }
        if (target.find("slides/") == std::string::npos) continue;
        rel_map[attrByLocalName(rel, "Id")] = "ppt/" + target;
    }
    std::vector<SlideLoc> slides;
    int index = 0;
    auto list = childByLocalName(presentation.document_element(), "sldIdLst");
    for (auto id_node : childrenNamed(list, "sldId")) {
        ++index;
        SlideLoc slide;
        slide.index = index;
        slide.id_node = id_node;
        slide.id = attrByLocalName(id_node, "id");
        std::string rel_id;
        for (auto attr : id_node.attributes()) {
            if (localNameEquals(attr.name(), "id") && std::string(attr.name()).find(':') != std::string::npos) rel_id = attr.value();
        }
        if (rel_id.empty()) rel_id = attrByLocalName(id_node, "id");
        auto part = rel_map.find(rel_id);
        if (part != rel_map.end()) slide.part = part->second;
        slides.push_back(slide);
    }
    return slides;
}

pugi::xml_node slideTree(pugi::xml_document &slide) {
    auto c_sld = childByLocalName(slide.document_element(), "cSld");
    auto tree = childByLocalName(c_sld, "spTree");
    return tree;
}

std::vector<pugi::xml_node> slideShapes(pugi::xml_node tree) {
    return childrenNamed(tree, "sp");
}

std::string shapeText(pugi::xml_node shape) { return nodeText(shape); }

int maxShapeId(pugi::xml_node tree) {
    int max_id = 1;
    for (auto node : tree.children()) {
        auto props = childByLocalName(node, "nvSpPr");
        if (!props) props = childByLocalName(node, "nvGrpSpPr");
        auto name = childByLocalName(props, "cNvPr");
        if (!name) continue;
        try {
            max_id = std::max(max_id, std::stoi(attrByLocalName(name, "id")));
        } catch (...) {
        }
    }
    return max_id;
}

void addShapeXml(pugi::xml_node tree, const PropMap &props, const std::string &fallback_name) {
    const int id = maxShapeId(tree) + 1;
    const auto *text = prop(props, "text");
    const auto *title = prop(props, "title");
    const std::string body = text ? *text : (title ? *title : "");
    const auto x = prop(props, "x") ? toEmu(*prop(props, "x")) : 457200;
    const auto y = prop(props, "y") ? toEmu(*prop(props, "y")) : 457200;
    const auto w = prop(props, "w") ? toEmu(*prop(props, "w")) : 8229600;
    const auto h = prop(props, "h") ? toEmu(*prop(props, "h")) : 685800;
    const auto name = prop(props, "name") ? *prop(props, "name") : fallback_name;
    auto shape = tree.append_child("p:sp");
    auto nv = shape.append_child("p:nvSpPr");
    auto c_nv = nv.append_child("p:cNvPr");
    setAttr(c_nv, "id", std::to_string(id));
    setAttr(c_nv, "name", name);
    nv.append_child("p:cNvSpPr");
    nv.append_child("p:nvPr");
    auto sp_pr = shape.append_child("p:spPr");
    auto xfrm = sp_pr.append_child("a:xfrm");
    auto off = xfrm.append_child("a:off");
    setAttr(off, "x", std::to_string(x));
    setAttr(off, "y", std::to_string(y));
    auto ext = xfrm.append_child("a:ext");
    setAttr(ext, "cx", std::to_string(w));
    setAttr(ext, "cy", std::to_string(h));
    auto geom = sp_pr.append_child("a:prstGeom");
    setAttr(geom, "prst", "rect");
    geom.append_child("a:avLst");
    auto tx = shape.append_child("p:txBody");
    tx.append_child("a:bodyPr");
    tx.append_child("a:lstStyle");
    auto paragraph = tx.append_child("a:p");
    auto run = paragraph.append_child("a:r");
    auto r_pr = run.append_child("a:rPr");
    setAttr(r_pr, "lang", "en-US");
    int size = 2400;
    if (const auto *sz = prop(props, "size")) {
        std::string raw = *sz;
        if (endsWith(raw, "pt")) raw.resize(raw.size() - 2);
        try {
            size = static_cast<int>(std::stod(raw) * 100.0);
        } catch (...) {
        }
    }
    setAttr(r_pr, "sz", std::to_string(size));
    if (const auto *font = prop(props, "font")) {
        auto latin = r_pr.append_child("a:latin");
        setAttr(latin, "typeface", *font);
    }
    if (const auto *color = prop(props, "color")) {
        std::string error;
        auto hex = hexColor(*color, error);
        if (error.empty()) {
            auto fill = r_pr.append_child("a:solidFill");
            setAttr(fill.append_child("a:srgbClr"), "val", hex);
        }
    }
    run.append_child("a:t").text().set(body.c_str());
}

const char *kSlideXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sld xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">
  <p:cSld><p:spTree>
    <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
    <p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>
  </p:spTree></p:cSld>
</p:sld>
)XML";

EditResult addPptx(Session &session, std::string_view parent_path, std::string_view type, const PropMap &props) {
    const auto kind = lower(std::string(type));
    pugi::xml_document presentation;
    pugi::xml_document rels;
    if (!loadPart(session, "ppt/presentation.xml", presentation)) return fail(session.error);
    if (!loadPart(session, "ppt/_rels/presentation.xml.rels", rels)) return fail(session.error);
    if (kind == "slide") {
        auto slides = listSlides(presentation, rels);
        const int number = static_cast<int>(slides.size()) + 1;
        const auto part = "ppt/slides/slide" + std::to_string(number) + ".xml";
        const auto rel_id = "rId" + std::to_string(nextRelationshipId(rels));
        auto rel = rels.document_element().append_child("Relationship");
        setAttr(rel, "Id", rel_id);
        setAttr(rel, "Type", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide");
        setAttr(rel, "Target", "slides/slide" + std::to_string(number) + ".xml");
        auto list = childByLocalName(presentation.document_element(), "sldIdLst");
        if (!list) list = presentation.document_element().prepend_child("p:sldIdLst");
        auto id_node = list.append_child("p:sldId");
        setAttr(id_node, "id", std::to_string(255 + number));
        setAttr(id_node, "r:id", rel_id);
        session.package.parts[part] = kSlideXml;
        const auto slide_rels = "ppt/slides/_rels/slide" + std::to_string(number) + ".xml.rels";
        session.package.parts[slide_rels] =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>"
            "</Relationships>";
        if (prop(props, "title") || prop(props, "text")) {
            pugi::xml_document slide;
            if (!loadXml(session.package.parts[part], slide, session.error, part.c_str())) return fail(session.error);
            PropMap title_props = props;
            if (!prop(title_props, "text") && prop(title_props, "title")) title_props["text"] = *prop(title_props, "title");
            if (!prop(title_props, "name")) title_props["name"] = "Title";
            if (!prop(title_props, "y")) title_props["y"] = "1cm";
            if (!prop(title_props, "size")) title_props["size"] = "32pt";
            addShapeXml(slideTree(slide), title_props, "Title");
            storePart(session, part, slide);
        }
        storePart(session, "ppt/presentation.xml", presentation);
        storePart(session, "ppt/_rels/presentation.xml.rels", rels);
        session.error.clear();
        addContentOverride(session, part, "application/vnd.openxmlformats-officedocument.presentationml.slide+xml");
        if (!session.error.empty()) return fail(session.error);
        return okJson("{\"ok\":true,\"format\":\"pptx\",\"path\":" + jstr("/slide[" + std::to_string(number) + "]") + "}");
    }
    if (kind == "shape") {
        auto parsed = parser::parsePath(parent_path.empty() ? "/slide[1]" : parent_path, "pptx");
        if (!parsed.ok) return fail(parsed.error);
        if (parsed.ast.segments.empty() || !sameAlias(parsed.ast.segments[0].name, {"slide", "sld"})) {
            return fail("shape parent must be /slide[n]");
        }
        auto slides = listSlides(presentation, rels);
        int index = pathIndex(parsed.ast.segments[0].predicate, static_cast<int>(slides.size()), session.error);
        if (!session.error.empty()) return fail(session.error);
        auto &slide_loc = slides[static_cast<std::size_t>(index - 1)];
        pugi::xml_document slide;
        if (!loadPart(session, slide_loc.part, slide)) return fail(session.error);
        addShapeXml(slideTree(slide), props, "TextBox " + std::to_string(maxShapeId(slideTree(slide)) + 1));
        const int shape_index = static_cast<int>(slideShapes(slideTree(slide)).size());
        storePart(session, slide_loc.part, slide);
        return okJson("{\"ok\":true,\"format\":\"pptx\",\"path\":" + jstr("/slide[" + std::to_string(index) + "]/shape[" + std::to_string(shape_index) + "]") + "}");
    }
    return fail(std::string("unsupported pptx type '") + std::string(type) + "'; supported: slide, shape. " + kGap);
}

EditResult setPptx(Session &session, std::string_view node, const PropMap &props, std::string_view find, std::string_view replacement) {
    pugi::xml_document presentation;
    pugi::xml_document rels;
    if (!loadPart(session, "ppt/presentation.xml", presentation)) return fail(session.error);
    if (!loadPart(session, "ppt/_rels/presentation.xml.rels", rels)) return fail(session.error);
    auto slides = listSlides(presentation, rels);
    auto edit_slide = [&](const SlideLoc &slide_loc, bool all_shapes) -> int {
        pugi::xml_document slide;
        if (!loadPart(session, slide_loc.part, slide)) return -1;
        int matched = 0;
        auto shapes = slideShapes(slideTree(slide));
        for (auto shape : shapes) {
            if (!all_shapes) continue;
            auto text = shapeText(shape);
            if (!find.empty() && text.find(find) == std::string::npos) continue;
            if (!find.empty()) {
                int local = 0;
                if (!replacement.empty()) text = replaceAll(text, find, replacement, local);
                else local = 1;
                matched += local;
            }
            if (const auto *next = prop(props, "text")) text = *next;
            auto tx = childByLocalName(shape, "txBody");
            if (!tx) continue;
            auto paragraph = childByLocalName(tx, "p");
            if (!paragraph) paragraph = tx.append_child("a:p");
            for (auto run : childrenNamed(paragraph, "r")) paragraph.remove_child(run);
            auto run = paragraph.append_child("a:r");
            run.append_child("a:t").text().set(text.c_str());
            if (find.empty()) matched = 1;
        }
        storePart(session, slide_loc.part, slide);
        return matched;
    };
    if (!find.empty() && (node.empty() || node == "/")) {
        int matched = 0;
        for (const auto &slide_loc : slides) {
            const int local = edit_slide(slide_loc, true);
            if (local < 0) return fail(session.error);
            matched += local;
        }
        return okJson("{\"ok\":true,\"format\":\"pptx\",\"matched\":" + std::to_string(matched) + "}");
    }
    auto parsed = parser::parsePath(node, "pptx");
    if (!parsed.ok) return fail(parsed.error);
    if (parsed.ast.segments.empty() || !sameAlias(parsed.ast.segments[0].name, {"slide", "sld"})) {
        return fail("pptx set path must be /slide[n] or /slide[n]/shape[m]");
    }
    int slide_index = pathIndex(parsed.ast.segments[0].predicate, static_cast<int>(slides.size()), session.error);
    if (!session.error.empty()) return fail(session.error);
    pugi::xml_document slide;
    const auto &slide_loc = slides[static_cast<std::size_t>(slide_index - 1)];
    if (!loadPart(session, slide_loc.part, slide)) return fail(session.error);
    auto shapes = slideShapes(slideTree(slide));
    std::vector<pugi::xml_node> targets;
    if (parsed.ast.segments.size() == 1) targets = shapes;
    else {
        if (!sameAlias(parsed.ast.segments[1].name, {"shape", "sp"})) return fail("unsupported pptx path");
        int shape_index = pathIndex(parsed.ast.segments[1].predicate, static_cast<int>(shapes.size()), session.error);
        if (!session.error.empty()) return fail(session.error);
        targets.push_back(shapes[static_cast<std::size_t>(shape_index - 1)]);
    }
    int matched = 0;
    for (auto shape : targets) {
        auto text = shapeText(shape);
        if (!find.empty()) {
            if (text.find(find) == std::string::npos) continue;
            int local = 0;
            if (!replacement.empty()) text = replaceAll(text, find, replacement, local);
            else local = 1;
            matched += local;
        } else {
            matched = 1;
        }
        if (const auto *next = prop(props, "text")) text = *next;
        auto tx = childByLocalName(shape, "txBody");
        if (!tx) tx = shape.append_child("p:txBody");
        auto paragraph = childByLocalName(tx, "p");
        if (!paragraph) paragraph = tx.append_child("a:p");
        for (auto run : childrenNamed(paragraph, "r")) paragraph.remove_child(run);
        paragraph.append_child("a:r").append_child("a:t").text().set(text.c_str());
    }
    storePart(session, slide_loc.part, slide);
    return okJson("{\"ok\":true,\"format\":\"pptx\",\"matched\":" + std::to_string(matched) + "}");
}

EditResult removePptx(Session &session, std::string_view node) {
    auto parsed = parser::parsePath(node, "pptx");
    if (!parsed.ok) return fail(parsed.error);
    pugi::xml_document presentation;
    pugi::xml_document rels;
    if (!loadPart(session, "ppt/presentation.xml", presentation)) return fail(session.error);
    if (!loadPart(session, "ppt/_rels/presentation.xml.rels", rels)) return fail(session.error);
    auto slides = listSlides(presentation, rels);
    if (parsed.ast.segments.empty() || !sameAlias(parsed.ast.segments[0].name, {"slide", "sld"})) {
        return fail("pptx remove path must be /slide[n] or /slide[n]/shape[m]");
    }
    int slide_index = pathIndex(parsed.ast.segments[0].predicate, static_cast<int>(slides.size()), session.error);
    if (!session.error.empty()) return fail(session.error);
    auto &slide_loc = slides[static_cast<std::size_t>(slide_index - 1)];
    if (parsed.ast.segments.size() == 1) {
        auto list = childByLocalName(presentation.document_element(), "sldIdLst");
        list.remove_child(slide_loc.id_node);
        session.package.parts.erase(slide_loc.part);
        storePart(session, "ppt/presentation.xml", presentation);
        return okJson("{\"ok\":true,\"format\":\"pptx\",\"removed\":" + jstr(node) + "}");
    }
    pugi::xml_document slide;
    if (!loadPart(session, slide_loc.part, slide)) return fail(session.error);
    auto shapes = slideShapes(slideTree(slide));
    int shape_index = pathIndex(parsed.ast.segments[1].predicate, static_cast<int>(shapes.size()), session.error);
    if (!session.error.empty()) return fail(session.error);
    slideTree(slide).remove_child(shapes[static_cast<std::size_t>(shape_index - 1)]);
    storePart(session, slide_loc.part, slide);
    return okJson("{\"ok\":true,\"format\":\"pptx\",\"removed\":" + jstr(node) + "}");
}

std::string pptxPlainText(Session &session, std::string &error) {
    pugi::xml_document presentation;
    pugi::xml_document rels;
    if (!loadPart(session, "ppt/presentation.xml", presentation)) return {};
    if (!loadPart(session, "ppt/_rels/presentation.xml.rels", rels)) return {};
    std::ostringstream out;
    bool first = true;
    for (const auto &slide_loc : listSlides(presentation, rels)) {
        pugi::xml_document slide;
        if (!loadPart(session, slide_loc.part, slide)) return {};
        for (auto shape : slideShapes(slideTree(slide))) {
            auto text = shapeText(shape);
            if (text.empty()) continue;
            if (!first) out << "\n";
            first = false;
            out << text;
        }
    }
    error.clear();
    return out.str();
}

DocumentNode pptxNode(Session &session, std::string_view node, int depth, std::string &error) {
    pugi::xml_document presentation;
    pugi::xml_document rels;
    DocumentNode root;
    if (!loadPart(session, "ppt/presentation.xml", presentation) || !loadPart(session, "ppt/_rels/presentation.xml.rels", rels)) {
        error = session.error;
        return root;
    }
    auto slides = listSlides(presentation, rels);
    auto shape_node = [](int slide_index, int shape_index, pugi::xml_node shape) {
        DocumentNode node;
        node.path = "/slide[" + std::to_string(slide_index) + "]/shape[" + std::to_string(shape_index) + "]";
        node.type = "shape";
        node.text = shapeText(shape);
        node.preview = node.text;
        node.format["name"] = [&] {
            auto nv = childByLocalName(shape, "nvSpPr");
            auto c_nv = childByLocalName(nv, "cNvPr");
            return attrByLocalName(c_nv, "name");
        }();
        return node;
    };
    if (node.empty() || node == "/") {
        root.path = "/";
        root.type = "presentation";
        root.child_count = static_cast<int>(slides.size());
        if (depth > 0) {
            for (const auto &slide_loc : slides) {
                DocumentNode slide;
                slide.path = "/slide[" + std::to_string(slide_loc.index) + "]";
                slide.type = "slide";
                pugi::xml_document doc;
                if (!loadPart(session, slide_loc.part, doc)) continue;
                auto shapes = slideShapes(slideTree(doc));
                slide.child_count = static_cast<int>(shapes.size());
                int shape_index = 0;
                for (auto shape : shapes) {
                    ++shape_index;
                    auto text = shapeText(shape);
                    if (slide.text.empty()) slide.text = text;
                    if (depth > 1) slide.children.push_back(shape_node(slide_loc.index, shape_index, shape));
                }
                slide.preview = slide.text;
                root.children.push_back(std::move(slide));
            }
        }
        error.clear();
        return root;
    }
    auto parsed = parser::parsePath(node, "pptx");
    if (!parsed.ok) {
        error = parsed.error;
        return root;
    }
    if (parsed.ast.segments.empty() || !sameAlias(parsed.ast.segments[0].name, {"slide", "sld"})) {
        error = "pptx path must start with /slide[n]";
        return root;
    }
    int slide_index = pathIndex(parsed.ast.segments[0].predicate, static_cast<int>(slides.size()), error);
    if (!error.empty()) return root;
    const auto &slide_loc = slides[static_cast<std::size_t>(slide_index - 1)];
    pugi::xml_document doc;
    if (!loadPart(session, slide_loc.part, doc)) {
        error = session.error;
        return root;
    }
    auto shapes = slideShapes(slideTree(doc));
    if (parsed.ast.segments.size() == 1) {
        root.path = "/slide[" + std::to_string(slide_index) + "]";
        root.type = "slide";
        root.child_count = static_cast<int>(shapes.size());
        int shape_index = 0;
        for (auto shape : shapes) {
            ++shape_index;
            if (root.text.empty()) root.text = shapeText(shape);
            if (depth > 0) root.children.push_back(shape_node(slide_index, shape_index, shape));
        }
        root.preview = root.text;
        error.clear();
        return root;
    }
    int shape_index = pathIndex(parsed.ast.segments[1].predicate, static_cast<int>(shapes.size()), error);
    if (!error.empty()) return root;
    root = shape_node(slide_index, shape_index, shapes[static_cast<std::size_t>(shape_index - 1)]);
    error.clear();
    return root;
}

DocumentNode pptxQuery(Session &session, const parser::Selector &selector, std::string &error) {
    DocumentNode root;
    root.path = "/";
    root.type = "query";
    pugi::xml_document presentation;
    pugi::xml_document rels;
    if (!loadPart(session, "ppt/presentation.xml", presentation) || !loadPart(session, "ppt/_rels/presentation.xml.rels", rels)) {
        error = session.error;
        return root;
    }
    struct Hit {
        int slide = 0;
        int shape = 0;
        std::string text;
        std::string name;
    };
    std::vector<Hit> hits;
    auto slides = listSlides(presentation, rels);
    for (const auto &slide_loc : slides) {
        pugi::xml_document doc;
        if (!loadPart(session, slide_loc.part, doc)) continue;
        int shape_index = 0;
        for (auto shape : slideShapes(slideTree(doc))) {
            ++shape_index;
            Hit hit;
            hit.slide = slide_loc.index;
            hit.shape = shape_index;
            hit.text = shapeText(shape);
            auto nv = childByLocalName(shape, "nvSpPr");
            hit.name = attrByLocalName(childByLocalName(nv, "cNvPr"), "name");
            hits.push_back(std::move(hit));
        }
    }
    auto match_step = [](const Hit &hit, const parser::SelectorStep &step) {
        if (step.subject != "shape" && step.subject != "sp" && step.subject != "slide") return false;
        for (const auto &pseudo : step.pseudos) {
            if (pseudo.name == "contains") {
                if (!pseudo.arg || hit.text.find(*pseudo.arg) == std::string::npos) return false;
            }
        }
        for (const auto &filter : step.filters) {
            if (filter.kind != parser::FilterExprKind::Predicate) return false;
            const auto &pred = filter.predicate;
            const auto value = (pred.key == "name") ? hit.name : hit.text;
            if (pred.op == "=" && value != pred.value) return false;
            if (pred.op == "~=" && value.find(pred.value) == std::string::npos) return false;
            if (pred.op == "!=" && value == pred.value) return false;
        }
        return step.subject == "shape" || step.subject == "sp" || step.subject == "slide";
    };
    for (const auto &alternative : selector.alternatives) {
        if (alternative.steps.empty()) continue;
        std::vector<Hit> matched;
        for (const auto &hit : hits) {
            if (match_step(hit, alternative.steps.back())) matched.push_back(hit);
        }
        const auto &last = alternative.steps.back();
        if (last.nth && *last.nth > 0 && static_cast<std::size_t>(*last.nth) <= matched.size()) {
            matched = {matched[static_cast<std::size_t>(*last.nth - 1)]};
        } else if (last.nth) {
            matched.clear();
        }
        for (const auto &hit : matched) {
            DocumentNode node;
            if (last.subject == "slide") {
                node.path = "/slide[" + std::to_string(hit.slide) + "]";
                node.type = "slide";
            } else {
                node.path = "/slide[" + std::to_string(hit.slide) + "]/shape[" + std::to_string(hit.shape) + "]";
                node.type = "shape";
                node.format["name"] = hit.name;
            }
            node.text = hit.text;
            node.preview = hit.text;
            root.children.push_back(std::move(node));
        }
    }
    root.child_count = static_cast<int>(root.children.size());
    error.clear();
    return root;
}

// ── batch JSON ──────────────────────────────────────────────────────────────

struct JsonVal {
    enum class Kind { Null, String, Number, Bool, Object, Array } kind = Kind::Null;
    std::string text;
    bool flag = false;
    std::map<std::string, JsonVal> object;
    std::vector<JsonVal> array;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool parse(JsonVal &out, std::string &error) {
        skip();
        if (!value(out)) {
            error = "invalid batch JSON at " + std::to_string(index_);
            return false;
        }
        skip();
        if (index_ != input_.size()) {
            error = "trailing data in batch JSON";
            return false;
        }
        return true;
    }

private:
    std::string_view input_;
    std::size_t index_ = 0;

    void skip() {
        while (index_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[index_]))) ++index_;
    }

    bool consume(char expected) {
        skip();
        if (index_ < input_.size() && input_[index_] == expected) {
            ++index_;
            return true;
        }
        return false;
    }

    bool value(JsonVal &out) {
        skip();
        if (index_ >= input_.size()) return false;
        const char c = input_[index_];
        if (c == '"') return string(out.text) && (out.kind = JsonVal::Kind::String, true);
        if (c == '{') return object(out);
        if (c == '[') return array(out);
        if (input_.compare(index_, 4, "true") == 0) {
            index_ += 4;
            out.kind = JsonVal::Kind::Bool;
            out.flag = true;
            return true;
        }
        if (input_.compare(index_, 5, "false") == 0) {
            index_ += 5;
            out.kind = JsonVal::Kind::Bool;
            out.flag = false;
            return true;
        }
        if (input_.compare(index_, 4, "null") == 0) {
            index_ += 4;
            out.kind = JsonVal::Kind::Null;
            return true;
        }
        return number(out);
    }

    bool string(std::string &out) {
        if (!consume('"')) return false;
        out.clear();
        while (index_ < input_.size()) {
            char c = input_[index_++];
            if (c == '"') return true;
            if (c == '\\' && index_ < input_.size()) {
                char esc = input_[index_++];
                if (esc == 'n') out.push_back('\n');
                else if (esc == 't') out.push_back('\t');
                else if (esc == 'r') out.push_back('\r');
                else out.push_back(esc);
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool number(JsonVal &out) {
        const auto start = index_;
        if (input_[index_] == '-' ) ++index_;
        while (index_ < input_.size() && (std::isdigit(static_cast<unsigned char>(input_[index_])) || input_[index_] == '.')) ++index_;
        if (index_ == start) return false;
        out.kind = JsonVal::Kind::Number;
        out.text = std::string(input_.substr(start, index_ - start));
        return true;
    }

    bool object(JsonVal &out) {
        if (!consume('{')) return false;
        out.kind = JsonVal::Kind::Object;
        skip();
        if (consume('}')) return true;
        while (index_ < input_.size()) {
            std::string key;
            if (!string(key) || !consume(':')) return false;
            JsonVal child;
            if (!value(child)) return false;
            out.object.emplace(std::move(key), std::move(child));
            skip();
            if (consume('}')) return true;
            if (!consume(',')) return false;
        }
        return false;
    }

    bool array(JsonVal &out) {
        if (!consume('[')) return false;
        out.kind = JsonVal::Kind::Array;
        skip();
        if (consume(']')) return true;
        while (index_ < input_.size()) {
            JsonVal child;
            if (!value(child)) return false;
            out.array.push_back(std::move(child));
            skip();
            if (consume(']')) return true;
            if (!consume(',')) return false;
        }
        return false;
    }
};

std::string jsonText(const JsonVal &value) {
    if (value.kind == JsonVal::Kind::String || value.kind == JsonVal::Kind::Number) return value.text;
    if (value.kind == JsonVal::Kind::Bool) return value.flag ? "true" : "false";
    return {};
}

} // namespace

std::string documentFormat(const std::filesystem::path &file) {
    const auto ext = lower(file.extension().string());
    if (ext == ".docx") return "docx";
    if (ext == ".xlsx") return "xlsx";
    if (ext == ".pptx") return "pptx";
    return {};
}

EditResult createDocument(const std::filesystem::path &file) {
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) return fail("file already exists: " + file.string());
    const auto format = documentFormat(file);
    OoxmlParts parts;
    if (format == "docx") parts = blankDocx();
    else if (format == "xlsx") parts = blankXlsx();
    else if (format == "pptx") parts = blankPptx();
    else return fail("create needs a .docx, .xlsx, or .pptx path");
    std::string error;
    if (!writeOoxmlParts(file, parts, error)) return fail(error);
    return okJson("{\"ok\":true,\"format\":" + jstr(format) + ",\"path\":" + jstr(file.string()) + "}");
}

EditResult viewDocument(const std::filesystem::path &file, std::string_view mode) {
    Session session;
    session.file = file;
    if (!session.load()) return fail(session.error);
    const auto which = lower(std::string(mode));
    if (which == "text") {
        std::string text;
        if (session.format == "docx") {
            pugi::xml_document doc;
            if (!loadPart(session, "word/document.xml", doc)) return fail(session.error);
            auto body = docxBody(doc, session.error);
            if (!body) return fail(session.error);
            text = docxPlainText(body);
        } else if (session.format == "xlsx") {
            text = xlsxPlainText(session, session.error);
            if (!session.error.empty()) return fail(session.error);
        } else {
            text = pptxPlainText(session, session.error);
            if (!session.error.empty()) return fail(session.error);
        }
        return plainText(text);
    }
    if (which == "outline" || which == "stats") {
        int paragraphs = 0;
        int sheets = 0;
        int slides = 0;
        std::string text;
        if (session.format == "docx") {
            pugi::xml_document doc;
            if (!loadPart(session, "word/document.xml", doc)) return fail(session.error);
            auto body = docxBody(doc, session.error);
            if (!body) return fail(session.error);
            paragraphs = static_cast<int>(childrenNamed(body, "p").size());
            text = docxPlainText(body);
            if (which == "outline") {
                std::ostringstream out;
                int index = 0;
                for (auto paragraph : childrenNamed(body, "p")) {
                    ++index;
                    auto p_pr = childByLocalName(paragraph, "pPr");
                    auto style = attrByLocalName(childByLocalName(p_pr, "pStyle"), "val");
                    out << "p[" << index << "]";
                    if (!style.empty()) out << " " << style;
                    out << " " << nodeText(paragraph) << "\n";
                }
                return plainText(out.str());
            }
        } else if (session.format == "xlsx") {
            pugi::xml_document workbook;
            if (!loadPart(session, "xl/workbook.xml", workbook)) return fail(session.error);
            sheets = static_cast<int>(childrenNamed(childByLocalName(workbook.document_element(), "sheets"), "sheet").size());
            text = xlsxPlainText(session, session.error);
            if (which == "outline") {
                std::ostringstream out;
                for (auto sheet : childrenNamed(childByLocalName(workbook.document_element(), "sheets"), "sheet")) {
                    out << attrByLocalName(sheet, "name") << "\n";
                }
                return plainText(out.str());
            }
        } else {
            pugi::xml_document presentation;
            pugi::xml_document rels;
            if (!loadPart(session, "ppt/presentation.xml", presentation)) return fail(session.error);
            if (!loadPart(session, "ppt/_rels/presentation.xml.rels", rels)) return fail(session.error);
            auto slide_list = listSlides(presentation, rels);
            slides = static_cast<int>(slide_list.size());
            text = pptxPlainText(session, session.error);
            if (which == "outline") {
                std::ostringstream out;
                for (const auto &slide_loc : slide_list) out << "slide[" << slide_loc.index << "]\n";
                return plainText(out.str());
            }
        }
        return okJson("{\"ok\":true,\"format\":" + jstr(session.format) +
                      ",\"paragraphs\":" + std::to_string(paragraphs) +
                      ",\"words\":" + std::to_string(wordCount(text)) +
                      ",\"sheets\":" + std::to_string(sheets) +
                      ",\"slides\":" + std::to_string(slides) + "}");
    }
    return fail("view mode must be text, outline, or stats");
}

EditResult addElement(const std::filesystem::path &file, std::string_view parent, std::string_view type, const PropMap &props,
                      std::string_view after, std::string_view before, int index) {
    Session session;
    session.file = file;
    if (!session.load()) return fail(session.error);
    EditResult result = fail("unsupported format");
    if (session.format == "docx") result = addDocx(session, parent, type, props, after, before, index);
    else if (session.format == "xlsx") result = addXlsx(session, parent, type, props, index);
    else result = addPptx(session, parent, type, props);
    if (!result.ok) return result;
    if (!session.save()) return fail(session.error);
    return result;
}

EditResult setElement(const std::filesystem::path &file, std::string_view node, const PropMap &props, std::string_view find,
                      std::string_view replace_with) {
    Session session;
    session.file = file;
    if (!session.load()) return fail(session.error);
    EditResult result = fail("unsupported format");
    if (session.format == "docx") result = setDocx(session, node, props, find, replace_with);
    else if (session.format == "xlsx") result = setXlsx(session, node, props, find, replace_with);
    else result = setPptx(session, node, props, find, replace_with);
    if (!result.ok) return result;
    if (!session.save()) return fail(session.error);
    return result;
}

EditResult removeElement(const std::filesystem::path &file, std::string_view node) {
    Session session;
    session.file = file;
    if (!session.load()) return fail(session.error);
    EditResult result = fail("unsupported format");
    if (session.format == "docx") result = removeDocx(session, node);
    else if (session.format == "xlsx") result = removeXlsx(session, node);
    else result = removePptx(session, node);
    if (!result.ok) return result;
    if (!session.save()) return fail(session.error);
    return result;
}

EditResult moveElement(const std::filesystem::path &file, std::string_view node, std::string_view, std::string_view after,
                       std::string_view before, int index) {
    Session session;
    session.file = file;
    if (!session.load()) return fail(session.error);
    if (session.format != "docx") return fail("move currently supports docx paragraphs and tables");
    auto result = moveDocx(session, node, after, before, index);
    if (!result.ok) return result;
    if (!session.save()) return fail(session.error);
    return result;
}

EditResult swapElements(const std::filesystem::path &file, std::string_view left, std::string_view right) {
    Session session;
    session.file = file;
    if (!session.load()) return fail(session.error);
    if (session.format != "docx") return fail("swap currently supports docx paragraphs and tables");
    pugi::xml_document doc;
    if (!loadPart(session, "word/document.xml", doc)) return fail(session.error);
    auto body = docxBody(doc, session.error);
    if (!body) return fail(session.error);
    auto left_path = parser::parsePath(left, "docx");
    auto right_path = parser::parsePath(right, "docx");
    if (!left_path.ok) return fail(left_path.error);
    if (!right_path.ok) return fail(right_path.error);
    auto a = locateDocx(body, left_path.ast, session.error);
    if (!session.error.empty()) return fail(session.error);
    auto b = locateDocx(body, right_path.ast, session.error);
    if (!session.error.empty()) return fail(session.error);
    if (a.kind != b.kind || (a.kind != "paragraph" && a.kind != "table")) return fail("swap needs two paragraphs or two tables");
    auto parent = a.parent.parent();
    auto marker = parent.insert_child_before(a.kind == "paragraph" ? "w:p" : "w:tbl", a.parent);
    parent.insert_move_before(a.parent, b.parent);
    parent.insert_move_before(b.parent, marker);
    parent.remove_child(marker);
    storePart(session, "word/document.xml", doc);
    if (!session.save()) return fail(session.error);
    return okJson("{\"ok\":true,\"format\":\"docx\",\"swapped\":[" + jstr(left) + "," + jstr(right) + "]}");
}

EditResult validateDocument(const std::filesystem::path &file) {
    Session session;
    session.file = file;
    if (!session.load()) return fail(session.error);
    const char *main_part = session.format == "docx" ? "word/document.xml" : session.format == "xlsx" ? "xl/workbook.xml" : "ppt/presentation.xml";
    pugi::xml_document doc;
    if (!loadPart(session, main_part, doc)) return fail(session.error);
    if (!session.part("[Content_Types].xml")) return fail("missing [Content_Types].xml");
    return okJson("{\"ok\":true,\"format\":" + jstr(session.format) + ",\"parts\":" + std::to_string(session.package.parts.size()) + "}");
}

EditResult runBatch(const std::filesystem::path &file, const std::vector<BatchOp> &ops, bool stop_on_error) {
    std::ostringstream out;
    out << "{\"results\":[";
    bool all_ok = true;
    Session session;
    session.file = file;
    if (!session.load()) return fail(session.error);
    bool dirty = false;
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const auto command = lower(ops[i].command);
        EditResult result = fail("unsupported batch command");
        if (command == "add") {
            if (session.format == "docx") result = addDocx(session, ops[i].parent.empty() ? "/" : ops[i].parent, ops[i].type, ops[i].props, ops[i].after, ops[i].before, ops[i].has_index ? ops[i].index : -1);
            else if (session.format == "xlsx") result = addXlsx(session, ops[i].parent.empty() ? "/" : ops[i].parent, ops[i].type, ops[i].props, ops[i].has_index ? ops[i].index : -1);
            else result = addPptx(session, ops[i].parent.empty() ? "/" : ops[i].parent, ops[i].type, ops[i].props);
            if (result.ok) dirty = true;
        } else if (command == "set") {
            if (session.format == "docx") result = setDocx(session, ops[i].path.empty() ? "/" : ops[i].path, ops[i].props, ops[i].find, ops[i].replace);
            else if (session.format == "xlsx") result = setXlsx(session, ops[i].path, ops[i].props, ops[i].find, ops[i].replace);
            else result = setPptx(session, ops[i].path.empty() ? "/" : ops[i].path, ops[i].props, ops[i].find, ops[i].replace);
            if (result.ok) dirty = true;
        } else if (command == "remove") {
            if (session.format == "docx") result = removeDocx(session, ops[i].path);
            else if (session.format == "xlsx") result = removeXlsx(session, ops[i].path);
            else result = removePptx(session, ops[i].path);
            if (result.ok) dirty = true;
        } else if (command == "view") {
            if (dirty && !session.save()) return fail(session.error);
            dirty = false;
            result = viewDocument(file, ops[i].mode.empty() ? "text" : ops[i].mode);
        } else if (command == "get") {
            if (dirty && !session.save()) return fail(session.error);
            dirty = false;
            result = documentGet(file, ops[i].path.empty() ? "/" : ops[i].path, 1);
        } else if (command == "query") {
            if (dirty && !session.save()) return fail(session.error);
            dirty = false;
            result = documentQuery(file, ops[i].path);
        } else {
            result = fail("batch command is not supported: " + ops[i].command + ". " + kGap);
        }
        if (i) out << ",";
        out << (result.json.empty() ? std::string("{\"ok\":false}") : result.json);
        if (!result.ok) {
            all_ok = false;
            if (stop_on_error) break;
        }
    }
    if (dirty && !session.save()) return fail(session.error);
    out << "],\"ok\":" << (all_ok ? "true" : "false") << "}";
    EditResult result;
    result.ok = all_ok;
    result.exit_code = all_ok ? 0 : 1;
    result.json = out.str();
    if (!all_ok) result.error = "one or more batch commands failed";
    return result;
}

EditResult runBatchJson(const std::filesystem::path &file, std::string_view json, bool stop_on_error) {
    JsonParser parser(json);
    JsonVal root;
    std::string error;
    if (!parser.parse(root, error)) return fail(error);
    if (root.kind != JsonVal::Kind::Array) return fail("batch JSON must be an array");
    std::vector<BatchOp> ops;
    for (const auto &item : root.array) {
        if (item.kind != JsonVal::Kind::Object) return fail("batch item must be an object");
        BatchOp op;
        auto take = [&](const char *key) -> const JsonVal * {
            auto found = item.object.find(key);
            if (found == item.object.end()) return nullptr;
            return &found->second;
        };
        if (const auto *command = take("command")) op.command = jsonText(*command);
        if (op.command.empty()) {
            if (const auto *command = take("op")) op.command = jsonText(*command);
        }
        if (const auto *path = take("path")) op.path = jsonText(*path);
        if (const auto *parent = take("parent")) op.parent = jsonText(*parent);
        if (const auto *type = take("type")) op.type = jsonText(*type);
        if (const auto *mode = take("mode")) op.mode = jsonText(*mode);
        if (const auto *after = take("after")) op.after = jsonText(*after);
        if (const auto *before = take("before")) op.before = jsonText(*before);
        if (const auto *find = take("find")) op.find = jsonText(*find);
        if (const auto *replace = take("replace")) op.replace = jsonText(*replace);
        if (const auto *index = take("index")) {
            try {
                op.index = std::stoi(jsonText(*index));
                op.has_index = true;
            } catch (...) {
                return fail("batch index must be a number");
            }
        }
        if (const auto *props = take("props")) {
            if (props->kind != JsonVal::Kind::Object) return fail("batch props must be an object");
            for (const auto &[key, value] : props->object) op.props[key] = jsonText(value);
        }
        if (op.find.empty()) {
            if (const auto *find = prop(op.props, "find")) op.find = *find;
        }
        if (op.replace.empty()) {
            if (const auto *replace = prop(op.props, "replace")) op.replace = *replace;
        }
        if (op.command.empty()) return fail("batch item needs command or op");
        ops.push_back(std::move(op));
    }
    return runBatch(file, ops, stop_on_error);
}

EditResult documentGet(const std::filesystem::path &file, std::string_view node, int depth) {
    const auto format = documentFormat(file);
    if (format.empty()) return fail("unsupported document extension");
    if (format == "pptx") {
        Session session;
        session.file = file;
        if (!session.load()) return fail(session.error);
        std::string error;
        auto doc = pptxNode(session, node, depth, error);
        if (!error.empty()) return fail(error);
        return okJson(toJson(doc));
    }
    try {
        XlsxPackage package(file);
        auto parsed = parser::parsePath(node.empty() ? "/" : node, format);
        if (!parsed.ok) return fail(parsed.error);
        ResolveResult result = format == "docx" ? resolveWordGet(package, parsed.ast, depth) : resolveXlsxGet(package, parsed.ast, depth);
        if (!result.ok) return fail(result.error);
        return okJson(toJson(result.node));
    } catch (const std::exception &ex) {
        return fail(ex.what());
    }
}

EditResult documentQuery(const std::filesystem::path &file, std::string_view selector_text) {
    const auto format = documentFormat(file);
    if (format.empty()) return fail("unsupported document extension");
    auto parsed = parser::parseSelector(selector_text);
    if (!parsed.ok) return fail(parsed.error);
    if (format == "pptx") {
        Session session;
        session.file = file;
        if (!session.load()) return fail(session.error);
        std::string error;
        auto doc = pptxQuery(session, parsed.ast, error);
        if (!error.empty()) return fail(error);
        return okJson(toJson(doc));
    }
    try {
        XlsxPackage package(file);
        ResolveResult result = format == "docx" ? resolveWordQuery(package, parsed.ast) : resolveXlsxQuery(package, parsed.ast);
        if (!result.ok) return fail(result.error);
        return okJson(toJson(result.node));
    } catch (const std::exception &ex) {
        return fail(ex.what());
    }
}

} // namespace office
