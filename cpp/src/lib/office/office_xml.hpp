#pragma once

#include "office_util.hpp"

#include <string>
#include <string_view>

#include <pugixml.hpp>

namespace office {

inline bool localNameEquals(const char *name, std::string_view expected) {
    if (name == nullptr) return false;
    std::string_view full(name);
    const auto colon = full.find(':');
    const auto local = colon == std::string_view::npos ? full : full.substr(colon + 1);
    return local == expected;
}

inline pugi::xml_node childByLocalName(pugi::xml_node parent, std::string_view name) {
    for (auto child : parent.children()) {
        if (localNameEquals(child.name(), name)) return child;
    }
    return {};
}

inline std::string attrByLocalName(pugi::xml_node node, std::string_view name) {
    for (auto attr : node.attributes()) {
        if (localNameEquals(attr.name(), name)) return attr.value();
    }
    return {};
}

} // namespace office
