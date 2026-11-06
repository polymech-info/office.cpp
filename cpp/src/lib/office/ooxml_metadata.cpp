#include "ooxml_metadata.h"

#include <map>
#include <sstream>
#include <string>
#include <string_view>

#include <pugixml.hpp>

namespace office {
namespace {

bool localNameEquals(const char *name, std::string_view expected) {
    if (name == nullptr) return false;
    std::string_view full(name);
    auto colon = full.find(':');
    auto local = colon == std::string_view::npos ? full : full.substr(colon + 1);
    return local == expected;
}

ResolveResult fail(std::string message) {
    return {.error = std::move(message)};
}

std::string collectText(pugi::xml_node node) {
    std::ostringstream out;
    for (auto child : node.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            out << child.value();
        } else {
            out << collectText(child);
        }
    }
    return out.str();
}

std::string partForTarget(const std::string &target) {
    if (target.empty() || target == "meta") return {};
    if (target == "core") return "docProps/core.xml";
    if (target == "app") return "docProps/app.xml";
    if (target == "custom") return "docProps/custom.xml";
    return {};
}

DocumentNode makeMetadataNode(const std::string &target, std::map<std::string, std::string> values) {
    DocumentNode node;
    node.path = target.empty() || target == "meta" ? "/meta" : "/meta/" + target;
    node.type = "metadata";
    node.format = std::move(values);
    node.child_count = static_cast<int>(node.format.size());
    auto title = node.format.find("title");
    if (title != node.format.end()) {
        node.text = title->second;
        node.preview = title->second;
    } else if (!node.format.empty()) {
        node.text = node.format.begin()->second;
        node.preview = node.format.begin()->second;
    }
    return node;
}

ResolveResult readMetadataPart(const XlsxPackage &package, const std::string &target) {
    auto part = partForTarget(target);
    if (part.empty()) return fail("unsupported metadata target: /meta/" + target);
    auto xml = package.readText(part);
    if (!xml.has_value()) return fail("missing metadata part: " + part);

    pugi::xml_document doc;
    auto parsed = doc.load_string(xml->c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
    if (!parsed) return fail("failed to parse " + part + ": " + parsed.description());

    std::map<std::string, std::string> values;
    for (auto child : doc.document_element().children()) {
        if (!child.name() || std::string_view(child.name()).empty()) continue;
        auto name = std::string(child.name());
        auto colon = name.find(':');
        if (colon != std::string::npos) name = name.substr(colon + 1);

        if (target == "custom" && localNameEquals(child.name(), "property")) {
            auto key = std::string(child.attribute("name").value());
            if (!key.empty()) values[key] = collectText(child);
        } else {
            values[name] = collectText(child);
        }
    }
    return {.ok = true, .node = makeMetadataNode(target, std::move(values))};
}

} // namespace

ResolveResult resolveOoxmlMetadataGet(const XlsxPackage &package, const std::string &target) {
    if (target.empty() || target == "meta") {
        DocumentNode node;
        node.path = "/meta";
        node.type = "metadata";
        for (const auto &child : {"core", "app", "custom"}) {
            auto result = readMetadataPart(package, child);
            if (result.ok) node.children.push_back(std::move(result.node));
        }
        node.child_count = static_cast<int>(node.children.size());
        node.format["count"] = std::to_string(node.children.size());
        return {.ok = true, .node = std::move(node)};
    }
    return readMetadataPart(package, target);
}

} // namespace office
