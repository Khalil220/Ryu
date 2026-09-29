#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ryu {

std::string resolveUrl(std::string_view base, std::string_view reference);
std::string hostOf(std::string_view url);
std::string withHost(std::string_view url, std::string_view host);

bool isMasterPlaylist(std::string_view playlist);
std::vector<std::string> mediaPlaylistUris(std::string_view master, std::string_view baseUrl);
std::vector<std::string> segmentUris(std::string_view media, std::string_view baseUrl);
std::string rewritePlaylist(std::string_view playlist, std::string_view baseUrl,
                            const std::function<std::string(const std::string&)>& map);

}
