#include "mal.hpp"

#include "encoding.hpp"
#include "stream_finder.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <ctime>
#include <random>
#include <utility>

namespace ryu {

namespace {

using nlohmann::json;

constexpr std::string_view listFields = "list_status,alternative_titles,media_type,num_episodes,start_season";
constexpr std::string_view animeFields = "my_list_status,alternative_titles,media_type,num_episodes,start_season";
constexpr int listPageSize = 1000;
constexpr int searchLimit = 20;
constexpr int verifierLength = 64;

constexpr std::array<std::pair<MalStatus, std::string_view>, 5> statusCodes{{
    {MalStatus::Watching, "watching"},
    {MalStatus::Completed, "completed"},
    {MalStatus::OnHold, "on_hold"},
    {MalStatus::Dropped, "dropped"},
    {MalStatus::PlanToWatch, "plan_to_watch"},
}};

std::string stringField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int intField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : 0;
}

bool boolField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

const json* objectField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_object() ? &*it : nullptr;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string body;
    for (const auto& [name, value] : fields) {
        body += (body.empty() ? "" : "&") + name + "=" + urlEncode(value);
    }
    return body;
}

std::string urlDecode(std::string_view text) {
    std::string output;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned value = 0;
        if (text[i] == '%' && i + 2 < text.size() &&
            std::from_chars(text.data() + i + 1, text.data() + i + 3, value, 16).ptr == text.data() + i + 3) {
            output.push_back(static_cast<char>(value));
            i += 2;
        } else {
            output.push_back(text[i] == '+' ? ' ' : text[i]);
        }
    }
    return output;
}

std::string errorMessage(const HttpResponse& response) {
    const auto parsed = json::parse(response.body, nullptr, false);
    if (parsed.is_object()) {
        auto message = stringField(parsed, "message");
        if (message.empty()) {
            message = stringField(parsed, "error");
        }
        if (!message.empty()) {
            return "MyAnimeList said: " + message;
        }
    }
    return "MyAnimeList returned HTTP " + std::to_string(response.status);
}

MalListStatus parseListStatus(const json& status) {
    MalListStatus list;
    list.status = malStatusFrom(stringField(status, "status"));
    list.score = intField(status, "score");
    list.watched = status.contains("num_episodes_watched") ? intField(status, "num_episodes_watched")
                                                           : intField(status, "num_watched_episodes");
    list.rewatching = boolField(status, "is_rewatching");
    list.startDate = stringField(status, "start_date");
    list.finishDate = stringField(status, "finish_date");
    return list;
}

MalAnime parseAnime(const json& node) {
    MalAnime anime;
    anime.id = intField(node, "id");
    anime.title = stringField(node, "title");
    if (const auto* titles = objectField(node, "alternative_titles")) {
        anime.englishTitle = stringField(*titles, "en");
        if (const auto synonyms = titles->find("synonyms"); synonyms != titles->end() && synonyms->is_array()) {
            for (const auto& synonym : *synonyms) {
                if (synonym.is_string()) {
                    anime.synonyms.push_back(synonym.get<std::string>());
                }
            }
        }
    }
    anime.mediaType = stringField(node, "media_type");
    anime.episodes = intField(node, "num_episodes");
    if (const auto* season = objectField(node, "start_season")) {
        anime.year = intField(*season, "year");
    }
    if (const auto* status = objectField(node, "my_list_status")) {
        anime.list = parseListStatus(*status);
    }
    return anime;
}

MalTokens requestTokens(HttpClient& http, const MalEndpoints& endpoints,
                        std::vector<std::pair<std::string, std::string>> fields, std::int64_t now, bool refreshing) {
    fields.insert(fields.begin(), {"client_id", endpoints.clientId});
    const auto response =
        http.post(endpoints.token, formEncode(fields), {{"Content-Type", "application/x-www-form-urlencoded"}});
    if (refreshing && (response.status == 400 || response.status == 401)) {
        throw MalLoginExpired();
    }
    if (response.status < 200 || response.status >= 300) {
        throw MalError(errorMessage(response));
    }
    const auto parsed = json::parse(response.body, nullptr, false);
    MalTokens tokens;
    if (parsed.is_object()) {
        tokens.access = stringField(parsed, "access_token");
        tokens.refresh = stringField(parsed, "refresh_token");
        tokens.expiresAt = now + intField(parsed, "expires_in");
    }
    if (tokens.access.empty() || tokens.refresh.empty()) {
        throw MalError("MyAnimeList's login answer had no tokens in it.");
    }
    return tokens;
}

