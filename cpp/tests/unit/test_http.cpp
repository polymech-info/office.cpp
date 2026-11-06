#include <catch2/catch_test_macros.hpp>

#include "http/http.h"

TEST_CASE("http::get returns a response", "[http]") {
  // This test requires network, so we test error handling for invalid URL
  auto resp = http::get("http://0.0.0.0:1/nonexistent");
  // Should fail gracefully with status -1
  CHECK(resp.status_code == -1);
  CHECK(!resp.body.empty());
}

TEST_CASE("http::post returns a response", "[http]") {
  auto resp = http::post("http://0.0.0.0:1/nonexistent", R"({"test": true})");
  CHECK(resp.status_code == -1);
  CHECK(!resp.body.empty());
}
