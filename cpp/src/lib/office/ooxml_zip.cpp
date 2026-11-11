#include "ooxml_zip.h"

#include <algorithm>
#include <ctime>
#include <utility>
#include <vector>

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

namespace office {
namespace {

std::string normalizePartName(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    return name;
}

class ZipReader {
public:
    explicit ZipReader(const std::filesystem::path &path) {
        reader_ = mz_zip_reader_create();
        if (reader_ == nullptr) {
            error_ = "failed to create zip reader";
            return;
        }
        const auto path_string = path.string();
        if (mz_zip_reader_open_file(reader_, path_string.c_str()) != MZ_OK) {
            mz_zip_reader_delete(&reader_);
            error_ = "failed to open zip package: " + path_string;
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
    const std::string &error() const { return error_; }

private:
    void *reader_ = nullptr;
    std::string error_;
};

class ZipWriter {
public:
    explicit ZipWriter(const std::filesystem::path &path) {
        writer_ = mz_zip_writer_create();
        if (writer_ == nullptr) {
            error_ = "failed to create zip writer";
            return;
        }
        const auto path_string = path.string();
        if (mz_zip_writer_open_file(writer_, path_string.c_str(), 0, 0) != MZ_OK) {
            mz_zip_writer_delete(&writer_);
            error_ = "failed to open zip for write: " + path_string;
        }
    }

    ~ZipWriter() {
        if (writer_ != nullptr) {
            mz_zip_writer_close(writer_);
            mz_zip_writer_delete(&writer_);
        }
    }

    ZipWriter(const ZipWriter &) = delete;
    ZipWriter &operator=(const ZipWriter &) = delete;

    bool add(const std::string &name, const std::string &bytes) {
        if (writer_ == nullptr) return false;
        mz_zip_file info{};
        info.version_madeby = 20;
        info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
        info.flag = MZ_ZIP_FLAG_UTF8;
        info.modified_date = std::time(nullptr);
        info.uncompressed_size = static_cast<int64_t>(bytes.size());
        info.filename = name.c_str();
        const auto err = mz_zip_writer_add_buffer(
            writer_,
            const_cast<char *>(bytes.data()),
            static_cast<int32_t>(bytes.size()),
            &info);
        if (err != MZ_OK) {
            error_ = "failed to add zip part: " + name;
            return false;
        }
        return true;
    }

    bool close() {
        if (writer_ == nullptr) return false;
        const auto err = mz_zip_writer_close(writer_);
        mz_zip_writer_delete(&writer_);
        if (err != MZ_OK) {
            error_ = "failed to close zip package";
            return false;
        }
        return true;
    }

    const std::string &error() const { return error_; }

private:
    void *writer_ = nullptr;
    std::string error_;
};

} // namespace

OoxmlParts readOoxmlParts(const std::filesystem::path &path, std::string &error) {
    OoxmlParts package;
    ZipReader reader(path);
    if (!reader.error().empty()) {
        error = reader.error();
        return package;
    }
    int32_t err = mz_zip_reader_goto_first_entry(reader.get());
    while (err == MZ_OK) {
        mz_zip_file *info = nullptr;
        if (mz_zip_reader_entry_get_info(reader.get(), &info) != MZ_OK || info == nullptr || info->filename == nullptr) {
            err = mz_zip_reader_goto_next_entry(reader.get());
            continue;
        }
        std::string name = normalizePartName(info->filename);
        if (name.empty() || name.back() == '/') {
            err = mz_zip_reader_goto_next_entry(reader.get());
            continue;
        }
        if (info->uncompressed_size > 64 * 1024 * 1024) {
            error = "zip part exceeds 64 MiB: " + name;
            return {};
        }
        if (mz_zip_reader_entry_open(reader.get()) != MZ_OK) {
            error = "failed to open zip part: " + name;
            return {};
        }
        std::string bytes;
        const auto expected = static_cast<std::size_t>(info->uncompressed_size);
        if (expected > 0) bytes.reserve(expected);
        std::vector<char> buffer(64 * 1024);
        int32_t read = 0;
        while ((read = mz_zip_reader_entry_read(reader.get(), buffer.data(), static_cast<int32_t>(buffer.size()))) > 0) {
            bytes.append(buffer.data(), static_cast<std::size_t>(read));
        }
        mz_zip_reader_entry_close(reader.get());
        if (read < 0) {
            error = "failed to read zip part: " + name;
            return {};
        }
        package.parts.emplace(std::move(name), std::move(bytes));
        err = mz_zip_reader_goto_next_entry(reader.get());
    }
    return package;
}

bool writeOoxmlParts(const std::filesystem::path &path, const OoxmlParts &package, std::string &error) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "failed to create directory: " + path.parent_path().string();
            return false;
        }
    }
    const auto tmp = path.parent_path() / (path.filename().string() + ".pmoffice-tmp");
    std::filesystem::remove(tmp, ec);
    {
        ZipWriter writer(tmp);
        if (!writer.error().empty()) {
            error = writer.error();
            std::filesystem::remove(tmp, ec);
            return false;
        }
        auto write_one = [&](const std::string &name, const std::string &bytes) {
            return writer.add(name, bytes);
        };
        if (auto content = package.parts.find("[Content_Types].xml"); content != package.parts.end()) {
            if (!write_one(content->first, content->second)) {
                error = writer.error();
                std::filesystem::remove(tmp, ec);
                return false;
            }
        }
        for (const auto &[name, bytes] : package.parts) {
            if (name == "[Content_Types].xml") continue;
            if (!write_one(name, bytes)) {
                error = writer.error();
                std::filesystem::remove(tmp, ec);
                return false;
            }
        }
        if (!writer.close()) {
            error = writer.error();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        error = "failed to replace package: " + path.string();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace office
