# Crystal Caverns Lighting - Current State for Claude Review

## PRIMARY GOAL
**Get minimum brightness (0.30) on ALL block faces so nothing renders completely black.**

Current approach: Compute per-face brightness via `dot(faceNormal, sunDirection)` clamped to MinimumBrightness, encode in vertex color alpha, multiply in material.

## WHAT'S ACTUALLY IN THE LEVEL
1. **DirectionalLight #1** (180, -89, -135) - Sun light, points DOWN
2. **DirectionalLight #2** (180, 26, 45) - Unknown purpose, points UP  
3. **SkyLight** - Contributes light only when clouds pass (dynamic)
4. **SkyAtmosphere** - Auto-spawned by GameMode (works)

## ACTUAL BEHAVIOR OBSERVED
- Delete DirectionalLight #1 (180, -89, -135): **Tops of blocks go black, sides stay lit**
- Delete DirectionalLight #2 (180, 26, 45): **Sides of blocks go darker, tops stay lit**
- Delete both: **Pure black everywhere**
- SkyLight only adds light when clouds roll by (minor contribution)

**Interpretation:** Two separate lights with different rotation angles produce different shading on different faces. Not fill/sun relationship - just two directional lights with different purposes.

## CODE CHANGES ATTEMPTED
1. `GetSunDirection()` tries to pick ONE directional light to use for brightness computation
2. Started with "pick highest intensity" - didn't work
3. Changed to "pick light NOT at world origin" - NOT YET TESTED
4. Problem: Code only uses ONE light's direction for dot product, but two lights are in level

## CORE ISSUE
We don't know:
- Why are there TWO DirectionalLights in the level?
- Should we compute brightness from BOTH or just ONE?
- If just one, which one?
- Is this a level design issue or a code issue?
- Should the brightness computation even use DirectionalLights at all, or just rely on Unreal's native lighting?

## WHAT'S WORKING
✅ Vertex color alpha is being read by material (tested with forced 0.5f - worked)
✅ Material multiply node is connected (BaseColor × VertexColor.A)
✅ Brightness computation runs and logs correct values (FinalBright=0.300)
✅ SkyAtmosphere spawns successfully (blue sky visible)
✅ Texture atlas builds and displays correctly

## WHAT'S NOT WORKING
❌ Minimum brightness floor not visible on faces
❌ Currently using wrong DirectionalLight for dot product (picking fill light instead of sun)
❌ Unclear why two DirectionalLights exist and what the design intent is

## IMMEDIATE QUESTIONS FOR CLAUDE
1. Should we compute brightness from BOTH directional lights or just ONE?
2. Is the two-light setup intentional or a mistake?
3. Should we simplify this to just use Unreal's native per-vertex lighting instead of manual computation?
4. What's the actual design intent for cavern lighting?
