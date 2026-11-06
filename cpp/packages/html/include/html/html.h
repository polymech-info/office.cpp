#pragma once

#include <string>
#include <vector>

namespace html {

/// Parsed element — tag name + text content.
struct Element {
  std::string tag;
  std::string text;
};

/// Link with href and optional attributes.
struct Link {
  std::string href;
  std::string rel;   // e.g. "canonical", "stylesheet"
  std::string text;  // anchor text (for <a> tags)
};

/// Parse an HTML string and return all elements with their text content.
std::vector<Element> parse(const std::string &html_str);

/// Extract the text content of all elements matching a CSS selector.
std::vector<std::string> select(const std::string &html_str,
                                const std::string &selector);

// ── Enricher extraction helpers ─────────────────────────────────────────────

/// Extract the <title> text.
std::string get_title(const std::string &html_str);

/// Extract a <meta name="X"> or <meta property="X"> content attribute.
std::string get_meta(const std::string &html_str, const std::string &name);

/// Extract <link rel="canonical"> href.
std::string get_canonical(const std::string &html_str);

/// Extract all <a href="..."> values (resolved links as-is from the HTML).
std::vector<Link> get_links(const std::string &html_str);

/// Extract visible body text, stripping script/style/noscript/svg/iframe.
std::string get_body_text(const std::string &html_str);

/// Extract raw JSON strings from <script type="application/ld+json">.
std::vector<std::string> get_json_ld(const std::string &html_str);

/// Extract an attribute value from the first element matching a CSS selector.
std::string get_attr(const std::string &html_str, const std::string &selector,
                     const std::string &attr_name);

/// Convert HTML content to Markdown.
std::string to_markdown(const std::string &html_str);

} // namespace html
