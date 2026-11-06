#pragma once

#include <map>
#include <string>
#include <vector>

namespace office {

struct DocumentNode {
    std::string path;
    std::string type;
    std::string text;
    std::string preview;
    std::string style;
    int child_count = 0;
    std::map<std::string, std::string> format;
    std::vector<DocumentNode> children;
};

std::string toJson(const DocumentNode &node);

} // namespace office
