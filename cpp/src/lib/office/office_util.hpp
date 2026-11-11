#pragma once

#include <string>
#include <string_view>

namespace office {

inline std::string lower(std::string value) {
    for (char &c : value) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

inline std::string upper(std::string value) {
    for (char &c : value) {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    return value;
}

inline bool endsWithCi(std::string_view value, std::string_view suffix) {
    if (suffix.size() > value.size()) return false;
    const auto tail = value.substr(value.size() - suffix.size());
    return lower(std::string(tail)) == lower(std::string(suffix));
}

inline std::string jsonEscape(std::string_view input) {
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

inline std::string jstr(std::string_view value) { return "\"" + jsonEscape(value) + "\""; }

} // namespace office
