#pragma once

#include <array>

#include <DetourNavMeshQuery.h>

#include "../Clients/ClientState.hpp"

/// Provides the default dtQueryFilter per ClientState. Filters are created once and are read-only afterwards,
/// clients that customize area costs get their own copy (see AmeisenNavClient).
class IQueryFilterProvider
{
protected:
    std::array<dtQueryFilter, ClientStateCount> Filters{};

    dtQueryFilter& Filter(ClientState state) noexcept { return Filters[static_cast<size_t>(state)]; }

public:
    virtual ~IQueryFilterProvider() noexcept = default;

    /// Returns the filter for a state, nullptr for invalid states.
    const dtQueryFilter* Get(ClientState state) const noexcept
    {
        return IsValidClientState(state) ? &Filters[static_cast<size_t>(state)] : nullptr;
    }
};
