#pragma once

namespace terrain_shadow_checks
{
constexpr int kMap = 0;
constexpr int kOtherMap = 1;
constexpr int kCameraChunk = 500;
constexpr float kWallHeight = 100.0f;
constexpr float kShadowTolerance = 0.01f;
constexpr int kWallVertex = 300;
constexpr float kLightElevationDegrees = 10.0f;

using VertexHeight = std::function<float(int vertexRow, int vertexCol)>;

class SyntheticTerrain final : public TerrainSource
{
public:
    explicit SyntheticTerrain(VertexHeight height) : m_height(std::move(height)) {}

    uintptr_t ChunkIdentity(int chunkRow, int chunkCol) const override
    {
        if (Unloaded(chunkRow, chunkCol))
            return 0;
        return static_cast<uintptr_t>(chunkRow * kTerrainMapChunks + chunkCol + 1) * 16 + m_generation;
    }

    bool ReadChunk(int chunkRow, int chunkCol, TerrainChunkHeights& out) const override
    {
        for (int row = 0; row < kTerrainChunkVertexSide; ++row)
            for (int col = 0; col < kTerrainChunkVertexSide; ++col)
                out.heights[row][col] =
                    m_height(chunkRow * kTerrainChunkCells + row, chunkCol * kTerrainChunkCells + col);
        return true;
    }

    void Unload(int chunkRow, int chunkCol)
    {
        m_unloadedRow = chunkRow;
        m_unloadedCol = chunkCol;
    }

    void Regenerate(VertexHeight height)
    {
        m_height = std::move(height);
        ++m_generation;
    }

private:
    bool Unloaded(int chunkRow, int chunkCol) const
    {
        return chunkRow == m_unloadedRow && chunkCol == m_unloadedCol;
    }

