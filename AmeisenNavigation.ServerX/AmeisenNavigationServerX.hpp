#ifndef _H_AmeisenNavigationServerX
#define _H_AmeisenNavigationServerX

/*
* This is going to be the cross platform navigation server for the AmeisenBotX.
* At the moment the following platform are supported:
*  - Windows
*/

#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#include <iostream>
#include <thread>
#include <vector>

#pragma comment (lib, "Ws2_32.lib")

constexpr auto DEFAULT_BUFFERLENGTH = 1024;
constexpr auto DEFAULT_ADDRESS = "0.0.0.0";
constexpr auto DEFAULT_PORT = "4711";

constexpr auto VERSION = "0.1.0.0";

bool mShouldExit = false;

void SetupServer();

#ifdef _WIN32

void HandleClient(SOCKET socket, sockaddr address);

#endif

#endif // !_H_AmeisenNavigationServerX