# Crystal Caverns — Procedural Generation Plan

## Design Summary

Two independent but connected systems, both deterministic functions of `(WorldSeed, Coordinates)` — no runtime simulation, no pregeneration, fully chunk-streamable.

1. **Caverns** — large rooms, placed via coarse 2D layout noise, shaped via 3D density noise. One large cavern per (X,Z) column, alternating between an upper and lower vertical layer (staggered checkerboard) so they never stack.
2. **Tunnels** — worm-path carving, noise-driven wander + deterministic steering toward target caverns. Guarantees connectivity by construction. Small caves are radius-bulges along the same worm path, also guaranteed connected.

Biome identity (crystal walls, hot springs, bare rock, plant life) is a separate noise layer applied to both caverns (2D, by region) and tunnels (1D, along path), sharing one config data structure.

Everything must be re-derivable per-chunk from seed alone. Saved data = world seed + per-chunk block diffs only.

---

## Step 0 — Foundation: Seeded Deterministic Noise Utilities

**Goal:** A small library of helper functions everything else builds on. No voxel logic yet.

Build:
- `SeededNoise1D(int32 Seed, float X)` — deterministic 1D noise, used for worm wander/biome-along-path
- `SeededNoise2D(int32 Seed, float X, float Y)` — deterministic 2D noise, used for layout/biome regions
- `SeededNoise3D(int32 Seed, FVector Pos)` — deterministic 3D density noise, used for cavern shape
- `HashCellSeed(int32 WorldSeed, FIntVector CellCoord)` — combines world seed + a coarse grid cell coordinate into a unique deterministic seed for that cell (this is what lets any chunk independently ask "does cell X have a worm/cavern, and what does it look like" without needing neighbors generated first)

**Test:** Write a tiny standalone test (or log output) confirming the same input always produces the same output, and that nearby inputs produce smoothly varying (not chaotic) output for the *Noise functions, while HashCellSeed can be chaotic/well-distributed (that's fine, it's just a unique ID generator, not a smooth field).

**Do not proceed until this is solid** — every later step depends on these being correctly deterministic.

---

## Step 1 — Cavern Layout: Layer Selection + Bubble Placement

**Goal:** For any (X,Z) column, determine if a cavern bubble exists there, which vertical layer it's in, and its rough center/radius. No voxel carving yet — just placement logic, tested via debug visualization (e.g., a 2D top-down debug overlay showing bubble centers and layer assignment).

Build:
- `FCavernLayerConfig` data asset — defines vertical layers (MinY, MaxY, layout noise threshold, associated tunnel Y)
- `ECavernLayer GetLayerForColumn(int32 WorldSeed, int32 X, int32 Z)` — samples layout noise (`SeededNoise2D`), returns which layer (upper/lower) is active for this column, per the staggered checkerboard rule
- Coarse cavern placement grid (e.g., 1 cavern candidate per N×N column region) — `HashCellSeed` per region determines: does a cavern spawn here, what's its center point, what's its approximate radius

**Test:** Generate a large flat debug grid (could be a simple 2D texture or in-editor visualization) showing cavern bubble centers across a large area. Confirm: no two bubbles in the same (X,Z) region are in different layers stacked on each other, and bubble placement looks organically scattered, not gridlocked.

---

## Step 2 — Cavern Shape Carving

**Goal:** Turn placed bubbles into actual carved voxel space using 3D density noise, gated by the layer determined in Step 1.

Build:
- `bool IsCavernVoxel(int32 WorldSeed, FIntVector WorldPos)`:
  - Determine active layer for this (X,Z) via Step 1
  - If WorldPos.Y is outside that layer's range, return false
  - Find nearest cavern bubble candidate(s) from the placement grid
  - If within bubble's rough radius, evaluate `SeededNoise3D` — if above density threshold, this voxel is air (cavern interior)

**Test:** Hook this into your chunk generation as an additional carve pass (alongside or replacing whatever placeholder terrain exists). Fly through a generated area and confirm caverns appear as expected — single large rooms, no double-stacking, reasonable organic shape (not perfect spheres, not too sparse/holey).

---

## Step 3 — Worm Tunnel Path Definition (Deterministic, Stateless)

**Goal:** Define a worm tunnel's full path as a pure function of `(WormSeed, Step)`, so any chunk can re-derive any portion of any worm without simulating from step 0.

