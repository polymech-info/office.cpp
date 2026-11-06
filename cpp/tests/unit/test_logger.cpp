#include <catch2/catch_test_macros.hpp>

#include "logger/logger.h"

TEST_CASE("logger::init does not throw", "[logger]") {
  REQUIRE_NOTHROW(logger::init("test"));
}

TEST_CASE("logger functions do not throw after init", "[logger]") {
  logger::init("test");

  REQUIRE_NOTHROW(logger::info("info message"));
  REQUIRE_NOTHROW(logger::warn("warn message"));
  REQUIRE_NOTHROW(logger::error("error message"));
  REQUIRE_NOTHROW(logger::debug("debug message"));
}

TEST_CASE("logger::init can be called multiple times", "[logger]") {
  REQUIRE_NOTHROW(logger::init("first"));
  REQUIRE_NOTHROW(logger::init("second"));
  REQUIRE_NOTHROW(logger::info("after re-init"));
}
