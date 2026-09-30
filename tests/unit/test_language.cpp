#include "language.hpp"

#include <doctest/doctest.h>

using ryu::isLanguageName;
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

TEST_CASE("isLanguageName recognises the English names of languages") {
    CHECK(isLanguageName("English"));
    CHECK(isLanguageName("Malay"));
    CHECK(isLanguageName("Filipino"));
    CHECK(isLanguageName("Norwegian"));
    CHECK(isLanguageName("Chinese"));
    CHECK_FALSE(isLanguageName("wowmdildo"));
    CHECK_FALSE(isLanguageName(""));
}
