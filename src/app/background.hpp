#pragma once

#include <wx/app.h>

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace ryu {

template <typename Result>
void runInBackground(std::weak_ptr<void> owner, std::function<Result()> work, std::function<void(Result)> onSuccess,
                     std::function<void(const std::string&)> onError) {
    std::thread([owner = std::move(owner), work = std::move(work), onSuccess = std::move(onSuccess),
                 onError = std::move(onError)] {
        std::function<void()> deliver;
        try {
            auto result = std::make_shared<Result>(work());
            deliver = [owner, result, onSuccess] {
                if (owner.lock()) {
                    onSuccess(std::move(*result));
                }
            };
        } catch (const std::exception& error) {
            deliver = [owner, message = std::string(error.what()), onError] {
                if (owner.lock()) {
                    onError(message);
                }
            };
        }
        if (auto* app = wxTheApp) {
            app->CallAfter(deliver);
        }
    }).detach();
}

}
