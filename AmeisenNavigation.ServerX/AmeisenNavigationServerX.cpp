#include "AmeisenNavigationServerX.hpp"

int main()
{
    std::cout << "    ___                   _                 _   __           " << std::endl
        << "   /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __" << std::endl
        << "  / /| | / __ `__ \\/ _ \\/ / ___/ _ \\/ __ \\/  |/ / __ `/ | / /" << std::endl
        << " / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / " << std::endl
        << "/_/  |_/_/ /_/ /_/\\___/_/____/\\___/_/ /_/_/ |_/\\__,_/ |___/  " << std::endl
        << "                                        Server " << VERSION << std::endl << std::endl;

    SetupServer();
}

#ifdef _WIN32

int __stdcall ExitCleanup(unsigned long signal)
{
    if (signal == CTRL_C_EVENT)
    {
        std::cout << ">> Stopping server..." << std::endl;
        mShouldExit = true;
    }

    return 1;
}

void SetupServer()
{
    if (!SetConsoleCtrlHandler(ExitCleanup, 1))
    {
        std::cout << ">> Unable to SetConsoleCtrlHandler, exiting..." << std::endl;
        return;
    }

    WSADATA wsaData;
    int returnCode = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (returnCode == 0)
    {
        struct addrinfo hints;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        struct addrinfo* result = nullptr;
        returnCode = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);

        if (returnCode == 0)
        {
            SOCKET listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

            if (listenSocket != INVALID_SOCKET)
            {
                returnCode = bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);

                if (returnCode != SOCKET_ERROR)
                {
                    returnCode = listen(listenSocket, SOMAXCONN);

                    if (returnCode != SOCKET_ERROR)
                    {
                        std::cout << ">> Server Listening on: " << DEFAULT_ADDRESS << ":" << DEFAULT_PORT << std::endl;

                        std::vector<std::thread> userThreads;

                        while (!mShouldExit)
                        {
                            sockaddr address;
                            SOCKET clientSocket = accept(listenSocket, &address, nullptr);

                            std::cout << ">> New client: " << address.sa_data << std::endl;

                            if (clientSocket != INVALID_SOCKET)
                            {
                                userThreads.push_back(std::thread(HandleClient, clientSocket, address));
                            }
                            else
                            {
                                std::cout << ">> Accepting client failed with return code: " << WSAGetLastError() << std::endl;
                            }
                        }

                        mShouldExit = true;

                        for (int i = 0; i < userThreads.size(); ++i)
                        {
                            userThreads[i].join();
                        }
                    }
                    else
                    {
                        std::cout << ">> Accepting client failed with return code: " << WSAGetLastError() << std::endl;
                    }

                    closesocket(listenSocket);
                }
                else
                {
                    std::cout << ">> Listening on socket failed: " << WSAGetLastError() << std::endl;
                }

                closesocket(listenSocket);
            }
            else
            {
                std::cout << ">> Binding socket failed: " << WSAGetLastError() << std::endl;
            }

            freeaddrinfo(result);
        }
        else
        {
            std::cout << ">> getaddrinfo failed: " << WSAGetLastError() << std::endl;
        }

        WSACleanup();
    }
    else
    {
        std::cout << ">> WSAStartup failed: " << returnCode << std::endl;
        return;
    }
}

void HandleClient(SOCKET socket, sockaddr address)
{
    char receiveBuffer[DEFAULT_BUFFERLENGTH];

    while (!mShouldExit)
    {
        int n = recv(socket, receiveBuffer, DEFAULT_BUFFERLENGTH, 0);

        if (n == SOCKET_ERROR)
        {
            break;
        }

        std::cout << "-> 0x" << std::hex << receiveBuffer << std::dec << std::endl;
    }

    closesocket(socket);
}

#endif