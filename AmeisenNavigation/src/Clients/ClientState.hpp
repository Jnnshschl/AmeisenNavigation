#pragma once

enum class ClientState : char
{
    NORMAL,
    // Avoid territories of opposite faction
    NORMAL_ALLIANCE,
    NORMAL_HORDE,
    // Allow movement through all type of bad liquids
    DEAD,
};
