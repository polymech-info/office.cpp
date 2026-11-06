#include <catch2/catch_test_macros.hpp>

#include "json/json.h"

TEST_CASE("json::is_valid accepts valid JSON", "[json]") {
  CHECK(json::is_valid(R"({"key": "value"})"));
  CHECK(json::is_valid("[]"));
  CHECK(json::is_valid("123"));
  CHECK(json::is_valid("\"hello\""));
}

TEST_CASE("json::is_valid rejects invalid JSON", "[json]") {
  CHECK_FALSE(json::is_valid("{invalid}"));
  CHECK_FALSE(json::is_valid("{key: value}"));
}

TEST_CASE("json::get_string extracts string values", "[json]") {
  auto val =
      json::get_string(R"({"name": "polymech", "version": "1.0"})", "name");
  CHECK(val == "polymech");
}

TEST_CASE("json::get_string returns empty for missing key", "[json]") {
  auto val = json::get_string(R"({"name": "polymech"})", "missing");
  CHECK(val.empty());
}

TEST_CASE("json::get_int extracts int values", "[json]") {
  auto val = json::get_int(R"({"port": 8080, "name": "test"})", "port");
  CHECK(val == 8080);
}

TEST_CASE("json::keys lists top-level keys", "[json]") {
  auto k = json::keys(R"({"a": 1, "b": 2, "c": 3})");
  REQUIRE(k.size() == 3);
  CHECK(k[0] == "a");
  CHECK(k[1] == "b");
  CHECK(k[2] == "c");
}

TEST_CASE("json::prettify formats JSON", "[json]") {
  auto pretty = json::prettify(R"({"a":1})");
  REQUIRE(!pretty.empty());
  // Pretty output should contain newlines
  CHECK(pretty.find('\n') != std::string::npos);
}
