#include "html.hpp"

#include <lexbor/css/css.h>
#include <lexbor/html/html.h>
#include <lexbor/selectors/selectors.h>

#include <cctype>
#include <memory>
#include <stdexcept>

namespace ryu {

namespace {

const lxb_char_t* bytes(std::string_view text) {
    return reinterpret_cast<const lxb_char_t*>(text.data());
}

struct ParserDeleter {
    void operator()(lxb_css_parser_t* parser) const { lxb_css_parser_destroy(parser, true); }
};

struct SelectorsDeleter {
    void operator()(lxb_selectors_t* selectors) const { lxb_selectors_destroy(selectors, true); }
};

struct ListDeleter {
    void operator()(lxb_css_selector_list_t* list) const { lxb_css_selector_list_destroy_memory(list); }
};

lxb_status_t collect(lxb_dom_node_t* node, lxb_css_selector_specificity_t, void* context) {
    static_cast<std::vector<HtmlNode>*>(context)->emplace_back(node);
    return LXB_STATUS_OK;
}

std::string collapseWhitespace(std::string_view text) {
    std::string output;
    output.reserve(text.size());
    bool pendingSpace = false;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            pendingSpace = !output.empty();
            continue;
        }
        if (pendingSpace) {
            output.push_back(' ');
            pendingSpace = false;
        }
        output.push_back(ch);
    }
    return output;
}

}

std::string HtmlNode::attr(std::string_view name) const {
    size_t length = 0;
    const lxb_char_t* value =
        lxb_dom_element_get_attribute(lxb_dom_interface_element(node_), bytes(name), name.size(), &length);
    return value ? std::string(reinterpret_cast<const char*>(value), length) : std::string();
}

std::string HtmlNode::text() const {
    size_t length = 0;
    lxb_char_t* content = lxb_dom_node_text_content(node_, &length);
    if (!content) {
        return {};
    }
    auto result = collapseWhitespace(std::string_view(reinterpret_cast<const char*>(content), length));
    lxb_dom_document_destroy_text(node_->owner_document, content);
    return result;
}

std::vector<HtmlNode> HtmlNode::select(std::string_view selector) const {
    std::unique_ptr<lxb_css_parser_t, ParserDeleter> parser(lxb_css_parser_create());
    std::unique_ptr<lxb_selectors_t, SelectorsDeleter> selectors(lxb_selectors_create());
    if (!parser || !selectors || lxb_css_parser_init(parser.get(), nullptr) != LXB_STATUS_OK ||
        lxb_selectors_init(selectors.get()) != LXB_STATUS_OK) {
        throw std::runtime_error("Could not set up the CSS selector engine");
    }

    std::unique_ptr<lxb_css_selector_list_t, ListDeleter> list(
        lxb_css_selectors_parse(parser.get(), bytes(selector), selector.size()));
    if (!list || parser->status != LXB_STATUS_OK) {
        throw std::invalid_argument("Invalid CSS selector: " + std::string(selector));
    }

    lxb_selectors_opt_set(selectors.get(), LXB_SELECTORS_OPT_MATCH_FIRST);
    std::vector<HtmlNode> matches;
    if (lxb_selectors_find(selectors.get(), node_, list.get(), collect, &matches) != LXB_STATUS_OK) {
        throw std::runtime_error("CSS selector search failed for " + std::string(selector));
    }
    return matches;
}

std::optional<HtmlNode> HtmlNode::first(std::string_view selector) const {
    auto matches = select(selector);
    if (matches.empty()) {
        return std::nullopt;
    }
    return matches.front();
}

HtmlDocument::HtmlDocument(std::string_view html) : document_(lxb_html_document_create()) {
    if (!document_) {
        throw std::runtime_error("Could not create an HTML document");
    }
    if (lxb_html_document_parse(document_, bytes(html), html.size()) != LXB_STATUS_OK) {
        lxb_html_document_destroy(document_);
        throw std::runtime_error("Could not parse HTML");
    }
}

HtmlDocument::~HtmlDocument() {
    lxb_html_document_destroy(document_);
}

HtmlNode HtmlDocument::root() const {
    return HtmlNode(lxb_dom_interface_node(document_));
}

}
