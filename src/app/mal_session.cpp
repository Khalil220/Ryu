#include "mal_session.hpp"

#include "log.hpp"
#include "mal_login.hpp"

#include <wx/ffile.h>
#include <wx/utils.h>

#include <algorithm>
#include <chrono>

namespace ryu {

namespace {

constexpr std::chrono::seconds loginTimeout{300};

struct LoginOutcome {
    MalTokens tokens;
    std::string userName;
    bool cancelled = false;
};

MalEndpoints endpoints() {
    wxString server;
    return wxGetEnv("RYU_MAL_SERVER", &server) ? MalEndpoints::on(server.utf8_string()) : MalEndpoints();
}

void openInBrowser(const std::string& url) {
    wxString record;
    if (wxGetEnv("RYU_OPENED_URLS", &record)) {
        wxFFile file(record, "a");
        if (file.IsOpened()) {
            file.Write(wxString::FromUTF8(url) + "\n", wxConvUTF8);
        }
        return;
    }
    wxLaunchDefaultBrowser(wxString::FromUTF8(url));
}

}

MalSession::MalSession(const MalAccount& account, std::weak_ptr<void> alive,
                       std::function<void(const MalAccount&)> accountChanged)
    : alive_(std::move(alive)),
      accountChanged_(std::move(accountChanged)),
      account_(account),
      shared_(std::make_shared<Shared>(endpoints(), account.tokens)) {}

void MalSession::logIn(LoginDone done) {
    if (loggingIn_) {
        return;
    }
    loggingIn_ = true;
    shared_->cancelLogin = false;
    const auto verifier = malCodeVerifier();
    const auto state = malCodeVerifier();
    runInBackground<LoginOutcome>(
        alive_,
        [shared = shared_, verifier, state] {
            const auto target = waitForMalRedirect(malRedirectPort, loginTimeout, shared->cancelLogin);
            if (target.empty()) {
                if (shared->cancelLogin) {
                    return LoginOutcome{{}, {}, true};
                }
                throw MalError("Ryu stopped waiting for the MyAnimeList login. Try again when you're ready.");
            }
            const auto redirect = parseMalRedirect(target);
            if (redirect.state != state) {
                throw MalError("The MyAnimeList login came back for a different request. Try again.");
            }
            if (redirect.code.empty()) {
                throw MalError("MyAnimeList did not approve the login.");
            }
            LoginOutcome outcome;
            outcome.tokens = exchangeMalCode(shared->http, shared->access.endpoints(), redirect.code, verifier,
                                             shared->access.now());
            outcome.userName = MalClient(shared->http, shared->access.endpoints(), outcome.tokens.access).userName();
            return outcome;
        },
        [this, done](LoginOutcome outcome) {
            loggingIn_ = false;
            if (outcome.cancelled) {
                done(false, {});
                return;
            }
            logLine("Logged in to MyAnimeList as " + outcome.userName);
            shared_->access.setTokens(outcome.tokens);
            account_ = {outcome.userName, outcome.tokens};
            accountChanged_(account_);
            done(true, {});
            refresh([](const std::string&) {});
        },
        [this, done](const std::string& message) {
            loggingIn_ = false;
            logLine("MyAnimeList login failed: " + message);
            done(false, message);
        });
    openInBrowser(malAuthorizationUrl(shared_->access.endpoints(), verifier, state));
}

void MalSession::cancelLogin() {
    shared_->cancelLogin = true;
}

void MalSession::logOut() {
    shared_->access.setTokens({});
    account_ = {};
    setEntries({}, false);
    accountChanged_(account_);
}

void MalSession::refresh(Done done) {
    const unsigned generation = ++entriesGeneration_;
    run<std::vector<MalAnime>>(
        [](MalClient& client) { return client.list(MalStatus::None); },
        [this, generation, done](std::vector<MalAnime> entries) {
            if (generation == entriesGeneration_ && loggedIn()) {
                setEntries(std::move(entries), true);
            }
            done({});
        },
        [done](const std::string& message) {
            logLine("Could not load the MyAnimeList lists: " + message);
            done(message);
        });
}

void MalSession::save(const MalAnime& anime, const MalChanges& changes, Done done) {
    run<MalListStatus>(
        [id = anime.id, changes](MalClient& client) { return client.update(id, changes); },
        [this, anime, done](MalListStatus status) {
            ++entriesGeneration_;
            auto entries = entries_;
            const auto existing = std::ranges::find(entries, anime.id, &MalAnime::id);
            if (existing != entries.end()) {
                existing->list = status;
            } else {
                entries.push_back(anime);
                entries.back().list = status;
            }
            setEntries(std::move(entries), entriesLoaded_);
            done({});
        },
        [done](const std::string& message) {
            logLine("Could not update MyAnimeList: " + message);
            done(message);
        });
}

void MalSession::remove(int id, Done done) {
    run<bool>(
        [id](MalClient& client) {
            client.remove(id);
            return true;
        },
        [this, id, done](bool) {
            ++entriesGeneration_;
            auto entries = entries_;
            std::erase_if(entries, [id](const MalAnime& anime) { return anime.id == id; });
            setEntries(std::move(entries), entriesLoaded_);
            done({});
        },
        [done](const std::string& message) {
            logLine("Could not remove from MyAnimeList: " + message);
            done(message);
        });
}

void MalSession::setEntries(std::vector<MalAnime> entries, bool loaded) {
    entries_ = std::move(entries);
    entriesLoaded_ = loaded;
    if (entriesChanged_) {
        entriesChanged_();
    }
}

void MalSession::syncAccount() {
    const auto tokens = shared_->access.tokens();
    if (tokens == account_.tokens) {
        return;
    }
    account_.tokens = tokens;
    if (!account_.loggedIn()) {
        logLine("The MyAnimeList login could not be renewed");
        account_ = {};
        setEntries({}, false);
    }
    accountChanged_(account_);
}

}
