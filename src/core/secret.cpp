#include "secret.hpp"

#include "encoding.hpp"

#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <wincrypt.h>
#endif

namespace ryu {

#ifdef _WIN32

namespace {

std::string transform(std::string_view input, bool protect) {
    DATA_BLOB in{static_cast<DWORD>(input.size()), reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))};
    DATA_BLOB out{};
    const BOOL done = protect ? CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                              : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                                                   &out);
    if (!done) {
        throw std::runtime_error("Windows could not protect or reveal a saved secret");
    }
    std::string result(reinterpret_cast<const char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return result;
}

}

std::string protectSecret(std::string_view secret) {
    return secret.empty() ? std::string() : base64Encode(transform(secret, true));
}

std::string revealSecret(std::string_view stored) {
    if (stored.empty()) {
        return {};
    }
    try {
        return transform(base64Decode(stored), false);
    } catch (const std::exception&) {
        return {};
    }
}

#else

std::string protectSecret(std::string_view) {
    throw std::runtime_error("Saved secrets are only available on Windows");
}

std::string revealSecret(std::string_view) {
    return {};
}

#endif

}
