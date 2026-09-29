#pragma once

/// Selects the base query filter of a client (wire value, sent as one byte).
enum class ClientState : char
{
    NORMAL,
    // Avoid territories of opposite faction
    NORMAL_ALLIANCE,
    NORMAL_HORDE,
    // Allow movement through all type of bad liquids
    DEAD,

    COUNT
};

constexpr bool IsValidClientState(ClientState state) noexcept
{
    return static_cast<unsigned char>(state) < static_cast<unsigned char>(ClientState::COUNT);
}

constexpr int ClientStateCount = static_cast<int>(ClientState::COUNT);
