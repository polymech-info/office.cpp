#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace office {

using PropMap = std::map<std::string, std::string>;

struct EditResult {
    bool ok = false;
    int exit_code = 1;
    std::string error;
    std::string json;
    std::string text;
    bool plain = false;
};

struct BatchOp {
    std::string command;
    std::string path;
    std::string parent;
    std::string type;
    std::string mode;
    std::string after;
    std::string before;
    std::string find;
    std::string replace;
    int index = -1;
    bool has_index = false;
    PropMap props;
};

// Blank .docx, .xlsx, or .pptx from the file extension. Fails if the file exists.
EditResult createDocument(const std::filesystem::path &file);

EditResult viewDocument(const std::filesystem::path &file, std::string_view mode);

EditResult addElement(const std::filesystem::path &file,
                      std::string_view parent,
                      std::string_view type,
                      const PropMap &props,
                      std::string_view after = {},
                      std::string_view before = {},
                      int index = -1);

EditResult setElement(const std::filesystem::path &file,
                      std::string_view node,
                      const PropMap &props,
                      std::string_view find = {},
                      std::string_view replace_with = {});

EditResult removeElement(const std::filesystem::path &file, std::string_view node);

EditResult moveElement(const std::filesystem::path &file,
                       std::string_view node,
                       std::string_view to,
                       std::string_view after,
                       std::string_view before,
                       int index);

EditResult swapElements(const std::filesystem::path &file, std::string_view left, std::string_view right);

EditResult validateDocument(const std::filesystem::path &file);

EditResult runBatch(const std::filesystem::path &file, const std::vector<BatchOp> &ops, bool stop_on_error);

// JSON array of batch ops. `command` or `op`, plus path/parent/type/props.
EditResult runBatchJson(const std::filesystem::path &file, std::string_view json, bool stop_on_error);

// Resolver JSON (DocumentNode), including a small pptx get/query.
EditResult documentGet(const std::filesystem::path &file, std::string_view node, int depth);
EditResult documentQuery(const std::filesystem::path &file, std::string_view selector);

std::string documentFormat(const std::filesystem::path &file);

// An agent run holds each document it touches. Tools share that copy, and the
// file is written and unlocked when the run ends. Outside a run every call
// saves before it returns.
void beginAgentDocuments();
void endAgentDocuments();

} // namespace office
