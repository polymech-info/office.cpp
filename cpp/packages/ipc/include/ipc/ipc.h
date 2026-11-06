#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ipc {

/// A single IPC message: { id, type, payload (raw JSON string) }.
struct Message {
  std::string id;
  std::string type;
  std::string payload; // opaque JSON string (can be "{}" or any object)
};

/// Encode a Message into a length-prefixed binary frame.
/// Layout: [4-byte LE uint32 length][JSON bytes]
std::vector<uint8_t> encode(const Message &msg);

/// Decode a binary frame (without the 4-byte length prefix) into a Message.
/// Returns false if the JSON is invalid or missing required fields.
bool decode(const uint8_t *data, size_t len, Message &out);
bool decode(const std::vector<uint8_t> &frame, Message &out);

/// Blocking: read exactly one length-prefixed message from a FILE*.
/// Returns false on EOF or read error.
bool read_message(Message &out, FILE *in = stdin);

/// Write one length-prefixed message to a FILE*. Flushes after write.
/// Returns false on write error.
bool write_message(const Message &msg, FILE *out = stdout);

} // namespace ipc
