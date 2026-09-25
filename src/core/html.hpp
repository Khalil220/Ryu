#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct lxb_dom_node;
struct lxb_html_document;

namespace ryu {

class HtmlNode {
public:
    explicit HtmlNode(lxb_dom_node* node) : node_(node) {}

    std::string attr(std::string_view name) const;
    std::string text() const;
    std::vector<HtmlNode> select(std::string_view selector) const;
    std::optional<HtmlNode> first(std::string_view selector) const;

private:
    lxb_dom_node* node_;
};

class HtmlDocument {
public:
    explicit HtmlDocument(std::string_view html);
    ~HtmlDocument();
    HtmlDocument(const HtmlDocument&) = delete;
    HtmlDocument& operator=(const HtmlDocument&) = delete;

    HtmlNode root() const;
    std::vector<HtmlNode> select(std::string_view selector) const { return root().select(selector); }
    std::optional<HtmlNode> first(std::string_view selector) const { return root().first(selector); }

private:
    lxb_html_document* document_ = nullptr;
};

}