Build:
- Coarse worm-spawn grid (separate from cavern grid, can be different cell size) — `HashCellSeed` per cell determines: does a worm spawn here, starting position, starting direction, target cavern lookup (nearest 1-2 cavern bubbles from Step 1's placement data, deterministically chosen)
- `FVector GetWormDirectionAtStep(int32 WormSeed, int32 Step, FVector TargetCavernCenter, FVector CurrentPos)`:
  - `WanderDir` from `SeededNoise1D` (yaw/pitch jitter)
  - `SteerDir` toward `TargetCavernCenter`
  - Blend by `SteerWeight` (consider increasing weight as distance to target shrinks)
- `FVector GetWormPositionAtStep(int32 WormSeed, int32 Step)` — accumulate position by integrating direction across steps. (Practical approach: cache a precomputed path per worm the first time it's needed by any chunk, store in a small in-memory map keyed by WormSeed so repeated chunk queries don't recompute from step 0 every time — this is a runtime cache, not saved data, since it's always cheaply regenerable.)

**Test:** Before any carving — just log/visualize the path itself (e.g., draw debug spheres along computed worm positions across a large area). Confirm: paths look like reasonable wandering tunnels, generally trend toward their target cavern, don't do anything wildly erratic (huge sudden direction snaps).

---

## Step 4 — Tunnel Carving + Radius Bulges (Small Caves)

**Goal:** Carve the worm path into actual voxel space, with occasional radius bulges for small caves.

Build:
- `float GetWormRadiusAtStep(int32 WormSeed, int32 Step)` — base radius (~2-3 blocks) + occasional bulge via `SeededNoise1D` at a lower frequency (long, slow swells, not per-block jitter) pushing radius up to 4-8 for a stretch
- `bool IsTunnelVoxel(int32 WorldSeed, FIntVector WorldPos)`:
  - Find nearby worm(s) whose path passes near this position (use the coarse worm-spawn grid to know which worms are even candidates for this chunk, including from adjacent cells)
  - For each candidate worm, check distance from WorldPos to nearest point on that worm's path
  - If within `GetWormRadiusAtStep` for that nearest point, this voxel is air
  - Optionally blend in a touch of `SeededNoise3D` near the surface of the tunnel for non-perfectly-smooth walls

**Test:** Carve tunnels into the world alongside caverns from Step 2. Walk through generated terrain. Confirm: tunnels are traversable without breaking blocks, mostly horizontal with gentle slopes, occasional wider bulge areas feel like small caves, and tunnels visibly connect into cavern rooms (taper into the cavern rather than abruptly stopping at its boundary — may need a small radius taper as the worm path enters a cavern's bubble radius).

---

## Step 5 — Biome Assignment

**Goal:** Tag carved voxels (or carved regions) with a biome identity, shared config between caverns and tunnels.

Build:
- `FCavernBiome` data asset — biome name, wall material, spawnable asset list (crystals, pool meshes, plants), density, noise range this biome occupies
- `FName GetCavernBiomeAt(int32 WorldSeed, FVector WorldPos)` — 2D noise over (X,Z), independent frequency from layout noise, mapped through biome config ranges
- `FName GetTunnelBiomeAtStep(int32 WormSeed, int32 Step)` — 1D noise over path step, same biome config, low frequency for long zones rather than block-by-block flicker

**Test:** Render different wall materials per biome (even just flat-color placeholder materials at this stage) and confirm regions are large, coherent, and transition smoothly rather than flickering block to block. Confirm tunnel biome zones are long stretches (tens of blocks) not chaotic.

---

## Step 6 — Asset Population (Crystals, Pools, Plants)

**Goal:** Within biome-tagged regions, spawn the actual decorative/functional assets (hot spring pools on walls/floor, unique crystal formations, plant life).

Build:
- Per-biome spawn rules from `FCavernBiome.SpawnableAssets` + `AssetDensity`
- Deterministic per-voxel-surface roll (seeded by position) deciding whether to spawn an asset at a given valid surface point (e.g., floor voxel adjacent to air, wall voxel adjacent to air) within a biome region
- Basic placement validation (don't spawn inside solid blocks, respect surface normal for wall-mounted assets like pools)

**Test:** Walk through a hot-spring-tagged region and a crystal-tagged region, confirm assets feel appropriately dense (not overlapping, not sparse to the point of looking empty) and are placed sensibly (pools on appropriate surfaces, crystals not floating in air).

---

## Step 7 — Save System Integration

**Goal:** Confirm the diff-only save approach works correctly against this generation system.

Build/Verify:
- Per-chunk modification storage already in place from your existing world system should apply unchanged here — confirm cavern/tunnel chunks regenerate identically from seed, then diffs apply cleanly on top
- Test: place/break blocks in a cavern and a tunnel, save, reload, confirm world matches exactly (procedural base + your edits)

**Test:** This should mostly be confirming existing systems work, not new code — but cavern/tunnel chunks are more complex than flat terrain, so worth explicitly testing edge cases (e.g., breaking a block right at a tunnel/cavern boundary).

---

## Notes for Working With Copilot

- Implement and test one step at a time — do not let Copilot jump ahead to later steps "while it's at it." Each step has a clear, narrow test criteria above; confirm that before moving on.
- Steps 0 and 3 are the highest-risk/most novel pieces (deterministic stateless noise, and deterministic stateless worm paths) — expect to iterate on these more than the others.
- Push back if Copilot suggests caching worm paths or chunk data as *saved* data — only the world seed and player modification diffs should be persisted. Runtime in-memory caching (for performance, e.g. avoiding recomputing a worm path every single chunk query) is fine and expected, but should be clearly distinct from save-file data.
- Keep `FCavernLayerConfig` and `FCavernBiome` as data assets/tables, not hardcoded values, per the earlier configurability discussion.
