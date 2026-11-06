#include "http/http.h"

#include <curl/curl.h>
#include <mutex>
#include <chrono>

namespace http {

static std::once_flag curl_init_flag;
static void ensure_curl_init() {
  std::call_once(curl_init_flag, []() {
    curl_global_init(CURL_GLOBAL_ALL);
  });
}

struct ThreadLocalCurl {
  CURL *handle;
  ThreadLocalCurl() {
    ensure_curl_init();
    handle = curl_easy_init();
  }
  ~ThreadLocalCurl() {
    if (handle) curl_easy_cleanup(handle);
  }
  CURL *get() {
    if (handle) curl_easy_reset(handle);
    return handle;
  }
};

thread_local ThreadLocalCurl tl_curl;

struct ProgressData {
  std::chrono::steady_clock::time_point start_time;
  int timeout_ms;
};

static int progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow) {
  auto *pd = static_cast<ProgressData *>(clientp);
  if (pd->timeout_ms <= 0) return 0;

  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pd->start_time).count();
  if (elapsed > pd->timeout_ms) {
    return 1; // Return non-zero to abort the transfer
  }
  return 0; // Continue
}

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
  auto *out = static_cast<std::string *>(userp);
  out->append(static_cast<char *>(contents), size * nmemb);
  return size * nmemb;
}

Response get(const std::string &url) {
  return get(url, GetOptions{});
}

Response get(const std::string &url, const GetOptions &opts) {
  Response resp{};

  CURL *curl = tl_curl.get();
  if (!curl) {
    resp.status_code = -1;
    resp.body = "curl_easy_init (thread_local) failed";
    return resp;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, opts.follow_redirects ? 1L : 0L);
  
  ProgressData prog_data;
  if (opts.timeout_ms > 0) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(opts.timeout_ms));
    prog_data.start_time = std::chrono::steady_clock::now();
    prog_data.timeout_ms = opts.timeout_ms + 1000;
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog_data);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  }

  // Fail fast on dead sites (TCP SYN timeout)
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);

  // Prevent stalling: abort if transfer speed is less than 1 byte/sec for 10 seconds
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);

  // Prevent signal handlers from breaking in multithreaded environments
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  if (!opts.user_agent.empty()) {
    curl_easy_setopt(curl, CURLOPT_USERAGENT, opts.user_agent.c_str());
  }

  // Accept-Encoding for compressed responses
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    resp.status_code = -1;
    resp.body = curl_easy_strerror(res);
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
  }

  return resp;
}

Response post(const std::string &url, const std::string &body,
              const std::string &content_type) {
  Response resp{};

  CURL *curl = tl_curl.get();
  if (!curl) {
    resp.status_code = -1;
    resp.body = "curl_easy_init failed";
    return resp;
  }

  struct curl_slist *headers = nullptr;
  headers =
      curl_slist_append(headers, ("Content-Type: " + content_type).c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  ProgressData prog_data;
  prog_data.start_time = std::chrono::steady_clock::now();
  prog_data.timeout_ms = 11000;
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog_data);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

  // Prevent stalling: abort if transfer speed is less than 1 byte/sec for 10 seconds
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    resp.status_code = -1;
    resp.body = curl_easy_strerror(res);
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
  }

  curl_slist_free_all(headers);
  return resp;
}

Response post(const std::string &url, const std::string &body,
              const PostOptions &opts) {
  Response resp{};

  CURL *curl = tl_curl.get();
  if (!curl) {
    resp.status_code = -1;
    resp.body = "curl_easy_init failed";
    return resp;
  }

  struct curl_slist *headers = nullptr;
  headers =
      curl_slist_append(headers, ("Content-Type: " + opts.content_type).c_str());
  if (!opts.bearer_token.empty()) {
    headers = curl_slist_append(
        headers, ("Authorization: Bearer " + opts.bearer_token).c_str());
    headers = curl_slist_append(
        headers, ("x-api-token: " + opts.bearer_token).c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  
  ProgressData prog_data;
  if (opts.timeout_ms > 0) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(opts.timeout_ms));
    prog_data.start_time = std::chrono::steady_clock::now();
    prog_data.timeout_ms = opts.timeout_ms + 1000;
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog_data);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  }

  // Prevent stalling: abort if transfer speed is less than 1 byte/sec for 10 seconds
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    resp.status_code = -1;
    resp.body = curl_easy_strerror(res);
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
  }

  curl_slist_free_all(headers);
  return resp;
}

} // namespace http
