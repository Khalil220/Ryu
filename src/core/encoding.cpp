#include "encoding.hpp"

#include <array>
#include <stdexcept>

namespace ryu {

namespace {

constexpr std::array<int, 256> buildBase64Table() {
    std::array<int, 256> table{};
    table.fill(-1);
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < alphabet.size(); ++i) {
        table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
    }
    table['-'] = 62;
    table['_'] = 63;
    return table;
}

constexpr auto base64Table = buildBase64Table();

bool isUnreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.' || c == '~';
}

}

std::string base64Decode(std::string_view input) {
    std::string output;
    output.reserve(input.size() * 3 / 4);
    unsigned int buffer = 0;
    int bits = 0;
    for (const char ch : input) {
        if (ch == '=') {
            break;
        }
        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t') {
            continue;
        }
        const int value = base64Table[static_cast<unsigned char>(ch)];
        if (value < 0) {
            throw std::invalid_argument("Invalid base64 input");
        }
        buffer = (buffer << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return output;
}

std::string xorCipher(std::string_view data, std::string_view key) {
    if (key.empty()) {
        throw std::invalid_argument("XOR key must not be empty");
    }
    std::string output(data);
    for (size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<char>(output[i] ^ key[i % key.size()]);
    }
    return output;
}

std::string urlEncode(std::string_view input) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(input.size());
    for (const char ch : input) {
        const auto c = static_cast<unsigned char>(ch);
        if (isUnreserved(c)) {
            output.push_back(ch);
        } else {
            output.push_back('%');
            output.push_back(hex[c >> 4]);
            output.push_back(hex[c & 0x0F]);
        }
    }
    return output;
}

}
