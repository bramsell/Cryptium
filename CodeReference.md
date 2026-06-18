# CryptCraft Code Reference

**Last Updated:** June 2026  
**Engine:** Unreal Engine 5.7  
**Language:** C++  

---

## Architecture Overview

```
CryptCraft: Voxel-based first-person sandbox game
├── Voxel System          (terrain generation, chunk management, block physics)
├── Game Framework        (game mode, character, player controller)
├── Inventory System      (player items, storage)
├── UI System             (HUD, hotbar, inventory display)
└── Items & Pickups       (block/item drops, collection)
```

---

## Core Voxel System

### **VoxelTypes.h**
**Purpose:** Shared constants, enums, and data structures for the entire voxel system.

**Key Definitions:**
- **Chunk Size:** `CHUNK_SIZE_X = 32`, `CHUNK_SIZE_Y = 32`, `CHUNK_SIZE_Z = 32` — uniform cube chunks
- **Block Scale:** `BLOCK_SIZE = 100.f` UE units (1 block = 1 meter)
- **Generation Modes:**
  - `EWorldGenType::Terrain` — Procedural Perlin noise with horizontal (8 chunks) and vertical (±3 chunks) streaming
  - `EWorldGenType::Flat` — Fixed grid flat plane (no streaming)
- **Block Types:** 32 types including Air, Grass, Dirt, Stone, Sand, Gravel, Ores, Logs, Leaves, Planks, Bedrock, Decorative blocks
- **FBlockDefinition struct:** Static properties per block
  - `Color` — fallback tint
  - `bIsOpaque` — face-culling flag
  - `bIsSolid` — collision flag
  - `TextureTop/Side/Bottom` — texture keys for 6 faces

**Usage:** Imported by all voxel files; provides the type system and constants.

---

### **VoxelWorld.h / VoxelWorld.cpp**
**Purpose:** World-level chunk manager and terrain generator.

**Key Responsibilities:**
1. **Chunk Streaming** — Loads/unloads chunks around player:
   - Horizontal: ±8 chunks in XY (17×17 grid)
   - Vertical: ±3 chunks in Z (7 vertical layers)
   - Total active: ~2000 chunks
2. **Terrain Generation** — Procedural Perlin noise (5-octave fBm)
   - Base height: 50 blocks
   - Height range: 30 blocks (surface varies Z=50..80)
   - Surface layers: 1 Grass, 4 Dirt, rest Stone
3. **Texture Atlas** — Packs all block textures into single runtime atlas
4. **Block API** — Global `GetBlockAt()` / `SetBlockAt()`
5. **Material Injection** — Creates dynamic material and binds atlas

**Designer Properties:**
- `WorldGenType` — Terrain or Flat mode
- `RenderDistance` — XY streaming range (default 8 chunks)
- `FlatExtentChunks` / `FlatSurfaceHeight` — Flat mode parameters
- `ChunkMaterial` — Base material (auto-loads `/Game/Materials/M_VoxelChunk` if null)
- `TextureBasePath` — Auto-discovery folder (default `/Game/Textures/Blocks/`)
- `BlockTextures` — Manual texture overrides (TMap)
- `BlockDefinitions` — Block properties (TMap)

**Key Methods:**
- `BeginPlay()` — Initialize atlas and spawn first chunks
- `Tick()` — Stream chunks every 0.5 seconds
- `UpdateStreamingPosition()` — Manually refresh streaming
- `GetBlockAt()` / `SetBlockAt()` — Query/modify blocks
- `GetPlayerSpawnLocation()` — Return spawn point above terrain
- `GenerateChunkData()` — Procedural terrain fill
- `GenerateFlatChunkData()` — Flat plane fill
- `GenerateSurfaceObjects()` — Boulder/tree placement
- `SampleTerrainHeight()` — Perlin noise elevation
- `BuildTextureAtlas()` — Pack textures into runtime atlas

**Important Implementation Details:**
- **Coordinate Conversion:** Local Z in chunk (0-31) must be converted to world Z before comparing against terrain height
  ```cpp
  const int32 WorldZ = Coord.Z * CHUNK_SIZE_Z + Z;
  if (WorldZ > SurfaceZ) Type = EBlockType::Air;  // CORRECT
  // NOT: if (Z > SurfaceZ)  which is WRONG
  ```
- **Collision:** Sync for Flat/preloaded, Async for streamed terrain
- **Material:** Requires `AtlasTexture` parameter in base material
- **Atlas:** Textures must have `CompressionSettings = TC_EditorIcon` for CPU-readable pixels

---

### **Chunk.h / Chunk.cpp**
**Purpose:** Individual 32×32×32 chunk actor with mesh rendering and collision.

