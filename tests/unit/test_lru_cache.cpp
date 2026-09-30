#include "lru_cache.hpp"

#include <doctest/doctest.h>

#include <string>

using ryu::LruCache;

TEST_CASE("LruCache finds what was put and misses what wasn't") {
    LruCache<std::string, std::string> cache(3);
    cache.put("a", "poster a");
    REQUIRE(cache.find("a") != nullptr);
    CHECK(*cache.find("a") == "poster a");
    CHECK(cache.find("b") == nullptr);
}

TEST_CASE("LruCache drops the least recently used entry once it's full") {
    LruCache<std::string, int> cache(3);
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);
    CHECK(cache.find("a") != nullptr);
    cache.put("d", 4);

    CHECK(cache.size() == 3);
    CHECK(cache.find("b") == nullptr);
    CHECK(cache.find("a") != nullptr);
    CHECK(cache.find("c") != nullptr);
    CHECK(cache.find("d") != nullptr);
}

TEST_CASE("LruCache replaces an existing entry without growing and counts it as used") {
    LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("a", 10);
    CHECK(cache.size() == 2);
    cache.put("c", 3);

    REQUIRE(cache.find("a") != nullptr);
    CHECK(*cache.find("a") == 10);
    CHECK(cache.find("b") == nullptr);
}

TEST_CASE("LruCache never holds more than its capacity") {
    LruCache<int, int> cache(60);
    for (int i = 0; i < 1000; ++i) {
        cache.put(i, i);
    }
    CHECK(cache.size() == 60);
    CHECK(cache.find(939) == nullptr);
    CHECK(cache.find(940) != nullptr);
    CHECK(cache.find(999) != nullptr);
}