std::string titleKey(std::string_view title) {
    std::string plain(title);
    for (const std::string_view curly : {"’", "‘"}) {
        for (auto at = plain.find(curly); at != std::string::npos; at = plain.find(curly, at + 1)) {
            plain.replace(at, curly.size(), "'");
        }
    }
    return normalizeTitle(plain);
}

std::vector<std::string> titleKeys(const MalAnime& anime) {
    std::vector<std::string> keys{titleKey(anime.title), titleKey(anime.englishTitle)};
    for (const auto& synonym : anime.synonyms) {
        keys.push_back(titleKey(synonym));
    }
    std::erase(keys, std::string());
    return keys;
}

}

MalEndpoints MalEndpoints::on(const std::string& server) {
    MalEndpoints endpoints;
    endpoints.authorize = server + "/v1/oauth2/authorize";
    endpoints.token = server + "/v1/oauth2/token";
    endpoints.api = server + "/v2";
    return endpoints;
}

std::string_view malStatusCode(MalStatus status) {
    for (const auto& [value, code] : statusCodes) {
        if (value == status) {
            return code;
        }
    }
    return {};
}

MalStatus malStatusFrom(std::string_view code) {
    for (const auto& [value, name] : statusCodes) {
        if (name == code) {
            return value;
        }
    }
    return MalStatus::None;
}

std::string malCodeVerifier() {
    static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::random_device device;
    std::uniform_int_distribution<size_t> pick(0, alphabet.size() - 1);
    std::string verifier;
    for (int i = 0; i < verifierLength; ++i) {
        verifier.push_back(alphabet[pick(device)]);
    }
    return verifier;
}

std::string malAuthorizationUrl(const MalEndpoints& endpoints, std::string_view verifier, std::string_view state) {
    return endpoints.authorize + "?" +
           formEncode({{"response_type", "code"},
                       {"client_id", endpoints.clientId},
                       {"state", std::string(state)},
                       {"redirect_uri", endpoints.redirectUri},
                       {"code_challenge", std::string(verifier)},
                       {"code_challenge_method", "plain"}});
}

MalRedirect parseMalRedirect(std::string_view target) {
    MalRedirect redirect;
    const auto question = target.find('?');
    auto query = question == std::string_view::npos ? std::string_view() : target.substr(question + 1);
    query = query.substr(0, query.find('#'));
    while (!query.empty()) {
        const auto end = query.find('&');
        const auto pair = query.substr(0, end);
        query = end == std::string_view::npos ? std::string_view() : query.substr(end + 1);
        const auto equals = pair.find('=');
        const auto name = pair.substr(0, equals);
        const auto value = urlDecode(equals == std::string_view::npos ? std::string_view() : pair.substr(equals + 1));
        if (name == "code") {
            redirect.code = value;
        } else if (name == "state") {
            redirect.state = value;
        } else if (name == "error") {
            redirect.error = value;
        }
    }
    return redirect;
}

MalTokens exchangeMalCode(HttpClient& http, const MalEndpoints& endpoints, std::string_view code,
                          std::string_view verifier, std::int64_t now) {
    return requestTokens(http, endpoints,
                         {{"grant_type", "authorization_code"},
                          {"code", std::string(code)},
                          {"code_verifier", std::string(verifier)},
                          {"redirect_uri", endpoints.redirectUri}},
                         now, false);
}

MalTokens refreshMalTokens(HttpClient& http, const MalEndpoints& endpoints, std::string_view refreshToken,
                           std::int64_t now) {
    return requestTokens(http, endpoints,
                         {{"grant_type", "refresh_token"}, {"refresh_token", std::string(refreshToken)}}, now, true);
}

