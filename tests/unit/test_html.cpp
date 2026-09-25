#include "html.hpp"

#include <doctest/doctest.h>

#include <stdexcept>

using namespace ryu;

namespace {

constexpr const char* sample = R"(
<html><body>
  <div id="main">
    <p class="item" title="Tom &amp; Jerry&#039;s">  Hello
      <b>world</b>  </p>
    <p class="item other">Second</p>
  </div>
  <p class="item">Outside</p>
</body></html>
)";

}

TEST_CASE("select finds matching elements in document order") {
    const HtmlDocument document(sample);
    const auto all = document.select(".item");
    REQUIRE(all.size() == 3);
    CHECK(all[0].text() == "Hello world");
    CHECK(all[1].text() == "Second");
    CHECK(all[2].text() == "Outside");
}

TEST_CASE("descendant selectors and scoped selection stay inside their scope") {
    const HtmlDocument document(sample);
    CHECK(document.select("#main .item").size() == 2);
    const auto main = document.first("#main");
    REQUIRE(main);
    CHECK(main->select("p").size() == 2);
    CHECK(main->first("b")->text() == "world");
}

TEST_CASE("selector lists do not report the same element twice") {
    const HtmlDocument document(sample);
    CHECK(document.select(".item, .other").size() == 3);
}

TEST_CASE("attributes come back with entities decoded and missing ones empty") {
    const HtmlDocument document(sample);
    const auto item = document.first(".item");
    REQUIRE(item);
    CHECK(item->attr("title") == "Tom & Jerry's");
    CHECK(item->attr("class") == "item");
    CHECK(item->attr("data-missing").empty());
}

TEST_CASE("first returns nothing when no element matches") {
    const HtmlDocument document(sample);
    CHECK_FALSE(document.first(".nope").has_value());
    CHECK(document.select(".nope").empty());
}

TEST_CASE("fragments without html or body tags still parse") {
    const HtmlDocument document(R"(<a class="ep-item" data-id="9" data-number="1">One</a>)");
    const auto link = document.first(".ep-item");
    REQUIRE(link);
    CHECK(link->attr("data-id") == "9");
}

TEST_CASE("an invalid selector throws") {
    const HtmlDocument document(sample);
    CHECK_THROWS_AS(document.select("p[["), std::invalid_argument);
}
