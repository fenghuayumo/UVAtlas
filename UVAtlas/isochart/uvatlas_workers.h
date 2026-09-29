#pragma once

#include <cstdint>

namespace DirectX
{
    // Thread-local engine queue width. Defined in UVAtlas.cpp.
    uint32_t UVAtlasEngineWorkerCount() noexcept;
    void UVAtlasEngineWorkerCountStore(uint32_t workerCount) noexcept;
}
