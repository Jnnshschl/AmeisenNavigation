#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

#include "../../../AmeisenNavigation.Pack/src/DetourUtils.hpp"
#include "../NavSources/IQueryFilterProvider.hpp"
#include "../Utils/Path.hpp"
#include "ClientState.hpp"

/// Upper bound for area costs: big enough to effectively forbid an area, small enough that path costs stay finite.
constexpr float MAX_AREA_COST = 1e6f;

/// Area cost override sent by a client.
struct AreaCost
{
    unsigned char areaId;
    float cost;
};

/// Per-connection navigation state: the active query filter and reusable buffers. dtNavMeshQuery objects are
/// not per client, requests lease them from the map's NavMeshQueryPool.
///
/// A client is not thread-safe, it must only be used by one thread at a time (the server processes each
/// connection's requests sequentially on that connection's thread).
class AmeisenNavClient
{
    size_t Id;
    ClientState State;
    const IQueryFilterProvider* FilterProvider;

    // Per-client area cost overrides, empty = use the provider's default filter.
    std::optional<dtQueryFilter> CustomFilter;

    // Reusable buffers for path calculation.
    int PolyPathBufferSize;
    int StraightPathRefsSize;
    std::unique_ptr<dtPolyRef[]> PolyPathBuffer;
    std::unique_ptr<dtPolyRef[]> StraightPathRefs;
    Path PrimaryPath;
    Path SecondaryPath;

public:
    AmeisenNavClient(size_t id, const IQueryFilterProvider* filterProvider, int polyPathBufferSize = 512,
                     int pointPathBufferSize = 256)
        : Id(id),
          State(ClientState::NORMAL),
          FilterProvider(filterProvider),
          CustomFilter(),
          PolyPathBufferSize(std::max(polyPathBufferSize, 1)),
          StraightPathRefsSize(std::max(pointPathBufferSize, 1)),
          PolyPathBuffer(std::make_unique<dtPolyRef[]>(static_cast<size_t>(PolyPathBufferSize))),
          StraightPathRefs(std::make_unique<dtPolyRef[]>(static_cast<size_t>(StraightPathRefsSize))),
          PrimaryPath(pointPathBufferSize),
          SecondaryPath(pointPathBufferSize)
    {
    }

    AmeisenNavClient(const AmeisenNavClient&) = delete;
    AmeisenNavClient& operator=(const AmeisenNavClient&) = delete;

    size_t GetId() const noexcept { return Id; }
    ClientState GetClientState() const noexcept { return State; }
    bool HasCustomFilter() const noexcept { return CustomFilter.has_value(); }

    /// The filter used for all queries of this client.
    const dtQueryFilter* QueryFilter() const noexcept
    {
        return CustomFilter ? &*CustomFilter : FilterProvider->Get(State);
    }

    int GetPolyPathBufferSize() const noexcept { return PolyPathBufferSize; }
    dtPolyRef* GetPolyPathBuffer() noexcept { return PolyPathBuffer.get(); }

    /// Poly refs of the straight path corners, sized like the path buffers.
    dtPolyRef* GetStraightPathRefBuffer() noexcept { return StraightPathRefs.get(); }
    int GetStraightPathRefBufferSize() const noexcept { return StraightPathRefsSize; }

    /// Two reusable path buffers (result + scratch for smoothing/validation).
    Path& GetPathBuffer() noexcept { return PrimaryPath; }
    Path& GetScratchPathBuffer() noexcept { return SecondaryPath; }

    /// Select the base filter by state and apply area cost overrides on top of it.
    /// Returns false (and changes nothing) for invalid states, area ids or costs.
    bool ConfigureQueryFilter(ClientState state, std::span<const AreaCost> costs) noexcept
    {
        if (!IsValidClientState(state))
        {
            return false;
        }

        const dtQueryFilter* base = FilterProvider->Get(state);

        if (!base)
        {
            return false;
        }

        for (const auto& [areaId, cost] : costs)
        {
            // dtQueryFilter stores DT_MAX_AREAS costs, anything else would write out of bounds.
            // Non-positive/infinite costs would break the A* search.
            if (areaId >= DT_MAX_AREAS || !std::isfinite(cost) || cost <= 0.0f || cost > MAX_AREA_COST)
            {
                return false;
            }
        }

        State = state;

        if (costs.empty())
        {
            CustomFilter.reset();
            return true;
        }

        CustomFilter.emplace(*base);

        for (const auto& [areaId, cost] : costs)
        {
            CustomFilter->setAreaCost(areaId, cost);
        }

        return true;
    }

    /// Drop all overrides and go back to the provider's default filter for the current state.
    void ResetQueryFilter() noexcept { CustomFilter.reset(); }
};
