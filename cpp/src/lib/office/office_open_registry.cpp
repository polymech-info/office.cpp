#include "office_open_registry.hpp"

#include "fs/path_sanitizer.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace office {
namespace {

constexpr std::uint32_t k_magic = 0x504D4F52u;
constexpr std::uint32_t k_version = 1u;
constexpr int k_slots = 48;
constexpr std::size_t k_path_cap = 1024;

// libc++ in the Android NDK does not provide std::atomic_ref. The spin word
// stays a plain uint32 so the mapped table layout matches desktop.
#if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
constexpr std::size_t k_spin_align = std::atomic_ref<std::uint32_t>::required_alignment;
inline bool spin_cas(std::uint32_t &slot, std::uint32_t &expected, std::uint32_t desired) {
    return std::atomic_ref<std::uint32_t>(slot).compare_exchange_weak(
        expected, desired, std::memory_order_acq_rel, std::memory_order_relaxed);
}
inline void spin_clear(std::uint32_t &slot) {
    std::atomic_ref<std::uint32_t>(slot).store(0, std::memory_order_release);
}
#else
constexpr std::size_t k_spin_align = alignof(std::uint32_t);
inline bool spin_cas(std::uint32_t &slot, std::uint32_t &expected, std::uint32_t desired) {
    return __atomic_compare_exchange_n(&slot, &expected, desired, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}
inline void spin_clear(std::uint32_t &slot) {
    __atomic_store_n(&slot, 0u, __ATOMIC_RELEASE);
}
#endif

struct Slot {
    std::uint32_t state = 0;
    std::uint32_t pid = 0;
    char path[k_path_cap]{};
};

struct Table {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    alignas(k_spin_align) std::uint32_t spin = 0;
    Slot slots[k_slots]{};
};

std::uint32_t self_pid() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

bool pid_alive(std::uint32_t pid) {
    if (pid == 0) return false;
#if defined(_WIN32)
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) return false;
    DWORD code = 0;
    const BOOL ok = ::GetExitCodeProcess(process, &code);
    ::CloseHandle(process);
    return ok && code == STILL_ACTIVE;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
    return errno != ESRCH;
#endif
}

struct Mapping {
    Table *table = nullptr;
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE view = nullptr;
#else
    int fd = -1;
#endif
};

Mapping &mapping() {
    static Mapping mapped = [] {
        Mapping out;
        std::error_code ec;
        const auto path = std::filesystem::temp_directory_path(ec) / "pm-office-open.registry";
        if (ec) return out;
#if defined(_WIN32)
        out.file = ::CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out.file == INVALID_HANDLE_VALUE) return out;
        LARGE_INTEGER size{};
        if (!::GetFileSizeEx(out.file, &size) || size.QuadPart < static_cast<LONGLONG>(sizeof(Table))) {
            LARGE_INTEGER end{};
            end.QuadPart = static_cast<LONGLONG>(sizeof(Table));
            if (!::SetFilePointerEx(out.file, end, nullptr, FILE_BEGIN) || !::SetEndOfFile(out.file)) {
                ::CloseHandle(out.file);
                out.file = INVALID_HANDLE_VALUE;
                return out;
            }
        }
        out.view = ::CreateFileMappingW(out.file, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(Table)), nullptr);
        if (out.view == nullptr) {
            ::CloseHandle(out.file);
            out.file = INVALID_HANDLE_VALUE;
            return out;
        }
        out.table = static_cast<Table *>(::MapViewOfFile(out.view, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Table)));
        if (out.table == nullptr) {
            ::CloseHandle(out.view);
            ::CloseHandle(out.file);
            out.view = nullptr;
            out.file = INVALID_HANDLE_VALUE;
        }
#else
        const auto utf8 = media::path::path_to_utf8(path);
        out.fd = ::open(utf8.c_str(), O_RDWR | O_CREAT, 0666);
        if (out.fd < 0) return out;
        struct stat st {};
        if (::fstat(out.fd, &st) != 0) return out;
        if (st.st_size < static_cast<off_t>(sizeof(Table)) && ::ftruncate(out.fd, static_cast<off_t>(sizeof(Table))) != 0) {
            ::close(out.fd);
            out.fd = -1;
            return out;
        }
        void *view = ::mmap(nullptr, sizeof(Table), PROT_READ | PROT_WRITE, MAP_SHARED, out.fd, 0);
        if (view == MAP_FAILED) {
            ::close(out.fd);
            out.fd = -1;
            return out;
        }
        out.table = static_cast<Table *>(view);
#endif
        return out;
    }();
    return mapped;
}

bool lock_table(Table &table, std::string &error) {
    const auto pid = self_pid();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        std::uint32_t expected = 0;
        if (spin_cas(table.spin, expected, pid)) break;
        if (expected != pid && !pid_alive(expected) && spin_cas(table.spin, expected, pid)) {
            break;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            error = "office document registry lock timed out";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (table.magic != k_magic || table.version != k_version) {
        const auto held = table.spin;
        std::memset(&table, 0, sizeof(table));
        table.magic = k_magic;
        table.version = k_version;
        table.spin = held;
    }
    return true;
}

void unlock_table(Table &table) {
    spin_clear(table.spin);
}

struct TableLock {
    Table *table = nullptr;
    explicit TableLock(Table *held) : table(held) {}
    ~TableLock() {
        if (table != nullptr) unlock_table(*table);
    }
    TableLock(const TableLock &) = delete;
    TableLock &operator=(const TableLock &) = delete;
};

} // namespace

bool claim_open_path(const std::string &key, std::string &error) {
    if (key.empty() || key.size() >= k_path_cap) {
        error = "office path is too long to lock";
        return false;
    }
    Table *table = mapping().table;
    if (table == nullptr) {
        error = "office document registry is unavailable";
        return false;
    }
    if (!lock_table(*table, error)) return false;
    TableLock guard(table);
    const auto pid = self_pid();
    Slot *free_slot = nullptr;
    for (auto &slot : table->slots) {
        if (slot.state != 0 && std::strcmp(slot.path, key.c_str()) == 0) {
            if (slot.pid == pid || !pid_alive(slot.pid)) {
                slot.pid = pid;
                slot.state = 1;
                return true;
            }
            error = "document is open in another process";
            return false;
        }
        if (free_slot == nullptr && (slot.state == 0 || !pid_alive(slot.pid))) free_slot = &slot;
    }
    if (free_slot == nullptr) {
        error = "office document registry is full";
        return false;
    }
    std::memset(free_slot, 0, sizeof(*free_slot));
    std::memcpy(free_slot->path, key.data(), key.size());
    free_slot->pid = pid;
    free_slot->state = 1;
    return true;
}

void release_open_path(const std::string &key) {
    Table *table = mapping().table;
    if (table == nullptr || key.empty()) return;
    std::string error;
    if (!lock_table(*table, error)) return;
    TableLock guard(table);
    const auto pid = self_pid();
    for (auto &slot : table->slots) {
        if (slot.state != 0 && slot.pid == pid && std::strcmp(slot.path, key.c_str()) == 0) {
            slot.state = 0;
            slot.pid = 0;
            slot.path[0] = '\0';
        }
    }
}

} // namespace office
