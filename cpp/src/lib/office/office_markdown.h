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

struct OoxmlParts;

// Markdown for docx, docm, xlsx, xlsm, pptx, pptm, odt, ods, and odp.
// Spreadsheets also fill cells. embed_images keeps picture bytes in the markdown.
// The reader passes false so speech hears the words and an image's alt text.
MarkdownDocument readMarkdown(const std::filesystem::path &file, bool embed_images = true);
MarkdownDocument markdownFromPackage(const OoxmlParts &package, const std::string &format, bool embed_images = true);

// True when the extension is one this library reads. error is set when the package cannot be read.
bool readOfficeText(const std::filesystem::path &file, std::string &text, std::string &error);

} // namespace office
