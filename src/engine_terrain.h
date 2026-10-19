#pragma once

#include "terrain_shadows.h"

namespace engine
{
class ClientTerrain final : public TerrainSource
{
public:
    uintptr_t ChunkIdentity(int chunkRow, int chunkCol) const override;
    bool ReadChunk(int chunkRow, int chunkCol, TerrainChunkHeights& out) const override;
};

bool TerrainContractSupported();
}
