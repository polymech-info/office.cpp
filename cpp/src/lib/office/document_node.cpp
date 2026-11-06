#include "document_node.h"

#include <sstream>
#include <string_view>

namespace office {
namespace {

std::string jsonEscape(std::string_view input) {
    std::ostringstream out;
    for (unsigned char c : input) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    const char *hex = "0123456789ABCDEF";
                    out << "\\u00" << hex[(c >> 4) & 0xF] << hex[c & 0xF];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::string jstr(std::string_view s) {
    return "\"" + jsonEscape(s) + "\"";
}

} // namespace

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
