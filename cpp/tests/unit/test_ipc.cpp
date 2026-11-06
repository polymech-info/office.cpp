#include <catch2/catch_test_macros.hpp>

#include "ipc/ipc.h"

#include <cstring>

TEST_CASE("ipc::encode produces a 4-byte LE length prefix", "[ipc]") {
  ipc::Message msg{"1", "ping", "{}"};
  auto frame = ipc::encode(msg);

  REQUIRE(frame.size() > 4);

  // First 4 bytes are the LE length of the JSON body
  uint32_t body_len = static_cast<uint32_t>(frame[0]) |
                      (static_cast<uint32_t>(frame[1]) << 8) |
                      (static_cast<uint32_t>(frame[2]) << 16) |
                      (static_cast<uint32_t>(frame[3]) << 24);

  REQUIRE(body_len == frame.size() - 4);
}

TEST_CASE("ipc::encode → decode round-trip", "[ipc]") {
  ipc::Message original{"42", "job", R"({"action":"resize","width":800})"};
  auto frame = ipc::encode(original);

  // Strip the 4-byte length prefix for decode
  ipc::Message decoded;
  bool ok = ipc::decode(frame.data() + 4, frame.size() - 4, decoded);

  REQUIRE(ok);
  REQUIRE(decoded.id == "42");
  REQUIRE(decoded.type == "job");
  // payload should round-trip (may be compacted)
  REQUIRE(decoded.payload.find("resize") != std::string::npos);
  REQUIRE(decoded.payload.find("800") != std::string::npos);
}

TEST_CASE("ipc::decode rejects invalid JSON", "[ipc]") {
  std::string garbage = "this is not json";
  ipc::Message out;
  bool ok = ipc::decode(reinterpret_cast<const uint8_t *>(garbage.data()),
                        garbage.size(), out);
  REQUIRE_FALSE(ok);
}

TEST_CASE("ipc::decode rejects JSON missing required fields", "[ipc]") {
  // Valid JSON but missing "id" and "type"
  std::string json = R"({"foo":"bar"})";
  ipc::Message out;
  bool ok = ipc::decode(reinterpret_cast<const uint8_t *>(json.data()),
                        json.size(), out);
  REQUIRE_FALSE(ok);
}

TEST_CASE("ipc::decode handles missing payload gracefully", "[ipc]") {
  std::string json = R"({"id":"1","type":"ping"})";
  ipc::Message out;
  bool ok = ipc::decode(reinterpret_cast<const uint8_t *>(json.data()),
                        json.size(), out);
  REQUIRE(ok);
  REQUIRE(out.id == "1");
  REQUIRE(out.type == "ping");
  REQUIRE(out.payload == "{}");
}

TEST_CASE("ipc::encode with empty payload", "[ipc]") {
  ipc::Message msg{"0", "ready", ""};
  auto frame = ipc::encode(msg);

  ipc::Message decoded;
  bool ok = ipc::decode(frame.data() + 4, frame.size() - 4, decoded);

  REQUIRE(ok);
  REQUIRE(decoded.id == "0");
  REQUIRE(decoded.type == "ready");
}

TEST_CASE("ipc::decode with vector overload", "[ipc]") {
  std::string json = R"({"id":"99","type":"shutdown","payload":{}})";
  std::vector<uint8_t> data(json.begin(), json.end());

  ipc::Message out;
  bool ok = ipc::decode(data, out);

  REQUIRE(ok);
  REQUIRE(out.id == "99");
  REQUIRE(out.type == "shutdown");
  REQUIRE(out.payload == "{}");
}
