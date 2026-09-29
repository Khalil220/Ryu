#include "hls.hpp"

#include <doctest/doctest.h>

using namespace ryu;

namespace {

constexpr const char* master = "#EXTM3U\n"
                               "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"stereo\",NAME=\"English\",LANGUAGE=\"eng\","
                               "URI=\"audio/eng.m3u8\"\n"
                               "#EXT-X-STREAM-INF:BANDWIDTH=1800000,AUDIO=\"stereo\"\n"
                               "video/720.m3u8\n"
                               "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=49071,URI=\"iframes.m3u8\"\n";

}

TEST_CASE("resolveUrl handles absolute, protocol-relative, rooted and relative references") {
    const std::string base = "https://cdn.example/anime/show/master.m3u8?token=1";
    CHECK(resolveUrl(base, "https://other.example/x.ts") == "https://other.example/x.ts");
    CHECK(resolveUrl(base, "//st1.example/a/000.jpg") == "https://st1.example/a/000.jpg");
    CHECK(resolveUrl(base, "/keys/k.key") == "https://cdn.example/keys/k.key");
    CHECK(resolveUrl(base, "index-f1.m3u8") == "https://cdn.example/anime/show/index-f1.m3u8");
    CHECK(resolveUrl("https://cdn.example", "a.m3u8") == "https://cdn.example/a.m3u8");
}

TEST_CASE("hostOf and withHost work on the authority only") {
    CHECK(hostOf("https://swoax.example.online/anime/a/seg-1.jpg") == "swoax.example.online");
    CHECK(hostOf("not a url").empty());
    CHECK(withHost("https://dead.example/anime/a/seg-1.jpg?x=1", "live.example") ==
          "https://live.example/anime/a/seg-1.jpg?x=1");
    CHECK(withHost("https://dead.example", "live.example") == "https://live.example");
}

TEST_CASE("mediaPlaylistUris lists variants and renditions but not I-frame playlists") {
    CHECK(isMasterPlaylist(master));
    const auto uris = mediaPlaylistUris(master, "https://cdn.example/m/master.m3u8");
    REQUIRE(uris.size() == 2);
    CHECK(uris[0] == "https://cdn.example/m/audio/eng.m3u8");
    CHECK(uris[1] == "https://cdn.example/m/video/720.m3u8");
}

TEST_CASE("segmentUris resolves every segment line") {
    const auto uris = segmentUris("#EXTM3U\r\n#EXTINF:6,\r\n//a.example/000.jpg\r\n#EXTINF:6,\r\n001.jpg\r\n",
                                  "https://b.example/v/playlist.m3u8");
    REQUIRE(uris.size() == 2);
    CHECK(uris[0] == "https://a.example/000.jpg");
    CHECK(uris[1] == "https://b.example/v/001.jpg");
    CHECK_FALSE(isMasterPlaylist("#EXTM3U\n#EXTINF:6,\n000.ts\n"));
}

TEST_CASE("rewritePlaylist maps line and attribute URIs and keeps everything else") {
    const auto rewritten = rewritePlaylist(master, "https://cdn.example/m/master.m3u8",
                                           [](const std::string& url) { return "<" + url + ">"; });
    CHECK(rewritten == "#EXTM3U\n"
                       "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"stereo\",NAME=\"English\",LANGUAGE=\"eng\","
                       "URI=\"<https://cdn.example/m/audio/eng.m3u8>\"\n"
                       "#EXT-X-STREAM-INF:BANDWIDTH=1800000,AUDIO=\"stereo\"\n"
                       "<https://cdn.example/m/video/720.m3u8>\n"
                       "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=49071,URI=\"<https://cdn.example/m/iframes.m3u8>\"\n");
}
