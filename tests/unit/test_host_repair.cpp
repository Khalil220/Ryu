#include "fake_http.hpp"
#include "host_repair.hpp"
#include "playlist_server.hpp"

#include <doctest/doctest.h>

using namespace ryu;
using ryu::test::FakeHttpClient;

namespace {

constexpr const char* masterUrl = "https://fetch.example.top/anime/a/b/master.m3u8";
constexpr const char* mediaUrl = "https://fetch.example.top/anime/a/b/index-f1.m3u8";
constexpr const char* abuse = "https://www.cloudflare-terms-of-service-abuse.com/stream.jpeg";
const std::string tsBytes = std::string("\x47\x40\x11\x10", 4) + std::string(20, '\x00');

Stream megaplayStream() {
    Stream stream;
    stream.url = masterUrl;
    stream.headers = {{"Referer", "https://megaplay.buzz/"}};
    stream.alternateHosts = {"fetch.example.top"};
    stream.subtitles = {{"https://dead.example.online/anime/a/b/subtitles/eng-2.vtt", "eng", "English", true}};
    return stream;
}

void serveMaster(FakeHttpClient& http) {
    http.serve(masterUrl, "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1800112,RESOLUTION=1920x1080\nindex-f1.m3u8\n"
                          "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=49071,URI=\"iframes-f1.m3u8\"\n");
}

void serveMedia(FakeHttpClient& http, const std::string& host) {
    http.serve(mediaUrl, "#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXTINF:10,\nhttps://" + host +
                             "/anime/a/b/seg-1-f1.jpg\n#EXTINF:10,\nhttps://" + host +
                             "/anime/a/b/seg-2-f1.html\n#EXT-X-ENDLIST\n");
}

}

TEST_CASE("the playlist server serves published files over loopback and forgets old ones") {
    PlaylistServer server;
    CurlHttpClient http;

    const auto first = server.publish({{"master.m3u8", "#EXTM3U\nfirst\n"}});
    CHECK(first.starts_with("http://127.0.0.1:"));
    const auto response = http.get(first + "master.m3u8");
    CHECK(response.status == 200);
    CHECK(response.body == "#EXTM3U\nfirst\n");
    CHECK(http.get(first + "missing.m3u8").status == 404);

    for (int i = 0; i < 4; ++i) {
        server.publish({{"master.m3u8", "#EXTM3U\nlater\n"}});
    }
    CHECK(http.get(first + "master.m3u8").status == 404);
}

TEST_CASE("a stream whose hosts are all alive is left alone") {
    FakeHttpClient http;
    serveMaster(http);
    serveMedia(http, "live.example.site");
    http.serve("https://live.example.site/anime/a/b/seg-1-f1.jpg", tsBytes);
    PlaylistServer server;

    const auto report = repairStreamHosts(http, server, megaplayStream());

    CHECK(report.deadHosts.empty());
    CHECK(report.stream.url == masterUrl);
    CHECK(http.header(2, "Referer") == "https://megaplay.buzz/");
}

TEST_CASE("a banned host is swapped for a working alternate and served from loopback") {
    FakeHttpClient http;
    serveMaster(http);
    serveMedia(http, "dead.example.online");
    http.serveRedirect("https://dead.example.online/anime/a/b/seg-1-f1.jpg", abuse);
    http.serve("https://fetch.example.top/anime/a/b/seg-1-f1.jpg", tsBytes);
    PlaylistServer server;

    const auto report = repairStreamHosts(http, server, megaplayStream());

    REQUIRE(report.deadHosts.size() == 1);
    CHECK(report.deadHosts[0] == "dead.example.online");
    CHECK(report.replacementHost == "fetch.example.top");
    CHECK(report.stream.url.starts_with("http://127.0.0.1:"));
    CHECK(report.stream.url.ends_with("/master.m3u8"));
    CHECK(report.stream.subtitles[0].url == "https://fetch.example.top/anime/a/b/subtitles/eng-2.vtt");

    CurlHttpClient loopback;
    const auto master = loopback.get(report.stream.url).body;
    CHECK(master.find("\nmedia-0.m3u8") != std::string::npos);
    CHECK(master.find("URI=\"https://fetch.example.top/anime/a/b/iframes-f1.m3u8\"") != std::string::npos);
    const auto media = loopback.get(report.stream.url.substr(0, report.stream.url.size() - 11) + "media-0.m3u8").body;
    CHECK(media.find("https://fetch.example.top/anime/a/b/seg-1-f1.jpg") != std::string::npos);
    CHECK(media.find("https://fetch.example.top/anime/a/b/seg-2-f1.html") != std::string::npos);
    CHECK(media.find("dead.example.online") == std::string::npos);
}

