# CryptCraft Code Reference

**Last Updated:** March 2026  
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
- `CHUNK_SIZE_X = 16`, `CHUNK_SIZE_Y = 16`, `CHUNK_SIZE_Z = 128` — voxel dimensions per chunk
- `BLOCK_SIZE = 100.f` UE units — size of each block (1 m = 100 cm)
- `EWorldGenType` enum: `Terrain` (procedural + streaming) vs `Flat` (fixed grid)
- `EBlockType` enum: 32 block types (Grass, Stone, Ore types, Logs, Water, Lava, etc.)
- `FBlockDefinition` struct: Per-block static properties
  - `Color` — fallback tint when texture unavailable
  - `bIsOpaque` — face-culling optimization
  - `bIsSolid` — collision flag
  - `TextureTop/Side/Bottom` — texture keys for 6-faced quad assignment

**Usage:** Imported by all voxel-related files; provides the type system.

---

### **VoxelWorld.h / VoxelWorld.cpp**
**Purpose:** World-level voxel manager — handles chunk streaming, terrain generation, atlas building, and the global block query API.

**Key Responsibilities:**
1. **Chunk Streaming:** Loads/unloads chunks in a streaming volume around a tracked actor (typically the player)
2. **Terrain Generation:** Procedural Perlin-noise-based height-map generation (5-octave fBm)
3. **Flat Worlds:** Optional fixed-grid spawning for testing
4. **Texture Atlas:** Packs all 16×16 block textures into a single runtime UTexture2D; assigns tile indices
5. **Block API:** `GetBlockAt()` / `SetBlockAt()` for runtime block manipulation
6. **Material Injection:** Creates `RuntimeChunkMaterial` and binds the atlas texture to all chunk meshes

**Designer-Facing Properties:**
- `WorldGenType` — selects Terrain or Flat generation mode
- `RenderDistance` — how many chunks to stream around player (default 8)
- `FlatExtentChunks` / `FlatSurfaceHeight` — size and height of flat worlds
- `ChunkMaterial` — base material to apply to all chunks (must have `AtlasTexture` parameter)
- `TextureBasePath` — where to auto-discover block textures (default `/Game/Textures/Blocks/`)
- `BlockTextures` — manual texture overrides (TMap of name → UTexture2D)
- `BlockDefinitions` — static properties per block type (color, opacity, solidity, texture keys)

**Key Methods:**
- `BeginPlay()` — initializes atlas, loads default block definitions, spawns first chunks
- `Tick()` — streams chunks based on player position (0.5 s interval)
- `UpdateStreamingPosition()` — manual refresh (called by player controller)
- `GetBlockAt()` / `SetBlockAt()` — query/modify blocks at world-voxel coordinates
- `GetTileIndex()` — texture name → atlas tile index (for rendering)
- `GetPlayerSpawnLocation()` — returns spawn point above surface
- `BuildTextureAtlas()` — packs BlockTextures into RuntimeAtlas (runs at BeginPlay)
- `GenerateChunkData()` — fills chunk with procedural terrain
- `GenerateFlatChunkData()` — fills chunk with flat plane
- `GenerateSurfaceObjects()` — second pass for boulders, trees, etc.
- `SampleTerrainHeight()` — Perlin noise for elevation (0..1 range)

**Private Data:**
- `LoadedChunks` — TMap of chunk coords → AChunk actors
- `RuntimeAtlas` — packed UTexture2D (16-bit or higher, BGRA8)
- `RuntimeChunkMaterial` — UMaterialInstanceDynamic with injected atlas
- `TextureToTileIndex` — texture name → atlas slot mapping
- `ComputedAtlasCols` — width/height of atlas grid in tiles

**Important Notes:**
- Chunks only stream in Terrain mode; Flat mode pre-spawns the entire grid
- Atlas building requires all block textures to have `CompressionSettings = TC_EditorIcon` so raw BGRA8 pixels are accessible via `BulkData.Lock()`
- Default block definitions are fallback; override via Blueprint or `BlockDefinitions` map

