#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace office {

class XlsxPackage {
public:
    explicit XlsxPackage(std::filesystem::path path);

    const std::filesystem::path &path() const;
    bool hasPart(std::string_view part_name) const;
    std::optional<std::string> readText(std::string_view part_name) const;
    std::vector<std::string> listParts() const;

private:
    std::filesystem::path path_;
};

} // namespace office
