#pragma once

#include "background.hpp"
#include "mal.hpp"
#include "settings.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ryu {

class MalSession {
public:
    using LoginDone = std::function<void(bool loggedIn, const std::string& error)>;
    using Done = std::function<void(const std::string& error)>;

    MalSession(const MalAccount& account, std::weak_ptr<void> alive,
               std::function<void(const MalAccount&)> accountChanged);

    const MalAccount& account() const { return account_; }
    bool loggedIn() const { return account_.loggedIn(); }
    void logIn(LoginDone done);
    void cancelLogin();
    void logOut();

    const std::vector<MalAnime>& entries() const { return entries_; }
    bool entriesLoaded() const { return entriesLoaded_; }
    void whenEntriesChange(std::function<void()> listener) { entriesChanged_ = std::move(listener); }
    void refresh(Done done);
    void save(const MalAnime& anime, const MalChanges& changes, Done done);
    void remove(int id, Done done);

    template <typename Result>
    void run(std::function<Result(MalClient&)> work, std::function<void(Result)> onSuccess,
             std::function<void(const std::string&)> onError) {
        runInBackground<Result>(
            alive_, [shared = shared_, work = std::move(work)] { return shared->access.template call<Result>(work); },
            [this, onSuccess = std::move(onSuccess)](Result result) {
                syncAccount();
                onSuccess(std::move(result));
            },
            [this, onError = std::move(onError)](const std::string& message) {
                syncAccount();
                onError(message);
            });
    }

private:
    struct Shared {
        Shared(MalEndpoints endpoints, MalTokens tokens) : access(http, std::move(endpoints), std::move(tokens)) {}

        CurlHttpClient http;
        MalAccess access;
        std::atomic<bool> cancelLogin = false;
    };

    void syncAccount();
    void setEntries(std::vector<MalAnime> entries, bool loaded);

    std::weak_ptr<void> alive_;
    std::function<void(const MalAccount&)> accountChanged_;
    MalAccount account_;
    std::shared_ptr<Shared> shared_;
    bool loggingIn_ = false;
    std::vector<MalAnime> entries_;
    bool entriesLoaded_ = false;
    unsigned entriesGeneration_ = 0;
    std::function<void()> entriesChanged_;
};

}
