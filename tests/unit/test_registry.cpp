#include "fake_http.hpp"
#include "registry.hpp"

#include <doctest/doctest.h>

using namespace ryu;

TEST_CASE("HiAnime is registered with its default base URL") {
    const auto* info = findProvider("hianime");
    REQUIRE(info != nullptr);
    CHECK(info->name == "HiAnime");
    CHECK(info->defaultBaseUrl == "https://hianime.at");
}

TEST_CASE("registered providers are created against the given base URL") {
    ryu::test::FakeHttpClient http;
    http.serve("https://custom.example/search?keyword=a", "<div id=\"main-content\"></div>");

    const auto provider = findProvider("hianime")->create(http, "https://custom.example");

    REQUIRE(provider != nullptr);
    CHECK(provider->search("a").empty());
    CHECK(http.requests.at(0).url == "https://custom.example/search?keyword=a");
}

TEST_CASE("unknown providers are not found") {
    CHECK(findProvider("wcostream") == nullptr);
}
