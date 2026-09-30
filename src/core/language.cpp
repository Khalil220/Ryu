#include "language.hpp"

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ryu {

std::string languageName(std::string_view code) {
    if (code == "tl") {
        code = "fil";
    }
    if (code.empty() || code.size() >= 32 ||
        !std::ranges::all_of(code, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; })) {
        return {};
    }
#ifdef _WIN32
    const std::wstring locale(code.begin(), code.end());
    DWORD id = 0;
    if (GetLocaleInfoEx(locale.c_str(), LOCALE_ILANGUAGE | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&id),
                        sizeof(id) / sizeof(wchar_t)) == 0 ||
        id == LOCALE_CUSTOM_UNSPECIFIED) {
        return {};
    }
    wchar_t name[128];
    const int length = GetLocaleInfoEx(locale.c_str(), LOCALE_SENGLISHDISPLAYNAME, name, 128);
    if (length <= 1) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, name, length - 1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name, length - 1, result.data(), size, nullptr, nullptr);
    return result;
#else
    return {};
#endif
}

}