---

### **Chunk.h / Chunk.cpp**
**Purpose:** Represents a single 16×16×128 voxel cube; owns a ProceduralMeshComponent and builds/updates its mesh via greedy meshing.

**Key Responsibilities:**
1. **Block Storage:** Flat array of 32768 blocks (16 × 16 × 128)
2. **Mesh Generation:** Greedy-mesh algorithm that merges same-type adjacent faces into large quads
3. **Face Culling:** Checks opacity of neighbors (even across chunk boundaries, via VoxelWorld query)
4. **UV Encoding:** Emits UVs as direct atlas-space tile corners per quad; vertex colors encode atlas offset
5. **Collision:** Can use sync or async collision cooking (faster for streamed chunks)

**Designer-Facing Properties:**
- `ChunkCoord` — FIntVector grid coordinate (read-only, set by VoxelWorld)
- `VoxelWorld` — reference to owning world (set by VoxelWorld::LoadChunk)
- `bUseSyncCollision` — if true, cook collision synchronously on mesh rebuild (false for async)

**Key Methods:**
- `Initialize()` — receive generated block data and build initial mesh
- `GetBlock()` / `SetBlock()` — access/modify block at local voxel coords
- `RebuildMesh()` — regenerate ProceduralMesh from current block data
- `IsBlockOpaque()` — check if a block occludes adjacent faces (queries world for out-of-bounds)
- `GetBlockWithNeighbors()` — get block type, querying world for out-of-bounds
- `BuildGreedyMesh()` — core greedy meshing: 6 face passes, per-axis slicing, face merging

**Greedy Meshing Algorithm:**
For each of 6 face directions (±X, ±Y, ±Z):
1. Slice the chunk along that axis
2. For each slice, scan for visible blocks (opaque block + air neighbor)
3. Merge horizontally adjacent blocks into rectangles
4. Emit 2 triangles per quad with merged UVs, vertex colors, and normals

**UV & Color Encoding:**
- `OutUVs[i]` = direct atlas-space tile corner (e.g., `(0.1, 0.2)` = top-left of tile at atlas slot 5)
- `OutColors[i].xy` = atlas tile offset (0..1 range for position on atlas)
- `OutColors[i].z` = tile size factor (1 / ComputedAtlasCols, used in material)
- Material formula: `FinalUV = frac(UV0) * TileSize + TileOffset`

**Private Data:**
- `Blocks` — flat TArray of EBlockType (32768 elements)
- `ProceduralMesh` — UProceduralMeshComponent owned by the actor

**Important Notes:**
- Chunks block out-of-bounds voxels as Air, enabling seamless inter-chunk face culling
- Greedy meshing runs every time `SetBlock()` is called → CPU-intensive; batch edits before calling
- Collision is optional; disable for far chunks to save memory

---

## Game Framework

### **CryptCraftGameMode.h / CryptCraftGameMode.cpp**
**Purpose:** Manages game initialization and world setup (spawns VoxelWorld and lighting).

**Designer-Facing Properties:**
- `VoxelWorldClass` — which AVoxelWorld subclass to spawn (allows blueprint override)
- `WorldGenType` — passed to spawned VoxelWorld (Terrain or Flat)
- `SunIntensity` — lux value of auto-spawned directional light (default 10.0)
- `SunRotation` — rotation of the sun (default -45° pitch, 45° yaw)

**Key Methods:**
- `BeginPlay()` — ensures VoxelWorld, DirectionalLight, and SkyLight exist; defers player spawn until surface is ready
- `EnsureVoxelWorld()` — finds existing or spawns new AVoxelWorld; applies WorldGenType
- `EnsureDirectionalLight()` — finds existing or spawns new ADirectionalLight
- `EnsureSkyLight()` — finds existing or spawns new ASkyLight
- `TeleportPlayersToSurface()` — deferred callback that moves spawned players above the voxel surface