MalClient::MalClient(HttpClient& http, MalEndpoints endpoints, std::string accessToken)
    : http_(http), endpoints_(std::move(endpoints)), accessToken_(std::move(accessToken)) {}

Headers MalClient::headers() const {
    return accessToken_.empty() ? Headers{{"X-MAL-CLIENT-ID", endpoints_.clientId}}
                                : Headers{{"Authorization", "Bearer " + accessToken_}};
}

std::string MalClient::fetch(const std::string& url) {
    auto response = http_.get(url, headers());
    if (response.status == 401) {
        throw MalLoginExpired();
    }
    if (response.status < 200 || response.status >= 300) {
        throw MalError(errorMessage(response));
    }
    return std::move(response.body);
}

std::string MalClient::userName() {
    const auto parsed = json::parse(fetch(endpoints_.api + "/users/@me"), nullptr, false);
    const auto name = parsed.is_object() ? stringField(parsed, "name") : std::string();
    if (name.empty()) {
        throw MalError("MyAnimeList didn't say who is logged in.");
    }
    return name;
}

std::vector<MalAnime> MalClient::list(MalStatus status) {
    auto url = endpoints_.api + "/users/@me/animelist?fields=" + std::string(listFields) +
               "&sort=anime_title&nsfw=true&limit=" + std::to_string(listPageSize);
    if (status != MalStatus::None) {
        url += "&status=" + std::string(malStatusCode(status));
    }
    std::vector<MalAnime> entries;
    while (!url.empty()) {
        const auto parsed = json::parse(fetch(url), nullptr, false);
        const auto data = parsed.is_object() ? parsed.find("data") : parsed.end();
        if (data == parsed.end() || !data->is_array()) {
            throw MalError("MyAnimeList sent a list Ryu could not read.");
        }
        for (const auto& edge : *data) {
            const auto* node = objectField(edge, "node");
            if (!node) {
                continue;
            }
            auto anime = parseAnime(*node);
            if (const auto* listStatus = objectField(edge, "list_status")) {
                anime.list = parseListStatus(*listStatus);
            }
            entries.push_back(std::move(anime));
        }
        const auto* paging = objectField(parsed, "paging");
        url = paging ? stringField(*paging, "next") : std::string();
    }
    return entries;
}

std::vector<MalAnime> MalClient::search(std::string_view query) {
    const auto parsed = json::parse(fetch(endpoints_.api + "/anime?q=" + urlEncode(query) +
                                          "&limit=" + std::to_string(searchLimit) +
                                          "&nsfw=true&fields=" + std::string(animeFields)),
                                    nullptr, false);
    std::vector<MalAnime> results;
    const auto data = parsed.is_object() ? parsed.find("data") : parsed.end();
    if (data == parsed.end() || !data->is_array()) {
        return results;
    }
    for (const auto& edge : *data) {
        if (const auto* node = objectField(edge, "node")) {
            results.push_back(parseAnime(*node));
        }
    }
    return results;
}

MalAnime MalClient::anime(int id) {
    const auto parsed = json::parse(
        fetch(endpoints_.api + "/anime/" + std::to_string(id) + "?fields=" + std::string(animeFields)), nullptr, false);
    if (!parsed.is_object() || intField(parsed, "id") == 0) {
        throw MalError("MyAnimeList sent an anime Ryu could not read.");
    }
    return parseAnime(parsed);
}

MalListStatus MalClient::update(int id, const MalChanges& changes) {
    std::vector<std::pair<std::string, std::string>> fields;
    if (changes.status) {
        fields.emplace_back("status", std::string(malStatusCode(*changes.status)));
    }
    if (changes.score) {
        fields.emplace_back("score", std::to_string(*changes.score));
    }
    if (changes.watched) {
        fields.emplace_back("num_watched_episodes", std::to_string(*changes.watched));
    }
    if (changes.rewatching) {
        fields.emplace_back("is_rewatching", *changes.rewatching ? "true" : "false");
    }
    if (changes.startDate) {
        fields.emplace_back("start_date", *changes.startDate);
    }
    if (changes.finishDate) {
        fields.emplace_back("finish_date", *changes.finishDate);
    }
    auto requestHeaders = headers();
    requestHeaders.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    const auto response = http_.send("PATCH", endpoints_.api + "/anime/" + std::to_string(id) + "/my_list_status",
                                     formEncode(fields), requestHeaders);
    if (response.status == 401) {
        throw MalLoginExpired();
    }
    if (response.status < 200 || response.status >= 300) {
        throw MalError(errorMessage(response));
    }
    const auto parsed = json::parse(response.body, nullptr, false);
    if (!parsed.is_object()) {
        throw MalError("MyAnimeList's answer to the update could not be read.");
    }
    return parseListStatus(parsed);
}

