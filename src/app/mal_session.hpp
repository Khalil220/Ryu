#pragma once

#include "background.hpp"
#include "mal.hpp"
#include "settings.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace ryu {

class MalSession {
public:
    using LoginDone = std::function<void(bool loggedIn, const std::string& error)>;

    MalSession(const MalAccount& account, std::weak_ptr<void> alive,
               std::function<void(const MalAccount&)> accountChanged);

    const MalAccount& account() const { return account_; }
    bool loggedIn() const { return account_.loggedIn(); }
    void logIn(LoginDone done);
    void cancelLogin();
    void logOut();

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

    std::weak_ptr<void> alive_;
    std::function<void(const MalAccount&)> accountChanged_;
    MalAccount account_;
    std::shared_ptr<Shared> shared_;
    bool loggingIn_ = false;
};

}
