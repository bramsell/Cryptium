Here's a clean implementation plan for Copilot:

---

## Bridge/Tunnel System — Implementation Plan

### Overview
A unified bridge-in-air / tunnel-in-rock system driven by a single 3D Perlin noise field. No wall-finding, no cell placement, no direction vectors. The system is entirely self-contained in one function (`GenerateBridgeTunnel`) that slots into `GenerateNoise2DCavern` as an optional pass. Enable/disable by toggling one constant.

---

### Step 1 — Add the enable flag and config constants

At the top of the config section alongside the existing zone structs, add:

```cpp
// ---------------------------------------------------------------------------
//  BRIDGE/TUNNEL CONFIG - Unified bridge-in-air / tunnel-in-rock system
//  Set BRIDGES_ENABLED to false to disable entirely with no other changes needed
// ---------------------------------------------------------------------------
static constexpr bool BRIDGES_ENABLED = true;

static constexpr float BRIDGE_NOISE_FREQ_XY   = 0.008f;  // horizontal frequency — controls bridge spacing/curve rate
static constexpr float BRIDGE_NOISE_FREQ_Z    = 0.002f;  // vertical frequency — keep low for walkable slopes
static constexpr float BRIDGE_THRESHOLD_BASE  = 0.015f;  // base thickness of bridge/tunnel ribbon
static constexpr float BRIDGE_THRESHOLD_WALL  = 0.040f;  // thickness at wall contact (wider at walls)
static constexpr float BRIDGE_SLANT_FREQ      = 0.005f;  // frequency of height warping — controls slope rate
static constexpr float BRIDGE_SLANT_AMPLITUDE = 15.0f;   // max height variation in blocks (±15 blocks)
static constexpr float BRIDGE_BASE_MID_Z      = 0.5f;    // fraction of layer height for bridge floor (0.5 = middle)
static constexpr float BRIDGE_WALL_FALLOFF    = 0.02f;   // how quickly bridge narrows away from wall (higher = faster)
```

---

### Step 2 — Add `GenerateBridgeTunnel` function

Add this as a standalone static function below the existing noise helpers, before `GenerateNoise2DCavern`. It takes only values already computed by `GenerateNoise2DCavern` — no new lookups needed:

```cpp
// ---------------------------------------------------------------------------
//  GenerateBridgeTunnel
//
//  Returns a block override if this voxel is part of the bridge/tunnel system,
//  or EBlockType::MAX as a sentinel meaning "no override, use default logic."
//
//  How it works:
//  - A single 3D Perlin field defines thin ribbon-like surfaces (zero-crossings)
//  - These ribbons form an organic network of curving paths with natural junctions
//  - In AIR regions:   bottom half of ribbon = solid (bridge deck + arch)
//  - In ROCK regions:  top half of ribbon = air (tunnel void, floor stays solid)
//  - MidZ (the walkable floor height) is warped by low-frequency noise → slanted bridges
//  - Ribbon thickness is modulated by distance from air/rock boundary → wide at walls, thin in open space
//
//  Parameters (all already computed in GenerateNoise2DCavern):
//    RegionNoise         — raw output of Sample2DNoise (positive = air, negative = rock)
//    VoxelHeightFromFloor — layer-relative Z in blocks from floor
//    TotalCavernHeight   — total height of this cavern layer in blocks
//    WorldX, WorldY      — world block coordinates
//    SolidBlockType      — block type to use for solid (matches rest of layer)
// ---------------------------------------------------------------------------
static EBlockType GenerateBridgeTunnel(
    float RegionNoise,
    float VoxelHeightFromFloor,
    int32 TotalCavernHeight,
    int32 WorldX,
    int32 WorldY,
    EBlockType SolidBlockType)
{
    if (!BRIDGES_ENABLED)
    {
        return EBlockType::MAX; // sentinel: no override
    }

    // --- Slanting floor height ---
    // Low-frequency 2D noise makes bridges rise and fall across the landscape
    float SlantNoise = CavePerlin2D(WorldX * BRIDGE_SLANT_FREQ, WorldY * BRIDGE_SLANT_FREQ);
    float MidZ = TotalCavernHeight * BRIDGE_BASE_MID_Z + SlantNoise * BRIDGE_SLANT_AMPLITUDE;

    // --- Bridge/tunnel noise field ---
    // Evaluate 3D Perlin at compressed vertical scale so ribbons are walkable (tall, not flat)
    // Z input uses distance from slanted MidZ so the ribbon is always centered on the floor
    float VerticalInput = (VoxelHeightFromFloor - MidZ) * BRIDGE_NOISE_FREQ_Z;
    float BridgeNoise = CavePerlin3D(
        WorldX * BRIDGE_NOISE_FREQ_XY,
        WorldY * BRIDGE_NOISE_FREQ_XY,
        VerticalInput);

    // --- Width modulation: wide at air/rock boundary, thin toward cavern center ---
    // RegionNoise is near 0 at boundary, larger positive deep in air, larger negative deep in rock
    float BoundaryDist = FMath::Abs(RegionNoise) * 100.0f; // 0 at boundary, increases away from it
    float WallProximity = FMath::Exp(-BoundaryDist * BRIDGE_WALL_FALLOFF); // 1.0 at wall, 0 toward center

    // Blend threshold between base (thin, open space) and wall (wide, at contact)
    float Threshold = FMath::Lerp(BRIDGE_THRESHOLD_BASE, BRIDGE_THRESHOLD_WALL, WallProximity);

    // --- Is this voxel inside the bridge/tunnel ribbon? ---
    bool bInRibbon = FMath::Abs(BridgeNoise) < Threshold;
    if (!bInRibbon)
    {
        return EBlockType::MAX; // no override
    }

    bool bIsAir    = RegionNoise > 0.0f;
    bool bAboveMid = VoxelHeightFromFloor > MidZ;

    if (bIsAir && !bAboveMid)
    {
        return SolidBlockType; // bridge deck in air region
    }

    if (!bIsAir && bAboveMid)
    {
        return EBlockType::Air; // tunnel void in rock region
    }

    return EBlockType::MAX; // in ribbon but not in active zone — no override
}
```