void MalClient::remove(int id) {
    const auto response =
        http_.send("DELETE", endpoints_.api + "/anime/" + std::to_string(id) + "/my_list_status", {}, headers());
    if (response.status == 401) {
        throw MalLoginExpired();
    }
    if (response.status != 404 && (response.status < 200 || response.status >= 300)) {
        throw MalError(errorMessage(response));
    }
}

MalAccess::MalAccess(HttpClient& http, MalEndpoints endpoints, MalTokens tokens, Clock clock)
    : http_(http), endpoints_(std::move(endpoints)), clock_(std::move(clock)), tokens_(std::move(tokens)) {}

MalTokens MalAccess::tokens() {
    std::scoped_lock lock(mutex_);
    return tokens_;
}

void MalAccess::setTokens(MalTokens tokens) {
    std::scoped_lock lock(mutex_);
    tokens_ = std::move(tokens);
}

std::int64_t MalAccess::now() const {
    return clock_ ? clock_() : static_cast<std::int64_t>(std::time(nullptr));
}

std::string MalAccess::freshAccess(const std::string& rejected) {
    std::scoped_lock lock(mutex_);
    if (tokens_.refresh.empty()) {
        throw MalLoginExpired();
    }
    if (!tokens_.usable(now()) || tokens_.access == rejected) {
        try {
            tokens_ = refreshMalTokens(http_, endpoints_, tokens_.refresh, now());
        } catch (const MalLoginExpired&) {
            tokens_ = {};
            throw;
        }
    }
    return tokens_.access;
}

namespace {

constexpr int yearTolerance = 1;

int listedEpisodes(const Show& show) {
    return std::max({show.subEpisodes, show.dubEpisodes, show.episodeCount});
}

bool sameTitle(const Show& show, const MalAnime& anime) {
    std::vector<std::string> wanted{titleKey(show.title), titleKey(show.altTitle)};
    std::erase(wanted, std::string());
    const auto keys = titleKeys(anime);
    return std::ranges::any_of(wanted, [&](const std::string& key) { return std::ranges::find(keys, key) != keys.end(); });
}

bool contradicts(const Show& show, const MalAnime& anime) {
    if (show.year > 0 && anime.year > 0 && std::abs(show.year - anime.year) > yearTolerance) {
        return true;
    }
    return anime.episodes > 0 && listedEpisodes(show) > anime.episodes;
}

template <typename Keep>
void narrow(std::vector<size_t>& matches, Keep keep) {
    std::vector<size_t> kept;
    std::ranges::copy_if(matches, std::back_inserter(kept), keep);
    if (!kept.empty()) {
        matches = std::move(kept);
    }
}

}

std::optional<size_t> matchMalAnime(const Show& show, const std::vector<MalAnime>& candidates) {
    if (show.malId > 0) {
        const auto it = std::ranges::find(candidates, show.malId, &MalAnime::id);
        return it == candidates.end() ? std::nullopt : std::optional<size_t>(it - candidates.begin());
    }
    std::vector<size_t> matches;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (sameTitle(show, candidates[i]) && !contradicts(show, candidates[i])) {
            matches.push_back(i);
        }
    }
    if (matches.size() > 1 && !show.format.empty()) {
        narrow(matches, [&](size_t i) { return titleKey(candidates[i].mediaType) == titleKey(show.format); });
    }
    if (matches.size() > 1 && show.year > 0) {
        narrow(matches, [&](size_t i) { return candidates[i].year == show.year; });
    }
    if (matches.size() > 1 && listedEpisodes(show) > 0) {
        narrow(matches, [&](size_t i) { return candidates[i].episodes == listedEpisodes(show); });
    }
    return matches.size() == 1 ? std::optional<size_t>(matches.front()) : std::nullopt;
}