**Important Notes:**
- The GameMode defers player teleport to the next tick so the pawn finishes initialization
- If a VoxelWorld already exists in the level, its WorldGenType is overridden by the GameMode setting

---

### **CryptCraftCharacter.h / CryptCraftCharacter.cpp**
**Purpose:** First-person player character with camera, skeletal mesh (arms), and input handling.

**Key Components:**
- `FirstPersonMesh` — USkeletalMeshComponent for player arms (first-person view only)
- `FirstPersonCameraComponent` — UCameraComponent attached to head
- `JumpAction`, `MoveAction`, `LookAction`, `MouseLookAction` — Enhanced Input System actions

**Input Callbacks:**
- `MoveInput()` — handles WASD movement from MoveAction
- `LookInput()` — handles gamepad stick look
- `DoAim()` — aiming/looking (Yaw/Pitch) from both gamepad and mouse
- `DoMove()` — locomotion from both input types
- `DoJumpStart()` / `DoJumpEnd()` — jump state handling

**Important Notes:**
- Uses Enhanced Input System (UE5 standard)
- Supports both controller and mouse/keyboard
- Blueprint-overridable (abstract UCLASS)

---

### **CryptCraftPlayerController.h / CryptCraftPlayerController.cpp**
**Purpose:** Manages input mapping, camera settings, and optional touch controls.

**Designer-Facing Properties:**
- `DefaultMappingContexts` — IMCs applied on all platforms
- `MobileExcludedMappingContexts` — IMCs for non-mobile platforms
- `MobileControlsWidgetClass` — UMG widget for touch control overlay
- `bForceTouchControls` — force touch even on desktop (for testing)

**Key Methods:**
- `BeginPlay()` — initializes input mapping contexts
- `SetupInputComponent()` — binds input actions to character callbacks
- `ShouldUseTouchControls()` — determines if UMG touch controls should be shown

**Important Notes:**
- Integrates with Enhanced Input System
- Conditionally spawns mobile UI
- Overrides player camera manager class (see `CryptCraftCameraManager`)

---

### **CryptCraftCameraManager.h / CryptCraftCameraManager.cpp**
**Purpose:** Custom camera behavior (can override view distance, shake, FOV, etc.).

**Note:** Lightweight extension point; most behavior is handled by the character's camera component.

---

### **VoxelHUD.h / VoxelHUD.cpp**
**Purpose:** Simple debug HUD that displays player position in world-voxel coordinates.

**Key Method:**
- `DrawHUD()` — renders text overlay showing chunk coord and local voxel position

**Usage:** Automatically created by the GameMode; provides live feedback for debugging terrain generation.

---

## Inventory & Items System

### **InventoryComponent.h / InventoryComponent.cpp**
**Purpose:** Tracks player inventory (held items, storage slots).

**Key Responsibility:**
- Manages item storage and retrieval
- Signals when inventory changes (for UI updates)

**Note:** Attached to the player character.

---

### **ItemData.h**
**Purpose:** Data structure for item definitions (name, icon, quantity, stack limit, etc.).

---

### **ItemPickup.h / ItemPickup.cpp**
**Purpose:** World actor that represents a dropped item (physical collectible).

**Behavior:**
- Spawned when a block is mined or an item is dropped
- Consumed when player collides with it (added to inventory)

---

## UI System

### **HotbarWidget.h / HotbarWidget.cpp**
**Purpose:** Displays the player's quick-access slot bar (typically 9 slots).

**Interaction:**
- Number keys (1–9) to select slot
- Reflects current equipped item visually

---

### **InventoryWidget.h / InventoryWidget.cpp**
**Purpose:** Full inventory screen (open with Tab or equivalent).

**Features:**
- Grid display of inventory slots
- Drag-and-drop item management
- Item details / tooltip

---

### **InventorySlotWidget.h / InventorySlotWidget.cpp**
**Purpose:** Single inventory slot UI element.