    VertexHeight m_height;
    int m_unloadedRow = -1;
    int m_unloadedCol = -1;
    uintptr_t m_generation = 0;
};

float ChunkCentreYards(int chunk)
{
    return kTerrainMapOriginYards - (chunk + 0.5f) * kTerrainChunkYards;
}

void CameraOverChunk(int chunkRow, int chunkCol, float* camera)
{
    camera[0] = ChunkCentreYards(chunkRow);
    camera[1] = ChunkCentreYards(chunkCol);
    camera[2] = 0.0f;
}

float WindowHeight(const TerrainHeightWindow& window, int vertexRow, int vertexCol)
{
    return window.Heights()[(vertexRow - window.FirstVertexRow()) * kTerrainWindowVertices + vertexCol -
                            window.FirstVertexCol()];
}

float MapVertexHeight(int vertexRow, int vertexCol)
{
    return static_cast<float>(vertexRow % 97) + static_cast<float>(vertexCol % 89) * 0.01f;
}

void CheckWindowPlacesClientChunks()
{
    SyntheticTerrain terrain(MapVertexHeight);
    TerrainHeightWindow window;
    float camera[3];
    CameraOverChunk(kCameraChunk, kCameraChunk + 3, camera);
    Check(window.Track(camera, kMap, terrain) && window.LoadedChunks() == kTerrainWindowChunks * kTerrainWindowChunks,
          "terrain window reads every chunk around the camera");
    Check(window.FirstVertexRow() == (kCameraChunk - kTerrainWindowChunks / 2) * kTerrainChunkCells &&
              window.FirstVertexCol() == (kCameraChunk + 3 - kTerrainWindowChunks / 2) * kTerrainChunkCells,
          "terrain window is centred on the camera's chunk, rows along world -X and columns along world -Y");
    const int vertexRow = kCameraChunk * kTerrainChunkCells + 5;
    const int vertexCol = (kCameraChunk + 3) * kTerrainChunkCells + 2;
    Check(WindowHeight(window, vertexRow, vertexCol) == MapVertexHeight(vertexRow, vertexCol),
          "terrain window stores a chunk's outer vertex at its map vertex");
    const float vertexX = kTerrainMapOriginYards - vertexRow * kTerrainVertexYards;
    const float vertexY = kTerrainMapOriginYards - vertexCol * kTerrainVertexYards;
    Check(std::fabs(TerrainVertexCoordinate(vertexX) - vertexRow) < 1e-2f &&
              std::fabs(TerrainVertexCoordinate(vertexY) - vertexCol) < 1e-2f,
          "terrain vertex coordinates invert the client's chunk vertex positions");
    Check(!window.Track(camera, kMap, terrain), "an unchanged terrain window reports no change");
}

void CheckWindowFollowsLoadingAndMaps()
{
    SyntheticTerrain terrain(MapVertexHeight);
    TerrainHeightWindow window;
    float camera[3];
    CameraOverChunk(kCameraChunk, kCameraChunk, camera);
    window.Track(camera, kMap, terrain);
    terrain.Unload(kCameraChunk, kCameraChunk);
    const int vertex = kCameraChunk * kTerrainChunkCells;
    Check(window.Track(camera, kMap, terrain) && WindowHeight(window, vertex, vertex) == kNoTerrainHeight &&
              window.LoadedChunks() == kTerrainWindowChunks * kTerrainWindowChunks - 1,
          "an unloaded chunk leaves no terrain in the window");
    terrain.Unload(-1, -1);
    window.Track(camera, kMap, terrain);
    const int firstRow = window.FirstVertexRow();
    CameraOverChunk(kCameraChunk + kTerrainWindowRecentreChunks, kCameraChunk, camera);
    window.Track(camera, kMap, terrain);
    Check(window.FirstVertexRow() == firstRow, "terrain window keeps its place while the camera stays near its centre");
    CameraOverChunk(kCameraChunk + kTerrainWindowRecentreChunks + 1, kCameraChunk, camera);
    window.Track(camera, kMap, terrain);
    Check(window.FirstVertexRow() == firstRow + (kTerrainWindowRecentreChunks + 1) * kTerrainChunkCells,
          "terrain window recentres once the camera leaves its centre");
    terrain.Regenerate([](int, int) { return 7.0f; });
    Check(window.Track(camera, kOtherMap, terrain) && WindowHeight(window, vertex, vertex) == 7.0f,
          "a map change rereads the terrain window");
}

void ToLightAt(float towardRows, float towardCols, float* toLight)
{
    const float elevation = kLightElevationDegrees * 3.14159265f / 180.0f;
    const float horizontal = std::sqrt(towardRows * towardRows + towardCols * towardCols);
    toLight[0] = -towardRows / horizontal * std::cos(elevation);
    toLight[1] = -towardCols / horizontal * std::cos(elevation);
    toLight[2] = std::sin(elevation);
}

std::vector<float> Wall(bool alongCols)
{
    std::vector<float> heights(kTerrainWindowVertices * kTerrainWindowVertices, 0.0f);
    for (int n = 0; n < kTerrainWindowVertices; ++n)
        heights[alongCols ? kWallVertex * kTerrainWindowVertices + n : n * kTerrainWindowVertices + kWallVertex] =
            kWallHeight;
    return heights;
}

void CheckWallShadow(bool alongCols, float towardRows, float towardCols, const char* name)
{
    const std::vector<float> heights = Wall(alongCols);
    std::vector<float> shadow(heights.size());
    float toLight[3];
    ToLightAt(towardRows, towardCols, toLight);
    ComputeTerrainShadowHeights(heights.data(), toLight, shadow.data());
    const float tangent = std::tan(kLightElevationDegrees * 3.14159265f / 180.0f);
    const float acrossWall = alongCols ? towardRows : towardCols;
    const float horizontalPerAcross =
        std::sqrt(towardRows * towardRows + towardCols * towardCols) / std::fabs(acrossWall);
    const int away = acrossWall > 0 ? -1 : 1;
    bool behind = true;
    for (int k = 1; k < 40; ++k)
    {
        const int across = kWallVertex + away * k;
        const int index = alongCols ? across * kTerrainWindowVertices + 200 : 200 * kTerrainWindowVertices + across;
        const float expected = kWallHeight - k * kTerrainVertexYards * horizontalPerAcross * tangent;
        behind = behind && std::fabs(shadow[index] - std::max(expected, 0.0f)) <= kShadowTolerance;
    }
    const int lit = alongCols ? (kWallVertex - away * 5) * kTerrainWindowVertices + 200
                              : 200 * kTerrainWindowVertices + kWallVertex - away * 5;
    char label[200];
    std::snprintf(label, sizeof(label), "terrain shadow height behind a wall, %s, falls with the light's slope", name);
    Check(behind, label);
    std::snprintf(label, sizeof(label), "terrain shadow height on the light's side of a wall, %s, stays the ground",
                  name);
    Check(shadow[lit] == 0.0f, label);
}

void CheckShadowHeights()
{
    CheckWallShadow(true, 1.0f, 0.0f, "light along rows");
    CheckWallShadow(true, -1.0f, 0.0f, "light against rows");
    CheckWallShadow(false, 0.0f, 1.0f, "light along columns");
    CheckWallShadow(true, 1.0f, 1.0f, "diagonal light");
    CheckWallShadow(false, -0.5f, -1.0f, "oblique light");
    std::vector<float> heights = Wall(true);
    std::vector<float> shadow(heights.size());
    const float overhead[3] = {0.0f, 0.0f, 1.0f};
    ComputeTerrainShadowHeights(heights.data(), overhead, shadow.data());
    Check(shadow == heights, "a light overhead leaves the terrain its own shadow height");
}

void CheckTerrainShadows()
{
    CheckWindowPlacesClientChunks();
    CheckWindowFollowsLoadingAndMaps();
    CheckShadowHeights();
}
}
