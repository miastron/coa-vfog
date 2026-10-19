#pragma once

#include <d3d9.h>

#include <cstdint>
#include <vector>

constexpr int kTerrainChunkVertexSide = 9;
constexpr int kTerrainChunkCells = 8;
constexpr int kTerrainMapChunks = 1024;
constexpr int kTerrainWindowChunks = 64;
constexpr int kTerrainWindowVertices = kTerrainWindowChunks * kTerrainChunkCells;
constexpr int kTerrainWindowRecentreChunks = 8;
constexpr float kTerrainMapOriginYards = 17066.666f;
constexpr float kTerrainChunkYards = 33.333332f;
constexpr float kTerrainVertexYards = kTerrainChunkYards / kTerrainChunkCells;
constexpr float kNoTerrainHeight = -100000.0f;

struct TerrainChunkHeights
{
    float heights[kTerrainChunkVertexSide][kTerrainChunkVertexSide];
};

class TerrainSource
{
public:
    virtual uintptr_t ChunkIdentity(int chunkRow, int chunkCol) const = 0;
    virtual bool ReadChunk(int chunkRow, int chunkCol, TerrainChunkHeights& out) const = 0;

protected:
    ~TerrainSource() = default;
};

int TerrainChunkIndex(float worldYards);
float TerrainVertexCoordinate(float worldYards);

class TerrainHeightWindow
{
public:
    TerrainHeightWindow();

    bool Track(const float* cameraWorld, int mapId, const TerrainSource& source);
    void Reset();
    const float* Heights() const { return m_heights.data(); }
    int FirstVertexRow() const { return m_firstChunkRow * kTerrainChunkCells; }
    int FirstVertexCol() const { return m_firstChunkCol * kTerrainChunkCells; }
    int LoadedChunks() const { return m_loadedChunks; }

private:
    bool Recentre(int cameraChunkRow, int cameraChunkCol, int mapId);
    bool RefreshChunk(int windowRow, int windowCol, const TerrainSource& source);
    void WriteChunk(int windowRow, int windowCol, const TerrainChunkHeights* chunk);

    std::vector<float> m_heights;
    std::vector<uintptr_t> m_identities;
    std::vector<bool> m_loaded;
    int m_firstChunkRow = 0;
    int m_firstChunkCol = 0;
    int m_mapId = -1;
    int m_loadedChunks = 0;
    bool m_placed = false;
};

void ComputeTerrainShadowHeights(const float* heights, const float* toLight, float* shadowHeights);

class TerrainShadowMap
{
public:
    ~TerrainShadowMap();

    bool Update(IDirect3DDevice9* dev, const float* cameraWorld, int mapId, const float* toLight,
                const TerrainSource& source);
    void ReleaseTexture();
    IDirect3DTexture9* Texture() const { return m_texture; }
    const TerrainHeightWindow& Window() const { return m_window; }

private:
    bool LightMoved(const float* toLight) const;
    bool Upload();

    TerrainHeightWindow m_window;
    std::vector<float> m_shadowHeights;
    IDirect3DTexture9* m_texture = nullptr;
    float m_toLight[3] = {};
    bool m_current = false;
    bool m_creationFailed = false;
};
