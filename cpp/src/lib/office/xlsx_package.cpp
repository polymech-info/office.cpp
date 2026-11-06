#include "xlsx_package.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

namespace office {
namespace {

std::string normalizePartName(std::string_view part_name) {
    std::string name(part_name.begin(), part_name.end());
    std::replace(name.begin(), name.end(), '\\', '/');
    while (!name.empty() && name.front() == '/') {
        name.erase(name.begin());
    }
    return name;
}

class ZipReader {
public:
    explicit ZipReader(const std::filesystem::path &path) {
        reader_ = mz_zip_reader_create();
        if (reader_ == nullptr) {
            throw std::runtime_error("failed to create zip reader");
        }
        const auto path_string = path.string();
        const auto err = mz_zip_reader_open_file(reader_, path_string.c_str());
        if (err != MZ_OK) {
            mz_zip_reader_delete(&reader_);
            throw std::runtime_error("failed to open zip package: " + path_string);
        }
    }

    ~ZipReader() {
        if (reader_ != nullptr) {
            mz_zip_reader_close(reader_);
            mz_zip_reader_delete(&reader_);
        }
    }

    ZipReader(const ZipReader &) = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    void *get() const { return reader_; }

private:
    void *reader_ = nullptr;
};

} // namespace

XlsxPackage::XlsxPackage(std::filesystem::path path) : path_(std::move(path)) {}

const std::filesystem::path &XlsxPackage::path() const {
    return path_;
}

bool XlsxPackage::hasPart(std::string_view part_name) const {
    auto name = normalizePartName(part_name);
    ZipReader reader(path_);
    return mz_zip_reader_locate_entry(reader.get(), name.c_str(), 0) == MZ_OK;
}

std::optional<std::string> XlsxPackage::readText(std::string_view part_name) const {
    auto name = normalizePartName(part_name);
    ZipReader reader(path_);
    if (mz_zip_reader_locate_entry(reader.get(), name.c_str(), 0) != MZ_OK) {
        return std::nullopt;
    }

    mz_zip_file *info = nullptr;
    if (mz_zip_reader_entry_get_info(reader.get(), &info) != MZ_OK || info == nullptr) {
        return std::nullopt;
    }

    if (mz_zip_reader_entry_open(reader.get()) != MZ_OK) {
        return std::nullopt;
    }

    std::string text;
    const auto expected_size = static_cast<std::size_t>(info->uncompressed_size);
    if (expected_size > 0) {
        text.reserve(expected_size);
    }

    std::vector<char> buffer(64 * 1024);
    int32_t read = 0;
    while ((read = mz_zip_reader_entry_read(reader.get(), buffer.data(), static_cast<int32_t>(buffer.size()))) > 0) {
        text.append(buffer.data(), static_cast<std::size_t>(read));
    }

    mz_zip_reader_entry_close(reader.get());
    if (read < 0) {
        return std::nullopt;
    }
    return text;
}

std::vector<std::string> XlsxPackage::listParts() const {
    ZipReader reader(path_);
    std::vector<std::string> parts;
    int32_t err = mz_zip_reader_goto_first_entry(reader.get());
    while (err == MZ_OK) {
        mz_zip_file *info = nullptr;
        if (mz_zip_reader_entry_get_info(reader.get(), &info) == MZ_OK && info != nullptr && info->filename != nullptr) {
            parts.emplace_back(info->filename);
        }
        err = mz_zip_reader_goto_next_entry(reader.get());
    }
    return parts;
}

} // namespace office
