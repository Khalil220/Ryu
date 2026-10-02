#pragma once

#include "http.hpp"
#include "provider.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ryu {

inline constexpr unsigned short malRedirectPort = 47813;
inline constexpr std::int64_t malRefreshMargin = 120;

struct MalEndpoints {
    std::string clientId = "7b530d8dae27b90f2e328b462f335ce0";
    std::string authorize = "https://myanimelist.net/v1/oauth2/authorize";
    std::string token = "https://myanimelist.net/v1/oauth2/token";
    std::string api = "https://api.myanimelist.net/v2";
    std::string redirectUri = "http://localhost:47813/callback";

    static MalEndpoints on(const std::string& server);
};

class MalError : public std::runtime_error {
public:
    explicit MalError(const std::string& message) : std::runtime_error(message) {}
};

class MalLoginExpired : public MalError {
public:
    MalLoginExpired() : MalError("Your MyAnimeList login has expired. Log in again from Preferences.") {}
};

enum class MalStatus { None, Watching, Completed, OnHold, Dropped, PlanToWatch };

inline constexpr MalStatus malLists[] = {MalStatus::Watching, MalStatus::Completed, MalStatus::OnHold,
                                         MalStatus::Dropped, MalStatus::PlanToWatch};

std::string_view malStatusCode(MalStatus status);
MalStatus malStatusFrom(std::string_view code);

struct MalListStatus {
    MalStatus status = MalStatus::None;
    int score = 0;
    int watched = 0;
    bool rewatching = false;
    std::string startDate;
    std::string finishDate;

    bool operator==(const MalListStatus&) const = default;
};

struct MalAnime {
    int id = 0;
    std::string title;
    std::string englishTitle;
    std::vector<std::string> synonyms;
    std::string mediaType;
    int episodes = 0;
    int year = 0;
    MalListStatus list;
};

struct MalChanges {
    std::optional<MalStatus> status;
    std::optional<int> score;
    std::optional<int> watched;
    std::optional<bool> rewatching;
    std::optional<std::string> startDate;
    std::optional<std::string> finishDate;

    bool empty() const { return !status && !score && !watched && !rewatching && !startDate && !finishDate; }
    bool operator==(const MalChanges&) const = default;
};

struct MalTokens {
    std::string access;
    std::string refresh;
    std::int64_t expiresAt = 0;

    bool usable(std::int64_t now) const { return !access.empty() && now + malRefreshMargin < expiresAt; }
};

struct MalRedirect {
    std::string code;
    std::string state;
    std::string error;
};

std::string malCodeVerifier();
std::string malAuthorizationUrl(const MalEndpoints& endpoints, std::string_view verifier, std::string_view state);
MalRedirect parseMalRedirect(std::string_view target);
MalTokens exchangeMalCode(HttpClient& http, const MalEndpoints& endpoints, std::string_view code,
                          std::string_view verifier, std::int64_t now);
MalTokens refreshMalTokens(HttpClient& http, const MalEndpoints& endpoints, std::string_view refreshToken,
                           std::int64_t now);

class MalClient {
public:
    MalClient(HttpClient& http, MalEndpoints endpoints, std::string accessToken);

    std::string userName();
    std::vector<MalAnime> list(MalStatus status);
    std::vector<MalAnime> search(std::string_view query);
    MalAnime anime(int id);
    MalListStatus update(int id, const MalChanges& changes);
    void remove(int id);

private:
    Headers headers() const;
    std::string fetch(const std::string& url);

    HttpClient& http_;
    MalEndpoints endpoints_;
    std::string accessToken_;
};

std::optional<size_t> matchMalAnime(const Show& show, const std::vector<MalAnime>& candidates);
std::optional<size_t> matchShowForMal(const MalAnime& anime, const std::vector<Show>& shows);
std::vector<MalAnime> findMalCandidates(MalClient& client, const Show& show);
std::optional<MalChanges> malProgress(const MalAnime& anime, std::string_view episodeNumber, std::string_view today);

}
