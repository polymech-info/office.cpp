#pragma once

#include <string>
#include <vector>

namespace json {

/// Parse a JSON string and return a pretty-printed version.
std::string prettify(const std::string &json_str);

/// Extract a string value by key from a JSON object (top-level only).
std::string get_string(const std::string &json_str, const std::string &key);

/// Extract an int value by key from a JSON object (top-level only).
int get_int(const std::string &json_str, const std::string &key);

/// Check if a JSON string is valid.
bool is_valid(const std::string &json_str);

/// Get all top-level keys from a JSON object.
std::vector<std::string> keys(const std::string &json_str);

} // namespace json
