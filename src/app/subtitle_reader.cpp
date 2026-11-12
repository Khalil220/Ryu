#include "subtitle_reader.hpp"

#include <algorithm>
#include <cctype>

namespace ryu {

namespace {

std::string collapse(std::string_view line) {
    std::string text;
    bool pendingSpace = false;
    for (const char ch : line) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            pendingSpace = !text.empty();
            continue;
        }
        if (pendingSpace) {
            text.push_back(' ');
            pendingSpace = false;
        }
        text.push_back(ch);
    }
    return text;
}

std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::ptrdiff_t characters(const std::string& line) {
    return std::ranges::count_if(line, [](unsigned char c) { return (c & 0xC0) != 0x80; });
}

bool isShort(const std::string& line) {
    return characters(line) <= 3;
}

std::vector<std::string> linesOf(std::string_view raw) {
    std::vector<std::string> lines;
    for (size_t start = 0; start <= raw.size();) {
        const auto end = std::min(raw.find('\n', start), raw.size());
        if (auto line = collapse(raw.substr(start, end - start)); !line.empty()) {
            lines.push_back(std::move(line));
        }
        start = end + 1;
    }
    return lines;
}

}

void SubtitleReader::setNoise(std::vector<std::string> noise) {
    noise_.clear();
    for (auto& entry : noise) {
        noise_.push_back(lower(std::move(entry)));
    }
}

void SubtitleReader::clear() {
    onScreen_.clear();
}

std::string SubtitleReader::next(std::string_view subText) {
    const auto lines = linesOf(subText);
    std::vector<std::string> distinct;
    for (const auto& line : lines) {
        if (std::ranges::find(distinct, line) == distinct.end()) {
            distinct.push_back(line);
        }
    }
    const bool karaoke = std::ranges::count_if(distinct, isShort) >= 2;
    std::string text;
    for (const auto& line : distinct) {
        if (std::ranges::find(onScreen_, line) != onScreen_.end() || characters(line) == 1 ||
            std::ranges::count(lines, line) >= 3 || (karaoke && isShort(line)) ||
            std::ranges::find(noise_, lower(line)) != noise_.end()) {
            continue;
        }
        text += (text.empty() ? "" : " ") + line;
    }
    onScreen_ = std::move(distinct);
    return text;
}

}
