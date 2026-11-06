#include "ipc/ipc.h"

#include <cstring>

#include "json/json.h"
#include "logger/logger.h"

// We use RapidJSON directly for structured serialization
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace ipc {

// ── helpers ──────────────────────────────────────────────────────────────────

static void write_u32_le(uint8_t *dst, uint32_t val) {
  dst[0] = static_cast<uint8_t>(val & 0xFF);
  dst[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
  dst[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
  dst[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

static uint32_t read_u32_le(const uint8_t *src) {
  return static_cast<uint32_t>(src[0]) |
         (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) |
         (static_cast<uint32_t>(src[3]) << 24);
}

static bool read_exact(FILE *f, uint8_t *buf, size_t n) {
  size_t total = 0;
  while (total < n) {
    size_t got = std::fread(buf + total, 1, n - total, f);
    if (got == 0) return false; // EOF or error
    total += got;
  }
  return true;
}

// ── encode ───────────────────────────────────────────────────────────────────

std::vector<uint8_t> encode(const Message &msg) {
  // Build JSON: { "id": "...", "type": "...", "payload": ... }
  // payload is stored as a raw JSON string, so we parse it first
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> w(sb);

  w.StartObject();
  w.Key("id");
  w.String(msg.id.c_str(), static_cast<rapidjson::SizeType>(msg.id.size()));
  w.Key("type");
  w.String(msg.type.c_str(),
           static_cast<rapidjson::SizeType>(msg.type.size()));
  w.Key("payload");

  // If payload is valid JSON, embed it as-is; otherwise embed as string
  rapidjson::Document pd;
  if (!msg.payload.empty() &&
      !pd.Parse(msg.payload.c_str()).HasParseError()) {
    pd.Accept(w);
  } else {
    w.String(msg.payload.c_str(),
             static_cast<rapidjson::SizeType>(msg.payload.size()));
  }

  w.EndObject();

  const char *json_str = sb.GetString();
  uint32_t json_len = static_cast<uint32_t>(sb.GetSize());

  std::vector<uint8_t> frame(4 + json_len);
  write_u32_le(frame.data(), json_len);
  std::memcpy(frame.data() + 4, json_str, json_len);

  return frame;
}

// ── decode ───────────────────────────────────────────────────────────────────

bool decode(const uint8_t *data, size_t len, Message &out) {
  rapidjson::Document doc;
  doc.Parse(reinterpret_cast<const char *>(data), len);

  if (doc.HasParseError() || !doc.IsObject()) return false;

  if (!doc.HasMember("id") || !doc["id"].IsString()) return false;
  if (!doc.HasMember("type") || !doc["type"].IsString()) return false;

  out.id = doc["id"].GetString();
  out.type = doc["type"].GetString();

  if (doc.HasMember("payload")) {
    if (doc["payload"].IsString()) {
      out.payload = doc["payload"].GetString();
    } else {
      // Re-serialize non-string payload back to JSON string
      rapidjson::StringBuffer sb;
      rapidjson::Writer<rapidjson::StringBuffer> w(sb);
      doc["payload"].Accept(w);
      out.payload = sb.GetString();
    }
  } else {
    out.payload = "{}";
  }

  return true;
}

bool decode(const std::vector<uint8_t> &frame, Message &out) {
  return decode(frame.data(), frame.size(), out);
}

// ── read_message ─────────────────────────────────────────────────────────────

bool read_message(Message &out, FILE *in) {
#ifdef _WIN32
  // Ensure binary mode on Windows to prevent \r\n translation
  _setmode(_fileno(in), _O_BINARY);
#endif

  uint8_t len_buf[4];
  if (!read_exact(in, len_buf, 4)) return false;

  uint32_t msg_len = read_u32_le(len_buf);
  if (msg_len == 0 || msg_len > 10 * 1024 * 1024) { // sanity: max 10 MB
    logger::error("ipc::read_message: invalid length " +
                  std::to_string(msg_len));
    return false;
  }

  std::vector<uint8_t> buf(msg_len);
  if (!read_exact(in, buf.data(), msg_len)) return false;

  return decode(buf, out);
}

// ── write_message ────────────────────────────────────────────────────────────

bool write_message(const Message &msg, FILE *out) {
#ifdef _WIN32
  _setmode(_fileno(out), _O_BINARY);
#endif

  auto frame = encode(msg);
  size_t written = std::fwrite(frame.data(), 1, frame.size(), out);
  if (written != frame.size()) return false;

  std::fflush(out);
  return true;
}

} // namespace ipc
