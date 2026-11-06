#include "html/html.h"

#include <lexbor/css/css.h>
#include <lexbor/html/html.h>
#include <lexbor/selectors/selectors.h>
#include <html/html2md.h>

#include <algorithm>
#include <cstring>

namespace html {

// ── helpers ─────────────────────────────────────────────────────────────────

static std::string node_text(lxb_dom_node_t *node) {
  size_t len = 0;
  lxb_char_t *text = lxb_dom_node_text_content(node, &len);
  if (!text)
    return {};
  std::string result(reinterpret_cast<const char *>(text), len);
  lxb_dom_document_destroy_text(node->owner_document, text);
  return result;
}

static std::string tag_name(lxb_dom_element_t *el) {
  size_t len = 0;
  const lxb_char_t *name = lxb_dom_element_qualified_name(el, &len);
  if (!name)
    return {};
  return std::string(reinterpret_cast<const char *>(name), len);
}

static std::string get_element_attr(lxb_dom_element_t *el, const char *attr) {
  size_t len = 0;
  const lxb_char_t *val = lxb_dom_element_get_attribute(
      el, reinterpret_cast<const lxb_char_t *>(attr), strlen(attr), &len);
  if (!val)
    return {};
  return std::string(reinterpret_cast<const char *>(val), len);
}

static lxb_html_document_t *parse_doc(const std::string &html_str) {
  auto *doc = lxb_html_document_create();
  if (!doc) return nullptr;
  auto status = lxb_html_document_parse(
      doc, reinterpret_cast<const lxb_char_t *>(html_str.c_str()),
      html_str.size());
  if (status != LXB_STATUS_OK) {
    lxb_html_document_destroy(doc);
    return nullptr;
  }
  return doc;
}

// ── Helper: check if a tag name matches a noise element ─────────────────────

static bool is_noise_tag(const std::string &name) {
  return name == "script" || name == "style" || name == "noscript" ||
         name == "svg" || name == "iframe";
}

// ── walk tree recursively ───────────────────────────────────────────────────

static void walk(lxb_dom_node_t *node, std::vector<Element> &out) {
  if (!node)
    return;
  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
    auto *el = lxb_dom_interface_element(node);
    auto txt = node_text(node);
    if (!txt.empty()) {
      out.push_back({tag_name(el), txt});
    }
  }
  auto *child = node->first_child;
  while (child) {
    walk(child, out);
    child = child->next;
  }
}

// ── Walk for visible text only (skip noise tags) ────────────────────────────

static void walk_text(lxb_dom_node_t *node, std::string &out) {
  if (!node) return;

  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
    auto *el = lxb_dom_interface_element(node);
    auto name = tag_name(el);
    if (is_noise_tag(name)) return; // Skip noise subtrees entirely
  }

  if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
    size_t len = 0;
    const lxb_char_t *data = lxb_dom_node_text_content(node, &len);
    if (data && len > 0) {
      std::string chunk(reinterpret_cast<const char *>(data), len);
      // Collapse whitespace
      bool needSpace = !out.empty() && out.back() != ' ' && out.back() != '\n';
      // Trim leading/trailing whitespace from chunk
      size_t start = chunk.find_first_not_of(" \t\n\r");
      size_t end = chunk.find_last_not_of(" \t\n\r");
      if (start != std::string::npos) {
        if (needSpace) out += ' ';
        out += chunk.substr(start, end - start + 1);
      }
    }
  }

  auto *child = node->first_child;
  while (child) {
    walk_text(child, out);
    child = child->next;
  }
}

// ── Walk <head> for meta/title/link ─────────────────────────────────────────

struct HeadData {
  std::string title;
  std::string canonical;
  std::vector<std::pair<std::string, std::string>> metas; // name/property → content
  std::vector<std::string> json_ld;
};