**Key Responsibilities:**
1. **Block Storage** — 32,768 blocks in flat array
2. **Mesh Generation** — Greedy meshing algorithm
3. **Face Culling** — Checks opacity including cross-chunk neighbors
4. **Collision** — ProceduralMesh with sync/async cooking

**Properties:**
- `ChunkCoord` — Grid coordinate (set by VoxelWorld)
- `VoxelWorld` — Owning world reference
- `bUseSyncCollision` — Sync vs async collision cooking

**Key Methods:**
- `Initialize()` — Receive block data and build mesh
- `GetBlock()` / `SetBlock()` — Block access/modification
- `RebuildMesh()` — Regenerate mesh from block data
- `IsBlockOpaque()` — Opacity check (queries world for neighbors)
- `GetBlockWithNeighbors()` — Get block, handling boundaries
- `BuildGreedyMesh()` — Core greedy meshing algorithm

**Greedy Meshing Algorithm:**
- 6 directional passes (±X, ±Y, ±Z)
- Per-axis slicing and scanning
- Horizontal rectangle merging
- Per-quad UV encoding with vertex colors

**UV & Color Encoding:**
- `OutUVs` — Direct atlas-space tile corners
- `OutColors.xy` — Atlas tile offset (0..1)
- `OutColors.z` — Tile size factor
- Material formula: `FinalUV = frac(UV0) * TileSize + TileOffset`

**Important Notes:**
- Cross-chunk face culling enabled via VoxelWorld queries
- Mesh generation is CPU-intensive; batch edits before rebuild
- Collision optional for far chunks

---

## Terrain Generation Details

**Parameters (GenerateChunkData):**
```cpp
const int32 BASE_HEIGHT = 50;    // Minimum surface Z
const int32 HEIGHT_RANGE = 30;   // Maximum extra Z
const int32 DIRT_DEPTH = 4;      // Dirt blocks below grass
```

**Generation Formula (per column):**
1. Sample Perlin noise at (WX, WY) → value 0..1
2. Calculate terrain surface: `SurfaceZ = 50 + RoundToInt(Noise * 30.f)` → 50..80 blocks
3. Fill Z column:
   - `WorldZ > SurfaceZ` → Air
   - `WorldZ == SurfaceZ` → Grass
   - `SurfaceZ - 4 <= WorldZ < SurfaceZ` → Dirt
   - `WorldZ < SurfaceZ - 4` → Stone

**Player Spawn (GetPlayerSpawnLocation):**
```cpp
float TerrainNoise = SampleTerrainHeight(0.f, 0.f);
int32 TerrainSurfaceZ = 50 + RoundToInt(TerrainNoise * 30.f);
int32 SpawnBlockZ = TerrainSurfaceZ + 2;  // 2 blocks above surface
float SpawnZ = (SpawnBlockZ + 1.f) * BLOCK_SIZE + 96.f;  // +96 for capsule
```
Player spawns at origin (0,0) on terrain, 2 blocks above grass surface.

---

## Game Framework

### **CryptCraftGameMode.h / CryptCraftGameMode.cpp**
**Purpose:** Game initialization, world setup, player spawn management.

**Designer Properties:**
- `VoxelWorldClass` — AVoxelWorld subclass
- `WorldGenType` — Mode selection (overrides existing VoxelWorld)
- `SunIntensity` — Directional light brightness
- `SunRotation` — Sun orientation

**Key Methods:**
- `BeginPlay()` — Spawn VoxelWorld, lights; defer player teleport
- `EnsureVoxelWorld()` — Find or spawn VoxelWorld
- `EnsureDirectionalLight()` — Find or spawn sun
- `EnsureSkyLight()` — Find or spawn sky light
- `TeleportPlayersToSurface()` — Move player above terrain

**Important Notes:**
- Player teleport deferred to next tick for initialization
- If VoxelWorld exists, its WorldGenType is overridden
- VoxelWorld is now code-spawned, not placed in editor

---

### **CryptCraftCharacter.h / CryptCraftCharacter.cpp**
**Purpose:** First-person player character with movement, looking, and input.

**Key Components:**
- `FirstPersonMesh` — Skeletal mesh for arms
- `FirstPersonCameraComponent` — Player camera
- Enhanced Input System actions: Jump, Move, Look, MouseLook

**Input Callbacks:**
- `DoMove()` — WASD movement
- `DoAim()` — Camera look (Yaw/Pitch)
- `DoJumpStart()` / `DoJumpEnd()` — Jump state

**Important Notes:**
- Uses UE5 Enhanced Input System
- Supports controller and mouse/keyboard
- Blueprint-overridable

---

### **CryptCraftPlayerController.h / CryptCraftPlayerController.cpp**
**Purpose:** Input mapping and camera management.

**Designer Properties:**
- `DefaultMappingContexts` — Input contexts
- `MobileExcludedMappingContexts` — Platform-specific contexts
- `MobileControlsWidgetClass` — Touch UI
- `bForceTouchControls` — Force touch on desktop

