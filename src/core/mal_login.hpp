#pragma once

#include <atomic>
#include <chrono>
#include <string>

namespace ryu {

std::string waitForMalRedirect(unsigned short port, std::chrono::seconds timeout, const std::atomic<bool>& cancelled);

}
