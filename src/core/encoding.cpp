#include "encoding.hpp"

#include <array>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <bcrypt.h>
#endif

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

std::string aes256CbcDecrypt(std::string_view ciphertext, std::string_view key, std::string_view iv) {
    if (key.size() != 32 || iv.size() != 16) {
        throw std::invalid_argument("AES-256-CBC needs a 32 byte key and a 16 byte IV");
    }
    if (ciphertext.empty() || ciphertext.size() % 16 != 0) {
        throw std::invalid_argument("AES-256-CBC ciphertext must be a non-empty multiple of 16 bytes");
    }
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        throw std::runtime_error("Could not open the AES provider");
    }
    BCRYPT_KEY_HANDLE handle = nullptr;
    auto* mode = const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(BCRYPT_CHAIN_MODE_CBC));
    const bool ready =
        BCRYPT_SUCCESS(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, mode, sizeof(BCRYPT_CHAIN_MODE_CBC), 0)) &&
        BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0,
                                                  reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
                                                  static_cast<ULONG>(key.size()), 0));
    std::string plaintext(ciphertext.size(), '\0');
    std::string ivCopy(iv);
    ULONG written = 0;
    const bool decrypted =
        ready && BCRYPT_SUCCESS(BCryptDecrypt(handle, reinterpret_cast<PUCHAR>(const_cast<char*>(ciphertext.data())),
                                              static_cast<ULONG>(ciphertext.size()), nullptr,
                                              reinterpret_cast<PUCHAR>(ivCopy.data()), static_cast<ULONG>(ivCopy.size()),
                                              reinterpret_cast<PUCHAR>(plaintext.data()),
                                              static_cast<ULONG>(plaintext.size()), &written, BCRYPT_BLOCK_PADDING));
    if (handle) {
        BCryptDestroyKey(handle);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!decrypted) {
        throw std::runtime_error("AES decryption failed");
    }
    plaintext.resize(written);
    return plaintext;
#else
    throw std::runtime_error("AES decryption is only implemented on Windows");
#endif
}

}
