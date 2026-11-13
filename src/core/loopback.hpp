#pragma once

#include <string>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ryu {

#ifdef _WIN32
using SocketHandle = SOCKET;
#else
using SocketHandle = int;
inline constexpr SocketHandle INVALID_SOCKET = -1;
#endif

bool startNetworking();
void stopNetworking();
void closeSocket(SocketHandle socket);
SocketHandle listenOnLoopback(int family, unsigned short port);
std::string requestTarget(SocketHandle client);
void sendAll(SocketHandle client, std::string_view data);

}
