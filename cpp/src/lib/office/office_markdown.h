#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace office {

struct MarkdownCell {
    std::string sheet;
    std::string ref;
    std::string value;
};

struct MarkdownDocument {
    bool ok = false;
    std::string error;
    std::string format;
    std::string markdown;
    std::vector<MarkdownCell> cells;
};

// Markdown for docx, xlsx, pptx, odt, ods, and odp. Spreadsheets also fill cells.
MarkdownDocument readMarkdown(const std::filesystem::path &file);

} // namespace office