static void walk_head(lxb_dom_node_t *node, HeadData &data) {
  if (!node) return;

  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
    auto *el = lxb_dom_interface_element(node);
    auto name = tag_name(el);

    if (name == "title") {
      data.title = node_text(node);
    } else if (name == "meta") {
      auto nameAttr = get_element_attr(el, "name");
      auto propAttr = get_element_attr(el, "property");
      auto content  = get_element_attr(el, "content");
      if (!content.empty()) {
        if (!nameAttr.empty()) data.metas.emplace_back(nameAttr, content);
        if (!propAttr.empty()) data.metas.emplace_back(propAttr, content);
      }
    } else if (name == "link") {
      auto rel = get_element_attr(el, "rel");
      if (rel == "canonical") {
        data.canonical = get_element_attr(el, "href");
      }
    } else if (name == "script") {
      auto type = get_element_attr(el, "type");
      if (type == "application/ld+json") {
        auto text = node_text(node);
        if (!text.empty()) data.json_ld.push_back(text);
      }
    }
  }

  auto *child = node->first_child;
  while (child) {
    walk_head(child, data);
    child = child->next;
  }
}

// ── Walk <body> for <a> links ───────────────────────────────────────────────

static void walk_links(lxb_dom_node_t *node, std::vector<Link> &out) {
  if (!node) return;

  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
    auto *el = lxb_dom_interface_element(node);
    auto name = tag_name(el);

    if (name == "a") {
      auto href = get_element_attr(el, "href");
      if (!href.empty()) {
        Link lk;
        lk.href = href;
        lk.rel  = get_element_attr(el, "rel");
        lk.text = node_text(node);
        out.push_back(std::move(lk));
      }
    }
  }

  auto *child = node->first_child;
  while (child) {
    walk_links(child, out);
    child = child->next;
  }
}

// ── public API ──────────────────────────────────────────────────────────────

std::vector<Element> parse(const std::string &html_str) {
  auto *doc = parse_doc(html_str);
  if (!doc) return {};

  std::vector<Element> result;
  auto *body = lxb_dom_interface_node(lxb_html_document_body_element(doc));
  walk(body, result);

  lxb_html_document_destroy(doc);
  return result;
}

// ── CSS selector callback ───────────────────────────────────────────────────

struct SelectCtx {
  std::vector<std::string> *out;
};

static lxb_status_t select_cb(lxb_dom_node_t *node,
                              lxb_css_selector_specificity_t spec, void *ctx) {
  (void)spec;
  auto *sctx = static_cast<SelectCtx *>(ctx);
  auto txt = node_text(node);
  if (!txt.empty()) {
    sctx->out->push_back(txt);
  }
  return LXB_STATUS_OK;
}

std::vector<std::string> select(const std::string &html_str,
                                const std::string &selector) {
  std::vector<std::string> result;

  auto *doc = parse_doc(html_str);
  if (!doc) return result;

  auto *css_parser = lxb_css_parser_create();
  lxb_css_parser_init(css_parser, nullptr);

  auto *selectors = lxb_selectors_create();
  lxb_selectors_init(selectors);

  auto *list = lxb_css_selectors_parse(
      css_parser, reinterpret_cast<const lxb_char_t *>(selector.c_str()),
      selector.size());

  if (list) {
    SelectCtx ctx{&result};
    lxb_selectors_find(
        selectors, lxb_dom_interface_node(lxb_html_document_body_element(doc)),
        list, select_cb, &ctx);
    lxb_css_selector_list_destroy_memory(list);
  }

  lxb_selectors_destroy(selectors, true);
  lxb_css_parser_destroy(css_parser, true);
  lxb_html_document_destroy(doc);

  return result;
}

// ── Enricher extraction helpers ─────────────────────────────────────────────

std::string get_title(const std::string &html_str) {
  auto *doc = parse_doc(html_str);
  if (!doc) return {};

  HeadData data;
  auto *head = lxb_dom_interface_node(lxb_html_document_head_element(doc));
  walk_head(head, data);

  lxb_html_document_destroy(doc);
  return data.title;
}