**Responsibilities:**
- Displays item icon and quantity
- Handles drag-and-drop interactions

---

### **PickupLabelWidget.h / PickupLabelWidget.cpp**
**Purpose:** Floating 3D label above nearby item pickups.

**Behavior:**
- Shows item name and distance
- Appears when player is within pickup range

---

## Module Entry Point

### **CryptCraft.h**
**Purpose:** Main module header.

**Declarations:**
- `DECLARE_LOG_CATEGORY_EXTERN(LogCryptCraft, Log, All)` — logging category for the entire project

---

### **CryptCraft.cpp**
**Purpose:** Module initialization and editor-only texture compression fixer.

**Key Functionality (Editor-Only):**
- `FixBlockTextureCompression()` — scans `/Game/Textures/Blocks` and ensures all textures have `CompressionSettings = TC_EditorIcon` (required for runtime BGRA8 access)
- `FCryptCraftModule::StartupModule()` — initializes the module and runs the compression fixer after asset registry is loaded

**Why This is Necessary:**
- Block textures must be readable at runtime via `BulkData.Lock()` to extract raw pixels for atlas packing
- Default compression (DXT) returns compressed data; TC_EditorIcon forces BGRA8 uncompressed format
- The fixer runs automatically at editor startup so artists don't have to manually set this property

---

## Build & Dependencies

### **CryptCraft.Build.cs**
**Purpose:** UnrealBuildTool module rules.

**Key Dependencies:**
- `Core`, `CoreUObject`, `Engine` — standard UE5 modules
- `GameplayTags`, `EnhancedInput` — modern input system
- `UMG`, `Slate` — UI framework
- `ProceduralMeshComponent` — runtime mesh generation
- `AssetRegistry`, `UnrealEd` — editor-only for texture compression fixer

---

## Data Flow Examples

### Example 1: World Generation & Rendering

```
GameMode::BeginPlay()
  ↓ EnsureVoxelWorld()
AVoxelWorld::BeginPlay()
  ↓ BuildTextureAtlas()
  ├─ Load BlockTextures (or discover from TextureBasePath)
  ├─ Pack into RuntimeAtlas UTexture2D
  ├─ Build TextureToTileIndex map
  └─ Create RuntimeChunkMaterial with atlas injected
  ↓ (first chunks loaded based on mode)
LoadChunk() for each visible chunk
  ↓ GenerateChunkData() or GenerateFlatChunkData()
  ↓ GenerateSurfaceObjects() (boulders, trees, etc.)
AChunk::Initialize(BlockData)
  ↓ RebuildMesh()
  ├─ BuildGreedyMesh() — merges faces, encodes UVs/colors
  └─ Apply RuntimeChunkMaterial to ProceduralMesh
Chunk visible on screen with textured faces
```

### Example 2: Block Modification

```
Player breaks block at world position (100, 100, 100)
  ↓ VoxelWorld::SetBlockAt(FIntVector(100, 100, 100), Air)
  ├─ Locate chunk containing that voxel
  ├─ Chunk::SetBlock(local coords, Air, bRebuild=true)
  ├─ Chunk::RebuildMesh()
  │  └─ BuildGreedyMesh() — recomputes all faces, merges neighboring quads
  └─ ProceduralMesh updated (visible immediately)
  ↓ ItemPickup spawned at break location
  ↓ Player collision detects ItemPickup
  ↓ InventoryComponent::AddItem()
  ↓ HotbarWidget / InventoryWidget updated
```

### Example 3: Chunk Streaming

```
Player moves to new location
  ↓ PlayerController / Character updates world position
  ↓ VoxelWorld::Tick() (every 0.5 s)
  ├─ Compute player's chunk coordinate
  ├─ For all chunks in RenderDistance:
  │  ├─ Chunks within distance → LoadChunk() if not loaded
  │  └─ Chunks outside distance → UnloadChunk()
  ├─ Newly loaded chunks generate data and build meshes
  └─ Unloaded chunks deleted, memory reclaimed
```

