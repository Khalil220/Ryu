#pragma once

#include "http.hpp"
#include "playlist_server.hpp"
#include "provider.hpp"

#include <string>
#include <vector>

namespace ryu {

struct RepairReport {
    Stream stream;
    std::vector<std::string> deadHosts;
    std::string replacementHost;
};

RepairReport repairStreamHosts(HttpClient& http, PlaylistServer& server, const Stream& stream);

}
