#include "document_node.h"

#include "office_util.hpp"

#include <sstream>
#include <string_view>

namespace office {

std::string toJson(const DocumentNode &node) {
    std::ostringstream out;
    out << "{\"path\":" << jstr(node.path)
        << ",\"type\":" << jstr(node.type)
        << ",\"text\":" << jstr(node.text)
        << ",\"preview\":" << jstr(node.preview)
        << ",\"style\":" << jstr(node.style)
        << ",\"childCount\":" << node.child_count
        << ",\"format\":{";

    bool first = true;
    for (const auto &[key, value] : node.format) {
        if (!first) out << ",";
        first = false;
        out << jstr(key) << ":" << jstr(value);
    }
    out << "},\"children\":[";
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (i) out << ",";
        out << toJson(node.children[i]);
    }
    out << "]}";
    return out.str();
}

} // namespace office