---

## Atlas System Deep Dive

The atlas packing system is critical for texture rendering. Here's how it works:

1. **Asset Preparation (Editor)**
   - Block textures stored as 16×16 PNG files in `/Game/Textures/Blocks/`
   - Must have `CompressionSettings = TC_EditorIcon` (auto-fixed by `CryptCraft.cpp` startup)

2. **Atlas Building (Runtime, BeginPlay)**
   - `BuildTextureAtlas()` iterates `BlockTextures` map
   - For each texture:
     - Call `BulkData.Lock()` to access raw BGRA8 pixel data
     - Copy into `RuntimeAtlas` buffer at position `(TileX * 16, TileY * 16)`
     - Record `TextureName → TileIndex` in map
   - Calculate `ComputedAtlasCols = ceil(sqrt(numTextures))`
   - Create UTexture2D transient from buffer and bind to `RuntimeChunkMaterial`

3. **Rendering (Per-Quad)**
   - Chunk::BuildGreedyMesh emits UV as atlas-space tile corner:
     ```cpp
     FVector2D AU = (TileX / ComputedAtlasCols);      // 0..1 within atlas
     FVector2D AV = (TileY / ComputedAtlasCols);
     OutUVs = { FVector2D(AU, AV), FVector2D(AU + TileU, AV), ... }
     ```
   - Vertex colors encode tile position for fallback (legacy encoding)
   - Material samples `RuntimeAtlas` at UV coordinates
   - Result: correct 16×16 texture appears on quad face

---

## Known Limitations & TODOs

1. **Missing Textures:** Several block types have no assets yet (Water, Lava, Birch, Spruse, CopperOre, IronOre, EmeraldOre, SapphireOre) — they render as fallback color
2. **Terrain Gen:** Simple 5-octave Perlin; no biomes, caves, trees yet
3. **Surface Objects:** Boulder placement only; trees/vegetation not implemented
4. **Lighting:** Static directional + sky light; no dynamic lights on blocks
5. **Fluid Physics:** Water and Lava are solid blocks, not fluids
6. **Inventory:** Basic system; no crafting or equipment
7. **Multiplayer:** Single-player only; no networking
8. **Performance:** Large render distances can cause frame drops; optimize greedy meshing or use LOD

---

## Common Workflows

### To Add a New Block Type

1. Add entry to `EBlockType` enum in `VoxelTypes.h`
2. Add texture key → UTexture2D mapping to `AVoxelWorld::BlockDefinitions` (Blueprint or C++)
3. Ensure texture files exist at expected path or add manually to `BlockTextures` map
4. Generator functions will use the block; it will render once textures are present

### To Change World Generation

1. Edit `VoxelWorld::SampleTerrainHeight()` for noise function
2. Edit `GenerateChunkData()` to use different height mapping
3. Or add new generator (e.g., `GenerateNether()`) and wire it to `EWorldGenType`

### To Adjust Chunk Streaming

1. Edit `RenderDistance` in GameMode Blueprint or C++
2. Or override `UpdateStreamingPosition()` in a custom VoxelWorld subclass

### To Replace the Material

1. Create new material with a `TextureSample` node using UV0
2. Add a Texture2D parameter named `AtlasTexture`
3. Assign to `ChunkMaterial` on the GameMode or VoxelWorld

---

## Debugging Tips

1. **VoxelHUD** displays chunk coords and local voxel position — use to verify generation boundaries
2. **Diagnostic Logs:** Search for `[AtlasDiag]` and `[ChunkDiag]` in Output Log for atlas/mesh details
3. **Wireframe:** Press `G` (editor) or console `viewmode wireframe` to see mesh topology
4. **Block Inspector:** Can edit voxels at runtime via `SetBlockAt()` (good for testing)
5. **Texture Issues:** Check Output Log for "Block texture not found" warnings; verify `TextureBasePath` and filenames match keys

