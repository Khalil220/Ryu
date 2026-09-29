#pragma once

#include <string>
#include <string_view>

namespace ryu {

std::string base64Decode(std::string_view input);
std::string xorCipher(std::string_view data, std::string_view key);
std::string urlEncode(std::string_view input);
std::string aes256CbcDecrypt(std::string_view ciphertext, std::string_view key, std::string_view iv);

}