**Key Methods:**
- `BeginPlay()` — Apply input contexts
- `SetupInputComponent()` — Bind input actions
- `ShouldUseTouchControls()` — Determine UI display

---

## Recent Changes & Bug Fixes

### Chunk Size Refactor (June 2026)
- **Old:** 16×16×128 (tall, narrow chunks)
- **New:** 32×32×32 (uniform cubes)
- **Reason:** Balanced vertical and horizontal scaling; supports underground caverns
- **Impact:** All coordinate calculations auto-scale via `CHUNK_SIZE_*` constants

### World Coordinate Bug Fix (June 2026)
- **Problem:** Terrain generation compared local chunk Z (0-31) against world surface Z (50-80), filling everything with stone
- **Root Cause:** Missing coordinate conversion in GenerateChunkData Z-loop
- **Solution:** Convert local Z to world Z before comparison
  ```cpp
  const int32 WorldZ = Coord.Z * CHUNK_SIZE_Z + Z;  // REQUIRED
  if (WorldZ > SurfaceZ) Type = EBlockType::Air;
  ```

### Texture Atlas Auto-Load (June 2026)
- **Problem:** VoxelWorld spawned via code had no ChunkMaterial assigned
- **Solution:** Auto-load `/Game/Materials/M_VoxelChunk` in BeginPlay if null
- **Result:** Textured terrain visible without manual editor setup

### Layers Mode Removal (June 2026)
- **Deleted:** `VoxelGenLayers.h`, `VoxelGenLayers.cpp` (legacy 3D layered generation)
- **Reason:** Simplified to single Terrain mode with vertical streaming
- **Result:** Cleaner codebase, easier maintenance

---

## Quick Start (Code-Only Setup)

1. **GameMode Auto-Spawns VoxelWorld** — No manual placement needed
2. **Terrain Generates on Play** — Chunks stream around player in ±8 XY, ±3 Z
3. **Player Spawns Above Surface** — GetPlayerSpawnLocation calculates height from terrain
4. **Textures Injected at Runtime** — Atlas built from `/Game/Textures/Blocks/` or `BlockTextures` map
5. **Material Auto-Loaded** — ChunkMaterial auto-discovers `/Game/Materials/M_VoxelChunk`

**To customize:**
- Override `BlockDefinitions` map in GameMode or Blueprint
- Override `BlockTextures` map to replace textures
- Set `WorldGenType` in GameMode to switch Terrain ↔ Flat
- Adjust terrain parameters: `BASE_HEIGHT`, `HEIGHT_RANGE`, `DIRT_DEPTH` in GenerateChunkData

---

## Known Limitations

1. **Single Biome** — Terrain uses uniform parameters (no biome variation yet)
2. **No Caves/Tunnels** — Terrain is solid height-map (3D Perlin planned)
3. **No Ores** — Stone is uniform (ore placement deferred)
4. **Surface Objects** — Boulder placement exists but limited
5. **No Saving** — World state not persisted (in-memory only)
6. **No Water/Lava** — Placeholder block types only

---

## Performance Notes

- **2000 Chunks Loaded** — ~17×17×7 active at typical RenderDistance=8, ±3Z
- **Async Collision** — Terrain chunks cook collision on worker thread
- **Greedy Mesh CPU** — ~1-5ms per chunk depending on surface complexity
- **Stream Interval** — 0.5 seconds between update passes
- **Memory:** Each chunk ~320KB (32³×4 bytes) + mesh/collision

---

## File Structure

```
Source/CryptCraft/
├── CryptCraft.h
├── CryptCraft.cpp
├── CryptCraftGameMode.h / .cpp      [Game initialization]
├── CryptCraftCharacter.h / .cpp      [Player character]
├── CryptCraftPlayerController.h / .cpp
├── Voxel/
│   ├── VoxelTypes.h                 [Constants & enums]
│   ├── VoxelWorld.h / .cpp          [World manager]
│   ├── Chunk.h / .cpp               [Chunk actor]
│   └── VoxelGenLayers.h / .cpp      [LEGACY - unused]
└── [Other game systems...]
```

---

## For Future Developers

- **Coordinate System:** X = East, Y = North, Z = Up. Blocks stored as linear array: `index = X + SizeX * (Y + SizeY * Z)`
- **Chunk Coordinates:** Integer grid (0,0,0) = origin. World voxel to chunk: `FloorDivide(WorldVoxel, ChunkSize)`
- **Material UVs:** Non-standard; check `Chunk.cpp:BuildGreedyMesh()` for encoding scheme
- **Streaming:** Player position queried every 0.5s; use `UpdateStreamingPosition()` for immediate refresh
- **Collision:** Both sync and async paths tested; verify physics bodies on new generation modes
