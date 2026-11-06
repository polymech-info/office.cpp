#pragma once

#include <string>

namespace http {

struct Response {
  long status_code;
  std::string body;
};

/// Options for customisable HTTP GET requests.
struct GetOptions {
  std::string user_agent = "Mozilla/5.0 (compatible; PolymechBot/1.0)";
  int timeout_ms = 10000;
  bool follow_redirects = true;
};

/// Perform an HTTP GET request. Returns the response body and status code.
Response get(const std::string &url);

/// Perform an HTTP GET request with custom options.
Response get(const std::string &url, const GetOptions &opts);

/// Perform an HTTP POST request with a body. Returns the response and status.
Response post(const std::string &url, const std::string &body,
              const std::string &content_type = "application/json");

/// Options for customisable HTTP POST requests.
struct PostOptions {
  std::string content_type = "application/json";
  std::string bearer_token;   // Authorization: Bearer <token>
  int timeout_ms = 30000;
};

/// Perform an HTTP POST request with custom options.
Response post(const std::string &url, const std::string &body,
              const PostOptions &opts);

} // namespace http