---

### Step 3 — Hook into `GenerateNoise2DCavern`

Add the bridge/tunnel call **before** the existing floor/ceiling height checks, right after the region check passes. It takes priority over spikes and pillars:

```cpp
EBlockType FCrystalCavesLevelGenerator::GenerateNoise2DCavern(...)
{
    float RegionNoise = Sample2DNoise(X, Z);
    int32 TotalCavernHeight = (FloorChunk - CeilingChunk + 1) * CHUNK_SIZE_Z;

    // --- Bridge/tunnel pass (highest priority, runs before floor/ceiling features) ---
    const int32 VoxelHeightFromFloor   = (FloorChunk - LocalChunkZ) * CHUNK_SIZE_Z + VoxelZ;
    EBlockType BridgeResult = GenerateBridgeTunnel(
        RegionNoise,
        static_cast<float>(VoxelHeightFromFloor),
        TotalCavernHeight,
        X, Z,
        SolidBlockType);

    if (BridgeResult != EBlockType::MAX)
    {
        return BridgeResult; // bridge/tunnel overrides everything else
    }

    // --- Existing region check ---
    if (RegionNoise <= 0.0f)
    {
        return SolidBlockType;
    }

    // ... rest of existing logic unchanged (floor height, ceiling height, etc.)
}
```

---

### Step 4 — Add `CavePerlin3D` if not already present

The system needs a 3D Perlin function. If `CavePerlin3D` doesn't already exist in `LayerBase.h`, add a simple version alongside `CavePerlin2D`. It follows the same pattern — just extend to three axes with an extra lerp pass.

---

### Testing checklist (test each independently)

1. Set `BRIDGES_ENABLED = false` — world should look identical to before. Confirms clean disable.
2. Set `BRIDGES_ENABLED = true`, fly through air region — should see solid arching ribbons forming bridge decks. Vary `BRIDGE_THRESHOLD_BASE` up/down to confirm thickness responds.
3. Fly into rock region — should see tunnel voids carved above walkable floor. Confirm floor is at same height as bridge deck in adjacent air region.
4. Vary `BRIDGE_SLANT_AMPLITUDE` — bridges and tunnels should visibly slope up and down across the landscape.
5. Look for triple junctions — where three ribbons meet naturally in the noise field. These appear without any extra work; frequency controls how often they occur.
6. Check wall contact — bridges should be noticeably wider where they meet rock walls. Tune `BRIDGE_WALL_FALLOFF` if the taper is too abrupt or too gradual.

---

### Tuning reference

| Constant | Effect | Start value |
|---|---|---|
| `BRIDGE_NOISE_FREQ_XY` | Bridge spacing and curve rate | `0.008f` |
| `BRIDGE_NOISE_FREQ_Z` | Vertical thickness of ribbon | `0.002f` |
| `BRIDGE_THRESHOLD_BASE` | Thinness in open space | `0.015f` |
| `BRIDGE_THRESHOLD_WALL` | Width at wall contact | `0.040f` |
| `BRIDGE_SLANT_AMPLITUDE` | How much bridges slope | `15.0f` |
| `BRIDGE_WALL_FALLOFF` | How quickly bridge narrows from wall | `0.02f` |