# Minecraft-Clone Ship System — Design Plan

## Core Concept

Ships are **independent block grids** that exist as their own Actors in the world, free from the world grid. Each ship has its own local coordinate space and a quaternion transform (position + rotation) mapping it into world space. The world grid stays static; ships float free of it.

---

## Architecture Overview

### Two Grids
- **World Grid** — static, integer coordinates, stores all terrain and placed blocks
- **Ship Grid** — per-ship sparse map of blocks in local integer coordinates, transformed into world space via `FQuat` + `FVector` origin

### Local → World Transform
```
WorldPos = ShipOrigin + (WorldRotation * (LocalBlockPos * BlockSize))
```

### Ship Data Structure
```cpp
struct FShip {
    TMap<FIntVector, FBlockData> Blocks;  // local coords → block
    FVector WorldPosition;                 // origin in world space
    FQuat WorldRotation;                   // quaternion — supports full 6DOF
    FVector LinearVelocity;
    FVector AngularVelocity;
    AControlBlock* ControlBlock;
    APawn* Pilot;                          // null if unpiloted
};
```

### Rendering
Use **Instanced Static Meshes (ISM/HISM)** — one ISM component per block type on the ship Actor. Rotation is handled by the Actor's transform, no per-block math needed.

---

## Key Design Decisions

| Decision | Choice | Reason |
|---|---|---|
| Rotation representation | Quaternions from day one | Avoids gimbal lock, natural upgrade path to full 6DOF |
| Ship vs world collision | Manual sweep-check per tick | Predictable, enables crash damage, avoids Chaos jitter |
| Water collision | Overlap channel + buoyancy forces | Ship passes through water physically, buoyancy faked per submerged block |
| Pilot control | UE5 Possess/Unpossess pattern | Standard vehicle pattern, clean input separation |

---

## Transition (World → Ship) — Atomic Swap Protocol

When a ship is created, blocks must transfer from world grid to ship grid without the player falling or experiencing a physics pop.

**Sequence:**
1. Spawn ship Actor at exact world position of the selected blocks
2. Build ship mesh and **enable collision** on ship Actor
3. **Disable collision** on those world-grid blocks (don't delete yet)
4. Wait one physics tick — player is now standing cleanly on ship collision only
5. Remove blocks from world grid and world mesh

This prevents both the "player falls through gap" and the "double-overlap physics pop" problems.

---

## Water Handling

- After ship block removal, run a **water propagation pass** to fill vacated underwater cells
- Buoyancy: each tick, count ship blocks overlapping world-grid water cells → apply upward force proportional to submerged count
- Water blocks use a separate collision channel set to `Overlap` (not `Block`) against the ship

---

## Ship vs World Collision During Movement

Each tick, before applying movement:
1. Compute where each ship block would be at the new position/rotation
2. Check those world positions against the world grid for solid blocks
3. If overlap found → cancel movement, trigger collision event (crash damage, bounce, hard stop)

---

## Build Order

### Step 1 — Flood Fill Detection
BFS from control block. Collect the set of blocks that will form the ship.

### Step 2 — Ship Actor Instantiation
Atomic swap: spawn ship Actor, build ISM mesh, disable world collision, wait one tick, remove world blocks.

### Step 3 — Static Ship Exists
Ship is a standalone Actor at the correct world position. No movement yet. Verify visuals and collision.

### Step 4 — Possession Swap
Player clicks helm → `Controller->Possess(ShipPawn)` → basic input routing works.

### Step 5 — Yaw-Only Movement
Apply rotation around world Z axis via quaternion each tick. Ship moves.

### Step 6 — Full 6DOF
Unlock pitch and roll. Ship vs world sweep-check becomes fully 3D.

---

## Step 1 Spec: Flood Fill Detection

### Goal
When a player activates the control block, find all blocks connected to it that should become part of the ship.

### Algorithm
**BFS from the control block's grid position.**

```
queue = [controlBlockPos]
visited = {}
shipBlocks = {}

while queue not empty:
    pos = queue.pop()
    if pos in visited: continue
    visited.add(pos)

    block = worldGrid.GetBlock(pos)
    if block is Air or block is Water: continue
    if shipBlocks.count >= BLOCK_LIMIT: break

    shipBlocks.add(pos, block)

    for each of 6 neighbors (±X, ±Y, ±Z):
        if neighbor not in visited:
            queue.push(neighbor)
```

### Rules
- **Include:** any solid block reachable from the control block via solid-block adjacency
- **Stop at:** Air, Water — these are natural hull boundaries
- **Limit:** enforce a max block count (e.g. 2000) to prevent runaway BFS on huge structures
- **Sea floor exclusion is automatic:** water between hull and sea floor breaks connectivity, no special casing needed

### Output
`TMap<FIntVector, FBlockData> ShipBlocks` — the full set of blocks and their world grid positions that will form the ship.

### What to Build in Step 1
- `AControlBlock` actor — placed by player, has an `Interact()` function
- `UShipDetectionComponent` (or free function) — runs the BFS when `Interact()` is called
- On success: log the block count and positions, draw debug boxes in the editor to visualize the detected ship outline
- No ship Actor spawned yet, no blocks removed — detection only

### Suggested UE5 Approach
- World grid should be queryable via something like `AWorldGrid::GetBlock(FIntVector pos)` returning a block type or null
- BFS uses a `TQueue<FIntVector>` and a `TSet<FIntVector>` for visited tracking
- Draw results with `DrawDebugBox` in world space for each detected block (use a distinct color for the control block)

---

## Current Status (Step 5 Complete)

### ✅ Working
- Flood fill detection (BFS with water/air boundaries, <10k block limit)
- Atomic transfer (spawn → build mesh → disable world collision → wait 1 tick → remove blocks)
- Ship mesh rendering (ProceduralMeshComponent with greedy meshing, no collision falls through)
- Possession system (right-click control block to possess ship, right-click anywhere to depossess)
- Full 6DOF input infrastructure (W/S forward/back, A/D yaw rotation, Space/Shift up/down)
- Quaternion-based yaw rotation with camera following

### ⚠️ Known Issues & Design Notes

#### Depossess Position Problem
When the player possesses a ship by clicking its control block **from off the ship** and then depossesses (right-click), the player is restored to their original world position, potentially very far from where the ship has traveled. This breaks immersion and gameplay.

**Solution:** 
- Option A: Teleport player onto the ship surface when possessing (find nearest block face, place player on it)
- Option B: Save player position relative to ship at possession time, restore relative position at depossess time (player moves with ship in local space)
- Option C: Anchor player to ship while piloting (player position becomes part of ship transform)

#### Rotation Smoothing / Momentum
Current rotation is instantaneous per key press. For better feel:
- Add rotational momentum/velocity that decays over time
- Smooth acceleration in and out of turns
- Potentially add air resistance multiplier

#### Still Missing for Full Gameplay
1. **Textures** — Ship mesh currently uses grey material; need to preserve original block textures from the world grid
2. **Block Add/Remove** — Player should be able to add blocks to the ship grid or break/remove them while piloted
3. **Ship vs World Collision** — Each tick, sweep check all ship block positions against world grid; cancel movement if collision, trigger damage/bounce event

#### Future Quality-of-Life Features
- **Free Camera** — Allow player to look around freely (detach camera from ship rotation) while still piloting, so they can inspect the ship and surroundings without losing control

---

## Open Questions to Revisit Later
- Does the ship persist when unpiloted, or do blocks return to the world grid?
- Chunk boundary handling — does the world grid use chunks? BFS needs to cross chunk borders.
- Multiplayer considerations (if ever relevant)
