#include "language.hpp"

#include <doctest/doctest.h>

using ryu::languageName;

TEST_CASE("languageName names language codes in English") {
    CHECK(languageName("fil") == "Filipino");
    CHECK(languageName("en") == "English");
    CHECK(languageName("es-419") == "Spanish (Latin America)");
    CHECK(languageName("pt-BR") == "Portuguese (Brazil)");
}

TEST_CASE("languageName treats Tagalog's two-letter code as Filipino") {
    CHECK(languageName("tl") == "Filipino");
}

TEST_CASE("languageName gives nothing for codes Windows does not know") {
    CHECK(languageName("").empty());
    CHECK(languageName("xx").empty());
    CHECK(languageName("eng").empty());
    CHECK(languageName("Default").empty());
    CHECK(languageName("en US").empty());
}
