#include "engine_terrain.h"

#include "client_memory.h"
#include "engine.h"
#include "log.h"

#include <cmath>

namespace
{
constexpr uintptr_t kMapAreaTable = 0x00CE48D0;
constexpr int kMapAreaTableSide = 64;
constexpr int kAreaChunkSide = 16;
constexpr uintptr_t kAreaAsyncLoad = 0x70;
constexpr uintptr_t kAreaChunks = 0xBC;
constexpr uintptr_t kChunkTopLeft = 0x7C;
constexpr uintptr_t kChunkHeights = 0x11C;
constexpr int kChunkHeightStride = 17;
constexpr int kChunkHeightCount = 145;
constexpr float kChunkPlacementTolerance = 0.1f;

constexpr uintptr_t kTerrainAreaLookupSite = 0x007BCF9B;
constexpr uintptr_t kTerrainAreaLoadingSite = 0x007BCFAA;
constexpr uintptr_t kTerrainAreaChunkSite = 0x007BCFC1;
constexpr uintptr_t kChunkVertexOriginSite = 0x00790534;
constexpr uintptr_t kChunkVertexHeightSite = 0x0079054C;

uintptr_t ChunkAddress(int chunkRow, int chunkCol)
{
    const int areaIndex = (chunkRow / kAreaChunkSide) * kMapAreaTableSide + chunkCol / kAreaChunkSide;
    const uintptr_t area = Read<uintptr_t>(kMapAreaTable + areaIndex * sizeof(uintptr_t));
    if (!ValidPointer(area, kAreaChunks + kAreaChunkSide * kAreaChunkSide * sizeof(uintptr_t)) ||
        Read<uintptr_t>(area + kAreaAsyncLoad) != 0)
        return 0;
    const int chunkIndex = (chunkRow % kAreaChunkSide) * kAreaChunkSide + chunkCol % kAreaChunkSide;
    const uintptr_t chunk = Read<uintptr_t>(area + kAreaChunks + chunkIndex * sizeof(uintptr_t));
    if (!ValidPointer(chunk, kChunkHeights + sizeof(uintptr_t)))
        return 0;
    return ValidPointer(Read<uintptr_t>(chunk + kChunkHeights), kChunkHeightCount * sizeof(float)) ? chunk : 0;
}

bool ChunkSitsAt(uintptr_t chunk, int chunkRow, int chunkCol, float* origin)
{
    ReadFloats(chunk + kChunkTopLeft, origin, 3);
    return std::fabs(origin[0] - (kTerrainMapOriginYards - chunkRow * kTerrainChunkYards)) <=
               kChunkPlacementTolerance &&
           std::fabs(origin[1] - (kTerrainMapOriginYards - chunkCol * kTerrainChunkYards)) <=
               kChunkPlacementTolerance &&
           std::isfinite(origin[2]);
}

bool ReadChunkUnsafe(int chunkRow, int chunkCol, TerrainChunkHeights& out)
{
    const uintptr_t chunk = ChunkAddress(chunkRow, chunkCol);
    float origin[3];
    if (!chunk || !ChunkSitsAt(chunk, chunkRow, chunkCol, origin))
        return false;
    float heights[kChunkHeightCount];
    ReadFloats(Read<uintptr_t>(chunk + kChunkHeights), heights, kChunkHeightCount);
    for (int row = 0; row < kTerrainChunkVertexSide; ++row)
        for (int col = 0; col < kTerrainChunkVertexSide; ++col)
        {
            const float height = heights[row * kChunkHeightStride + col] + origin[2];
            if (!std::isfinite(height))
                return false;
            out.heights[row][col] = height;
        }
    return true;
}
}

namespace engine
{
bool TerrainContractSupported()
{
    static constexpr unsigned char areaLookup[] = {0x8B, 0x04, 0x85, 0xD0, 0x48, 0xCE, 0x00};
    static constexpr unsigned char areaLoading[] = {0x83, 0x78, 0x70, 0x00};
    static constexpr unsigned char areaChunk[] = {0x8B, 0x84, 0xB0, 0xBC, 0x00, 0x00, 0x00};
    static constexpr unsigned char vertexOrigin[] = {0xD8, 0x41, 0x7C};
    static constexpr unsigned char vertexHeight[] = {0x8B, 0x81, 0x1C, 0x01, 0x00, 0x00};
    if (!IsSupportedClient())
        return false;
    const bool matches = Matches(kTerrainAreaLookupSite, areaLookup) &&
                         Matches(kTerrainAreaLoadingSite, areaLoading) && Matches(kTerrainAreaChunkSite, areaChunk) &&
                         Matches(kChunkVertexOriginSite, vertexOrigin) && Matches(kChunkVertexHeightSite, vertexHeight);
    if (!matches)
        VF_LOG_ERROR("terrain shadows: client bytes differ at the terrain chunk table; terrain does not block the sun");
    return matches;
}

uintptr_t ClientTerrain::ChunkIdentity(int chunkRow, int chunkCol) const
{
    __try
    {
        return ChunkAddress(chunkRow, chunkCol);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool ClientTerrain::ReadChunk(int chunkRow, int chunkCol, TerrainChunkHeights& out) const
{
    __try
    {
        return ReadChunkUnsafe(chunkRow, chunkCol, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
}
