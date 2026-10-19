#include "terrain_shadows.h"

#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr int kWindowChunkSlots = kTerrainWindowChunks * kTerrainWindowChunks;
constexpr int kWindowVertexCount = kTerrainWindowVertices * kTerrainWindowVertices;
constexpr float kMinHorizontalLight = 1e-6f;
constexpr float kLightRecomputeCosine = 0.999998f;

template <typename T>
void SafeRelease(T*& p)
{
    if (p)
    {
        p->Release();
        p = nullptr;
    }
}

bool InMap(int chunk)
{
    return chunk >= 0 && chunk < kTerrainMapChunks;
}

float UpstreamShadowHeight(const float* shadowHeights, int line, int majorStride, int minorStride, float minor)
{
    const float cell = std::floor(minor);
    const int first = static_cast<int>(cell);
    const float fraction = minor - cell;
    if (first < 0 || first >= kTerrainWindowVertices)
        return kNoTerrainHeight;
    const float lower = shadowHeights[line * majorStride + first * minorStride];
    if (first + 1 >= kTerrainWindowVertices)
        return fraction > 0.0f ? kNoTerrainHeight : lower;
    const float upper = shadowHeights[line * majorStride + (first + 1) * minorStride];
    return lower + (upper - lower) * fraction;
}
}

int TerrainChunkIndex(float worldYards)
{
    return static_cast<int>(std::floor((kTerrainMapOriginYards - worldYards) / kTerrainChunkYards));
}

float TerrainVertexCoordinate(float worldYards)
{
    return (kTerrainMapOriginYards - worldYards) / kTerrainVertexYards;
}

TerrainHeightWindow::TerrainHeightWindow()
    : m_heights(kWindowVertexCount, kNoTerrainHeight), m_identities(kWindowChunkSlots, 0),
      m_loaded(kWindowChunkSlots, false)
{
}

void TerrainHeightWindow::Reset()
{
    std::fill(m_heights.begin(), m_heights.end(), kNoTerrainHeight);
    std::fill(m_identities.begin(), m_identities.end(), 0);
    std::fill(m_loaded.begin(), m_loaded.end(), false);
    m_loadedChunks = 0;
    m_placed = false;
    m_mapId = -1;
}

bool TerrainHeightWindow::Recentre(int cameraChunkRow, int cameraChunkCol, int mapId)
{
    const int half = kTerrainWindowChunks / 2;
    const bool keeps = m_placed && mapId == m_mapId &&
                       std::abs(cameraChunkRow - (m_firstChunkRow + half)) <= kTerrainWindowRecentreChunks &&
                       std::abs(cameraChunkCol - (m_firstChunkCol + half)) <= kTerrainWindowRecentreChunks;
    if (keeps)
        return false;
    const bool hadTerrain = m_loadedChunks > 0;
    Reset();
    m_firstChunkRow = cameraChunkRow - half;
    m_firstChunkCol = cameraChunkCol - half;
    m_mapId = mapId;
    m_placed = true;
    return hadTerrain;
}

void TerrainHeightWindow::WriteChunk(int windowRow, int windowCol, const TerrainChunkHeights* chunk)
{
    for (int row = 0; row < kTerrainChunkCells; ++row)
    {
        float* line = &m_heights[(windowRow * kTerrainChunkCells + row) * kTerrainWindowVertices +
                                 windowCol * kTerrainChunkCells];
        for (int col = 0; col < kTerrainChunkCells; ++col)
            line[col] = chunk ? chunk->heights[row][col] : kNoTerrainHeight;
    }
}

bool TerrainHeightWindow::RefreshChunk(int windowRow, int windowCol, const TerrainSource& source)
{
    const int chunkRow = m_firstChunkRow + windowRow;
    const int chunkCol = m_firstChunkCol + windowCol;
    const int slot = windowRow * kTerrainWindowChunks + windowCol;
    const uintptr_t identity = InMap(chunkRow) && InMap(chunkCol) ? source.ChunkIdentity(chunkRow, chunkCol) : 0;
    if (identity == m_identities[slot])
        return false;
    TerrainChunkHeights chunk;
    const bool loaded = identity != 0 && source.ReadChunk(chunkRow, chunkCol, chunk);
    const bool wasLoaded = m_loaded[slot];
    m_identities[slot] = identity;
    m_loaded[slot] = loaded;
    m_loadedChunks += (loaded ? 1 : 0) - (wasLoaded ? 1 : 0);
    if (!loaded && !wasLoaded)
        return false;
    WriteChunk(windowRow, windowCol, loaded ? &chunk : nullptr);
    return true;
}