std::string get_meta(const std::string &html_str, const std::string &name) {
  auto *doc = parse_doc(html_str);
  if (!doc) return {};

  HeadData data;
  auto *head = lxb_dom_interface_node(lxb_html_document_head_element(doc));
  walk_head(head, data);

  lxb_html_document_destroy(doc);

  for (auto &[key, val] : data.metas) {
    if (key == name) return val;
  }
  return {};
}

std::string get_canonical(const std::string &html_str) {
  auto *doc = parse_doc(html_str);
  if (!doc) return {};

  HeadData data;
  auto *head = lxb_dom_interface_node(lxb_html_document_head_element(doc));
  walk_head(head, data);

  lxb_html_document_destroy(doc);
  return data.canonical;
}

std::vector<Link> get_links(const std::string &html_str) {
  auto *doc = parse_doc(html_str);
  if (!doc) return {};

  std::vector<Link> links;
  auto *body = lxb_dom_interface_node(lxb_html_document_body_element(doc));
  walk_links(body, links);

  lxb_html_document_destroy(doc);
  return links;
}

std::string get_body_text(const std::string &html_str) {
  auto *doc = parse_doc(html_str);
  if (!doc) return {};

  std::string text;
  auto *body = lxb_dom_interface_node(lxb_html_document_body_element(doc));
  walk_text(body, text);

  lxb_html_document_destroy(doc);
  return text;
}

std::vector<std::string> get_json_ld(const std::string &html_str) {
  auto *doc = parse_doc(html_str);
  if (!doc) return {};

  HeadData data;
  // JSON-LD can be in head or body — walk entire document
  auto *root = lxb_dom_interface_node(
      lxb_dom_document_element(&doc->dom_document));
  walk_head(root, data);

  lxb_html_document_destroy(doc);
  return data.json_ld;
}

// ── get_attr via CSS selector ───────────────────────────────────────────────

struct AttrCtx {
  std::string attr_name;
  std::string result;
  bool found;
};

static lxb_status_t attr_cb(lxb_dom_node_t *node,
                            lxb_css_selector_specificity_t spec, void *ctx) {
  (void)spec;
  auto *actx = static_cast<AttrCtx *>(ctx);
  if (actx->found) return LXB_STATUS_OK;

  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
    auto *el = lxb_dom_interface_element(node);
    auto val = get_element_attr(el, actx->attr_name.c_str());
    if (!val.empty()) {
      actx->result = val;
      actx->found = true;
    }
  }
  return LXB_STATUS_OK;
}

std::string get_attr(const std::string &html_str, const std::string &selector,
                     const std::string &attr_name) {
  auto *doc = parse_doc(html_str);
  if (!doc) return {};

  auto *css_parser = lxb_css_parser_create();
  lxb_css_parser_init(css_parser, nullptr);

  auto *selectors = lxb_selectors_create();
  lxb_selectors_init(selectors);

  auto *list = lxb_css_selectors_parse(
      css_parser, reinterpret_cast<const lxb_char_t *>(selector.c_str()),
      selector.size());

  std::string result;
  if (list) {
    AttrCtx ctx{attr_name, {}, false};
    auto *root = lxb_dom_interface_node(
        lxb_dom_document_element(&doc->dom_document));
    lxb_selectors_find(selectors, root, list, attr_cb, &ctx);
    result = ctx.result;
    lxb_css_selector_list_destroy_memory(list);
  }

  lxb_selectors_destroy(selectors, true);
  lxb_css_parser_destroy(css_parser, true);
  lxb_html_document_destroy(doc);

  return result;
}

std::string to_markdown(const std::string &html_str) {
  // Defense-in-depth: hard cap at 2 MB even if the caller forgets.
  // The enricher pipeline already caps at 512 KB, but future callers
  // may not — prevent OOM / multi-second hangs from html2md.
  static constexpr size_t MAX_HTML2MD_INPUT = 2 * 1024 * 1024;
  if (html_str.size() > MAX_HTML2MD_INPUT) {
    return "*[Content truncated: HTML too large for markdown conversion ("
           + std::to_string(html_str.size() / 1024) + " KB)]*\n";
  }
  return html2md::Convert(html_str);
}

} // namespace html
