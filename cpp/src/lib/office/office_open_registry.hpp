#pragma once

#include <string>

namespace office {

// Cross-process table of documents an agent run is holding. The mapped file is
// the lock: a second process fails a claim while the pid is still alive.
bool claim_open_path(const std::string &key, std::string &error);
void release_open_path(const std::string &key);

} // namespace office
