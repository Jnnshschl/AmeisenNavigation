#include "AnTcpServer.hpp"

AnTcpError AnTcpServer::Run() noexcept
{
    WSADATA wsaData{};
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (result != 0)
    {
        DEBUG_ONLY(std::cout << ">> WSAStartup() failed: " << result << std::endl);
        return AnTcpError::Win32WsaStartupFailed;
    }

    addrinfo hints{ 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* addrResult{ nullptr };
    result = getaddrinfo(Ip.c_str(), Port.c_str(), &hints, &addrResult);

    if (result != 0)
    {
        DEBUG_ONLY(std::cout << ">> getaddrinfo() failed: " << result << std::endl);
        WSACleanup();
        return AnTcpError::GetAddrInfoFailed;
    }

    ListenSocket = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);

    if (ListenSocket == INVALID_SOCKET)
    {
        DEBUG_ONLY(std::cout << ">> socket() failed: " << WSAGetLastError() << std::endl);
        freeaddrinfo(addrResult);
        WSACleanup();
        return AnTcpError::SocketCreationFailed;
    }

    result = bind(ListenSocket, addrResult->ai_addr, static_cast<int>(addrResult->ai_addrlen));
    freeaddrinfo(addrResult);

    if (result == SOCKET_ERROR)
    {
        DEBUG_ONLY(std::cout << ">> bind() failed: " << WSAGetLastError() << std::endl);
        SocketCleanup();
        WSACleanup();
        return AnTcpError::SocketBindingFailed;
    }

    if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        DEBUG_ONLY(std::cout << ">> listen() failed: " << WSAGetLastError() << std::endl);
        SocketCleanup();
        WSACleanup();
        return AnTcpError::SocketListeningFailed;
    }


    while (!ShouldExit)
    {
        // accept client and get socket info from it, the socket info contains the ip address
        // and port used to connect to the server
        SOCKADDR_IN clientInfo{ 0 };
        int sockAddrSize = static_cast<int>(sizeof(SOCKADDR_IN));
        SOCKET clientSocket = accept(ListenSocket, reinterpret_cast<sockaddr*>(&clientInfo), &sockAddrSize);

        if (clientSocket == INVALID_SOCKET)
        {
            DEBUG_ONLY(std::cout << ">> accept() failed: " << WSAGetLastError() << std::endl);
            continue;
        }

        // Disable Nagle's algorithm for lower latency on small packets
        int flag = 1;
        setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

        // cleanup old disconnected clients and add the new
        ClientCleanup();
        Clients.push_back(std::make_unique<ClientHandler>(clientSocket, clientInfo, ShouldExit, &Callbacks, OnClientConnected, OnClientDisconnected));
    }

    Clients.clear();

    SocketCleanup();
    WSACleanup();
    return AnTcpError::Success;
}

void ClientHandler::Listen() noexcept
{
    if (OnClientConnected)
    {
        OnClientConnected(this);
    }

    // the total packet size and data ptr offset
    AnTcpSizeType packetSize = 0;
    AnTcpSizeType packetOffset = 0;

    // buffer for the packet
    char packet[sizeof(AnTcpSizeType) + ANTCP_MAX_PACKET_SIZE]{ 0 };

    while (!ShouldExit)
    {
        // Calculate how many bytes we need: either finish reading the size header,
        // or fill the remaining payload.
        const AnTcpSizeType totalNeeded = (packetSize == 0)
            ? static_cast<AnTcpSizeType>(sizeof(AnTcpSizeType))
            : static_cast<AnTcpSizeType>(sizeof(AnTcpSizeType)) + packetSize;
        const auto maxReceiveSize = totalNeeded - packetOffset;

        auto receivedBytes = recv(Socket, packet + packetOffset, maxReceiveSize, 0);

        if (receivedBytes <= 0)
            break;

        packetOffset += static_cast<AnTcpSizeType>(receivedBytes);

        DEBUG_ONLY(std::cout << "[" << Id << "] " << "Received " << std::to_string(receivedBytes) + " bytes" << std::endl);

        // Parse size header once we have enough bytes
        if (packetSize == 0 && packetOffset >= static_cast<AnTcpSizeType>(sizeof(AnTcpSizeType)))
        {
            packetSize = *reinterpret_cast<AnTcpSizeType*>(packet);

            if (packetSize <= 0 || packetSize > ANTCP_MAX_PACKET_SIZE)
            {
                DEBUG_ONLY(std::cout << "[" << Id << "] " << "Packet size invalid (" << std::to_string(packetSize)
                    << "), disconnecting client..." << std::endl);
                break;
            }

            DEBUG_ONLY(std::cout << "[" << Id << "] " << "New Packet: " << std::to_string(packetSize) + " bytes" << std::endl);
        }

        // Check if packet is complete
        if (packetSize > 0 && packetOffset >= static_cast<AnTcpSizeType>(sizeof(AnTcpSizeType)) + packetSize)
        {
            if (!ProcessPacket(packet + sizeof(AnTcpSizeType), packetSize))
                break;

            packetSize = 0;
            packetOffset = 0;
        }
    }

    Disconnect();
}