std::optional<size_t> matchShowForMal(const MalAnime& anime, const std::vector<Show>& shows) {
    std::vector<size_t> matches;
    for (size_t i = 0; i < shows.size(); ++i) {
        const bool matched = shows[i].malId > 0 ? shows[i].malId == anime.id
                                                : sameTitle(shows[i], anime) && !contradicts(shows[i], anime);
        if (matched) {
            matches.push_back(i);
        }
    }
    narrow(matches, [&](size_t i) { return shows[i].malId == anime.id; });
    narrow(matches, [&](size_t i) { return titleKey(shows[i].format) == titleKey(anime.mediaType); });
    narrow(matches, [&](size_t i) { return anime.year > 0 && shows[i].year == anime.year; });
    narrow(matches, [&](size_t i) { return anime.episodes > 0 && listedEpisodes(shows[i]) == anime.episodes; });
    return matches.empty() ? std::nullopt : std::optional<size_t>(matches.front());
}

std::vector<MalAnime> findMalCandidates(MalClient& client, const Show& show) {
    if (show.malId > 0) {
        return {client.anime(show.malId)};
    }
    auto found = client.search(show.title);
    if (!matchMalAnime(show, found) && !show.altTitle.empty()) {
        for (auto& extra : client.search(show.altTitle)) {
            if (std::ranges::find(found, extra.id, &MalAnime::id) == found.end()) {
                found.push_back(std::move(extra));
            }
        }
    }
    return found;
}

MalChanges malChangesBetween(const MalListStatus& before, const MalListStatus& after) {
    MalChanges changes;
    if (after.status != before.status) {
        changes.status = after.status;
    }
    if (after.score != before.score) {
        changes.score = after.score;
    }
    if (after.watched != before.watched) {
        changes.watched = after.watched;
    }
    if (after.rewatching != before.rewatching) {
        changes.rewatching = after.rewatching;
    }
    if (after.startDate != before.startDate) {
        changes.startDate = after.startDate;
    }
    if (after.finishDate != before.finishDate) {
        changes.finishDate = after.finishDate;
    }
    return changes;
}

bool validMalDate(std::string_view date) {
    if (date.empty()) {
        return true;
    }
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    const auto number = [&](size_t from, size_t length, int& value) {
        const auto parsed = std::from_chars(date.data() + from, date.data() + from + length, value);
        return parsed.ec == std::errc() && parsed.ptr == date.data() + from + length;
    };
    if (!number(0, 4, year) || !number(5, 2, month) || !number(8, 2, day) || year < 1900 || month < 1 || month > 12) {
        return false;
    }
    constexpr std::array<int, 12> lengths{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return day >= 1 && day <= lengths[static_cast<size_t>(month - 1)] + (month == 2 && leap ? 1 : 0);
}

std::optional<MalChanges> malProgress(const MalAnime& anime, std::string_view episodeNumber, std::string_view today) {
    int episode = 0;
    const auto parsed = std::from_chars(episodeNumber.data(), episodeNumber.data() + episodeNumber.size(), episode);
    if (parsed.ec != std::errc() || parsed.ptr != episodeNumber.data() + episodeNumber.size() || episode <= 0) {
        return std::nullopt;
    }
    const auto& list = anime.list;
    if (list.status != MalStatus::Watching && !list.rewatching) {
        return std::nullopt;
    }
    if (episode <= list.watched || (anime.episodes > 0 && episode > anime.episodes)) {
        return std::nullopt;
    }
    MalChanges changes;
    changes.watched = episode;
    const bool last = anime.episodes > 0 && episode == anime.episodes;
    if (list.rewatching) {
        if (last) {
            changes.rewatching = false;
        }
        return changes;
    }
    if (list.watched == 0 && list.startDate.empty()) {
        changes.startDate = std::string(today);
    }
    if (last) {
        changes.status = MalStatus::Completed;
        changes.finishDate = std::string(today);
    }
    return changes;
}

}