bool TerrainHeightWindow::Track(const float* cameraWorld, int mapId, const TerrainSource& source)
{
    bool changed = Recentre(TerrainChunkIndex(cameraWorld[0]), TerrainChunkIndex(cameraWorld[1]), mapId);
    for (int windowRow = 0; windowRow < kTerrainWindowChunks; ++windowRow)
        for (int windowCol = 0; windowCol < kTerrainWindowChunks; ++windowCol)
            changed |= RefreshChunk(windowRow, windowCol, source);
    return changed;
}

void ComputeTerrainShadowHeights(const float* heights, const float* toLight, float* shadowHeights)
{
    const float towardRows = -toLight[0];
    const float towardCols = -toLight[1];
    const float horizontal = std::sqrt(towardRows * towardRows + towardCols * towardCols);
    if (horizontal < kMinHorizontalLight)
    {
        std::memcpy(shadowHeights, heights, sizeof(float) * kWindowVertexCount);
        return;
    }
    const bool rowsMajor = std::fabs(towardRows) >= std::fabs(towardCols);
    const float major = rowsMajor ? towardRows : towardCols;
    const float slope = (rowsMajor ? towardCols : towardRows) / std::fabs(major);
    const int majorStride = rowsMajor ? kTerrainWindowVertices : 1;
    const int minorStride = rowsMajor ? 1 : kTerrainWindowVertices;
    const int step = major > 0.0f ? 1 : -1;
    const float drop = kTerrainVertexYards * std::sqrt(1.0f + slope * slope) * toLight[2] / horizontal;
    for (int k = 0; k < kTerrainWindowVertices; ++k)
    {
        const int line = step > 0 ? kTerrainWindowVertices - 1 - k : k;
        const int upstream = line + step;
        const bool hasUpstream = upstream >= 0 && upstream < kTerrainWindowVertices;
        for (int n = 0; n < kTerrainWindowVertices; ++n)
        {
            const int index = line * majorStride + n * minorStride;
            float height = heights[index];
            if (hasUpstream)
                height = std::max(height, UpstreamShadowHeight(shadowHeights, upstream, majorStride, minorStride,
                                                               n + slope) -
                                              drop);
            shadowHeights[index] = height;
        }
    }
}

TerrainShadowMap::~TerrainShadowMap()
{
    ReleaseTexture();
}

void TerrainShadowMap::ReleaseTexture()
{
    SafeRelease(m_texture);
    m_current = false;
}

bool TerrainShadowMap::LightMoved(const float* toLight) const
{
    return toLight[0] * m_toLight[0] + toLight[1] * m_toLight[1] + toLight[2] * m_toLight[2] <
           kLightRecomputeCosine;
}

bool TerrainShadowMap::Upload()
{
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_texture->LockRect(0, &locked, nullptr, 0)))
        return false;
    for (int row = 0; row < kTerrainWindowVertices; ++row)
        std::memcpy(static_cast<unsigned char*>(locked.pBits) + row * locked.Pitch,
                    &m_shadowHeights[row * kTerrainWindowVertices], sizeof(float) * kTerrainWindowVertices);
    return SUCCEEDED(m_texture->UnlockRect(0));
}

bool TerrainShadowMap::Update(IDirect3DDevice9* dev, const float* cameraWorld, int mapId, const float* toLight,
                              const TerrainSource& source)
{
    const bool terrainChanged = m_window.Track(cameraWorld, mapId, source);
    if (m_window.LoadedChunks() == 0)
        return false;
    if (!m_texture && !m_creationFailed)
    {
        if (FAILED(dev->CreateTexture(kTerrainWindowVertices, kTerrainWindowVertices, 1, 0, D3DFMT_R32F,
                                      D3DPOOL_MANAGED, &m_texture, nullptr)))
        {
            m_texture = nullptr;
            m_creationFailed = true;
            VF_LOG_ERROR("terrain shadows: %dx%d R32F texture unavailable; terrain does not block the sun",
                         kTerrainWindowVertices, kTerrainWindowVertices);
        }
        m_current = false;
    }
    if (!m_texture)
        return false;
    if (m_current && !terrainChanged && !LightMoved(toLight))
        return true;
    m_shadowHeights.resize(static_cast<size_t>(kWindowVertexCount));
    ComputeTerrainShadowHeights(m_window.Heights(), toLight, m_shadowHeights.data());
    std::memcpy(m_toLight, toLight, sizeof(m_toLight));
    m_current = Upload();
    return m_current;
}