TEST_CASE("a dead host behind any quality level is swapped, not only the first level's") {
    FakeHttpClient http;
    http.serve(masterUrl, "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=5500000,RESOLUTION=1920x1080\nindex-f1.m3u8\n"
                          "#EXT-X-STREAM-INF:BANDWIDTH=2800000,RESOLUTION=1280x720\nindex-f2.m3u8\n"
                          "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\nindex-f3.m3u8\n");
    const auto level = [&](const std::string& name, const std::string& host) {
        http.serve("https://fetch.example.top/anime/a/b/index-" + name + ".m3u8",
                   "#EXTM3U\n#EXTINF:10,\nhttps://" + host + "/anime/a/b/seg-" + name + "-00000.jpg\n#EXT-X-ENDLIST\n");
    };
    level("f1", "one.example.top");
    level("f2", "two.example.shop");
    level("f3", "gone.example.shop");
    http.serve("https://one.example.top/anime/a/b/seg-f1-00000.jpg", tsBytes);
    http.serve("https://two.example.shop/anime/a/b/seg-f2-00000.jpg", tsBytes);
    http.serve("https://gone.example.shop/anime/a/b/seg-f3-00000.jpg", "<html>404</html>", 404);
    PlaylistServer server;

    const auto report = repairStreamHosts(http, server, megaplayStream());

    REQUIRE(report.deadHosts == std::vector<std::string>{"gone.example.shop"});
    CHECK(report.replacementHost == "one.example.top");
    CurlHttpClient loopback;
    const auto base = report.stream.url.substr(0, report.stream.url.size() - 11);
    CHECK(loopback.get(base + "media-0.m3u8").body.find("https://one.example.top/anime/a/b/seg-f1-00000.jpg") !=
          std::string::npos);
    CHECK(loopback.get(base + "media-1.m3u8").body.find("https://two.example.shop/anime/a/b/seg-f2-00000.jpg") !=
          std::string::npos);
    CHECK(loopback.get(base + "media-2.m3u8").body.find("https://one.example.top/anime/a/b/seg-f3-00000.jpg") !=
          std::string::npos);
}

TEST_CASE("only the dead hosts in a rotating playlist are swapped, onto a living one") {
    FakeHttpClient http;
    http.serve(mediaUrl, "#EXTM3U\n#EXTINF:6,\n//st1.alpha.xyz/v/000.jpg\n#EXTINF:6,\n//st1.beta.xyz/v/001.jpg\n"
                         "#EXTINF:6,\n//st1.alpha.xyz/v/002.jpg\n#EXT-X-ENDLIST\n");
    http.serve("https://st1.alpha.xyz/v/000.jpg", tsBytes);
    http.serveRedirect("https://st1.beta.xyz/v/001.jpg", abuse);
    PlaylistServer server;
    Stream stream;
    stream.url = mediaUrl;

    const auto report = repairStreamHosts(http, server, stream);

    REQUIRE(report.deadHosts.size() == 1);
    CHECK(report.deadHosts[0] == "st1.beta.xyz");
    CHECK(report.replacementHost == "st1.alpha.xyz");
    CurlHttpClient loopback;
    const auto media = loopback.get(report.stream.url).body;
    CHECK(media.find("https://st1.alpha.xyz/v/001.jpg") != std::string::npos);
    CHECK(media.find("https://st1.alpha.xyz/v/002.jpg") != std::string::npos);
}

TEST_CASE("a stream with no reachable host is reported as a provider error") {
    FakeHttpClient http;
    serveMaster(http);
    serveMedia(http, "dead.example.online");
    http.serveRedirect("https://dead.example.online/anime/a/b/seg-1-f1.jpg", abuse);
    http.serve("https://fetch.example.top/anime/a/b/seg-1-f1.jpg", "<html>404</html>", 404);
    PlaylistServer server;

    CHECK_THROWS_WITH_AS(repairStreamHosts(http, server, megaplayStream()), doctest::Contains("not reachable"),
                         ProviderError);
}

TEST_CASE("a stream whose playlist cannot be read is passed through for the player to report") {
    FakeHttpClient http;
    http.serve(masterUrl, "<html>error</html>", 500);
    PlaylistServer server;

    const auto report = repairStreamHosts(http, server, megaplayStream());

    CHECK(report.stream.url == masterUrl);
    CHECK(report.deadHosts.empty());
}
