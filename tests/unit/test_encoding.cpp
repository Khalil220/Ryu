#include "encoding.hpp"

#include <doctest/doctest.h>

#include <stdexcept>

using namespace ryu;

TEST_CASE("base64Decode handles padded, unpadded and URL-safe input") {
    CHECK(base64Decode("aGVsbG8=") == "hello");
    CHECK(base64Decode("aGVsbG8") == "hello");
    CHECK(base64Decode("aGk_Pz8-") == base64Decode("aGk/Pz8+"));
    CHECK(base64Decode("") == "");
    CHECK(base64Decode("aHR0cHM6Ly96b2tvYW5pbWUudmlkZW8vc3RyZWFtL21hbC81Mjk5MS8xL3N1Yg==") ==
          "https://zokoanime.video/stream/mal/52991/1/sub");
}

TEST_CASE("base64Decode rejects characters outside the alphabet") {
    CHECK_THROWS_AS(base64Decode("aGV*bG8="), std::invalid_argument);
}

TEST_CASE("xorCipher round-trips and cycles the key") {
    const std::string plain = "{\"src\":\"https://example.com/master.m3u8\"}";
    const auto encoded = xorCipher(plain, "otaku-embed-v1");
    CHECK(encoded != plain);
    CHECK(xorCipher(encoded, "otaku-embed-v1") == plain);
    CHECK(xorCipher("ab", "\x01") == "`c");
    CHECK_THROWS_AS(xorCipher("data", ""), std::invalid_argument);
}

TEST_CASE("urlEncode escapes everything except unreserved characters") {
    CHECK(urlEncode("frieren") == "frieren");
    CHECK(urlEncode("one piece") == "one%20piece");
    CHECK(urlEncode("a&b=c/d?") == "a%26b%3Dc%2Fd%3F");
    CHECK(urlEncode("Re:Zero") == "Re%3AZero");
    CHECK(urlEncode("\xC3\xA9") == "%C3%A9");
    CHECK(urlEncode("-_.~") == "-_.~");
}

namespace {

const std::string megaplayKey = std::string("i?LMTAx0Q6,:}50U") + std::string(16, '\0');
const std::string megaplayIv = "W0;27ToaUpl_P%'c";
const std::string megaplayBlob =
    "wdeBruh3qqn_i5wUNnyaPcXqidp1UWP84FfPHzGyKXAz4mAVkH6j3DueswO2yXLWn8H-XMHNvbAo5Gsg7zIcFBuQI_zsUvMGI1gKwQsPTSHQHiF55R4"
    "BopgEQ-7jebQQ4C0Gu7YhaMucopp6d3Q8yAY9b5GdsSvPGq6CUn7SHyc";

}

TEST_CASE("aes256CbcDecrypt decrypts megaplay's source blob and strips the padding") {
    CHECK(aes256CbcDecrypt(base64Decode(megaplayBlob), megaplayKey, megaplayIv) ==
          "{\"file\":\"https://fetch.nexabloom.top/anime/bb6d2babd7797d94d8f4a8600bc9b44e/"
          "b7d51fb7e838ee9b60dcdb34b953bc07/master.m3u8\"}");
}

TEST_CASE("aes256CbcDecrypt rejects a wrong key, bad sizes and truncated input") {
    const std::string wrongKey(32, 'x');
    CHECK_THROWS_AS(aes256CbcDecrypt(base64Decode(megaplayBlob), wrongKey, megaplayIv), std::runtime_error);
    CHECK_THROWS_AS(aes256CbcDecrypt(base64Decode(megaplayBlob), "short", megaplayIv), std::invalid_argument);
    CHECK_THROWS_AS(aes256CbcDecrypt(base64Decode(megaplayBlob), megaplayKey, "short"), std::invalid_argument);
    CHECK_THROWS_AS(aes256CbcDecrypt("0123456789", megaplayKey, megaplayIv), std::invalid_argument);
}
