#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace office {

// In-memory OOXML package. Part names use zip paths (`word/document.xml`).
// Values are raw bytes, held in std::string so binary parts round-trip.
struct OoxmlParts {
    std::map<std::string, std::string> parts;
};

OoxmlParts readOoxmlParts(const std::filesystem::path &path, std::string &error);
bool writeOoxmlParts(const std::filesystem::path &path, const OoxmlParts &package, std::string &error);

} // namespace office
