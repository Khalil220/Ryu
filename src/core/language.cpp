#include "language.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ryu {

namespace {

#ifdef _WIN32
std::string narrow(const wchar_t* text, int length) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    return result;
}

BOOL CALLBACK collectLanguage(LPWSTR locale, DWORD, LPARAM names) {
    wchar_t name[128];
    const int length = GetLocaleInfoEx(locale, LOCALE_SENGLISHLANGUAGENAME, name, 128);
    if (length > 1) {
        const auto text = narrow(name, length - 1);
        reinterpret_cast<std::set<std::string>*>(names)->insert(text.substr(0, text.find_first_of(" (")));
    }
    return TRUE;
}
#endif

const std::set<std::string>& languageNames() {
    static const std::set<std::string> names = [] {
        std::set<std::string> collected;
#ifdef _WIN32
        EnumSystemLocalesEx(collectLanguage, LOCALE_ALL, reinterpret_cast<LPARAM>(&collected), nullptr);
#endif
        return collected;
    }();
    return names;
}

}

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
    return narrow(name, length - 1);
#else
    return {};
#endif
}

bool isLanguageName(std::string_view name) {
    return !name.empty() && languageNames().contains(std::string(name));
}

}
