// LayerPrimordialCavern.cpp
// Primordial Cavern layer generation (Level 2, GlobalChunkZ -9 .. -16, world Z -257 .. -512).
//
// Block layout (256 blocks = 8 chunks, LocalChunkZ 0 = shallowest):
//   LocalChunkZ 0   : Solid stone ceiling                                ( 32 blocks)
//   LocalChunkZ 1   : Ceiling fringe — Perlin stalactites, up to 32 blocks deep
//   LocalChunkZ 2–3 : Pure open air void                                 ( 64 blocks)
//   LocalChunkZ 4–7 : Land/Water terrain based on 2-octave Perlin        (128 blocks)

#include "LayerPrimordialCavern.h"
#include "LayerBase.h"
#include "Voxel/Chunk.h"

// Perlin noise utility functions are now in LayerBase.h

// 3-octave fBm Perlin for ceiling fringe; result clamped to [0, 1].
static float SampleCeilingFringeNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 20000.5f;
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 96.f;
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 3; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp  *= 0.5f;
		Freq *= 2.f;
	}

	return FMath::Clamp(Value / Total * 0.5f + 0.5f, 0.f, 1.f);
}

// 3-octave fBm continent noise — large scale landmass shape, returns approx [-1, 1].
static float SampleContinentNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 50000.5f;
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 3000.f;  // PIECE 1: Enlarged from 1/475 for larger continental scale
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 3; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp  *= 0.5f;
		Freq *= 2.f;
	}

	return Value / Total;  // [-1, 1]
}

// Spline remap: maps continent noise [-1, 1] to a shaped [0, 1] height fraction.
// PIECE 2: Ocean side compressed (smaller fraction of input range), land side unchanged.
// Control points: (input, output)
//   (-1.0, 0.02)  — deep ocean abyss (unchanged)
//   (-0.65, 0.04) — shallow ocean (MOVED: was -0.40)
//   (-0.40, 0.15) — mid ocean (MOVED: was -0.20)
//   (-0.20, 0.35) — deep shelf (MOVED: was -0.10)
//   (-0.05, 0.48) — upper shelf (MOVED: was -0.03)
//   ( 0.0,  0.50) — coastline (unchanged)
//   ( 0.03, 0.52) — coastal plain (unchanged)
//   ( 0.12, 0.60) — plains (unchanged)
//   ( 0.30, 0.72) — foothills (unchanged)
//   ( 0.50, 0.85) — mid hills (unchanged)
//   ( 0.75, 0.95) — high hills (unchanged)
//   ( 1.0,  1.00) — mountain peaks (unchanged)
static float RemapShapeCurve(float t)
{
	struct FControlPoint { float In; float Out; };
	static constexpr FControlPoint Points[] = {
		{ -1.00f, 0.02f },
		{ -0.65f, 0.04f },  // Compressed ocean range
		{ -0.40f, 0.15f },
		{ -0.20f, 0.35f },
		{ -0.05f, 0.48f },
		{  0.00f, 0.50f },
		{  0.03f, 0.52f },
		{  0.12f, 0.60f },
		{  0.30f, 0.72f },
		{  0.50f, 0.85f },
		{  0.75f, 0.95f },
		{  1.00f, 1.00f },
	};
	static constexpr int32 NumPoints = UE_ARRAY_COUNT(Points);

	// Clamp to valid range
	if (t <= Points[0].In)          return Points[0].Out;
	if (t >= Points[NumPoints-1].In) return Points[NumPoints-1].Out;

	// Linear search for the surrounding segment
	for (int32 i = 0; i < NumPoints - 1; ++i)
	{
		if (t <= Points[i+1].In)
		{
			const float SegT = (t - Points[i].In) / (Points[i+1].In - Points[i].In);
			return FMath::Lerp(Points[i].Out, Points[i+1].Out, SegT);
		}
	}

	return Points[NumPoints-1].Out;
}

// 2-octave fBm detail noise — high frequency surface roughness, returns [-1, 1].
static float SampleDetailNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 70000.5f;
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 40.f;
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 2; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp  *= 0.5f;
		Freq *= 2.f;
	}

	return Value / Total;  // [-1, 1]
}

// PIECE 3: High-frequency noise for breaking up coastline with organic detail
static float SampleCoastJitter(float WX, float WY)
{
	static constexpr float NoiseOffset = 60000.5f;
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 40.f;  // High frequency for fine coastal variation
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 2; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp  *= 0.5f;
		Freq *= 2.f;
	}

	return Value / Total;  // [-1, 1]
}

// PIECE 4: Low-frequency noise for sparse island clusters in deep ocean
static float SampleIslandMask(float WX, float WY)
{
	static constexpr float NoiseOffset = 80000.5f;
	static constexpr float Freq = 1.f / 200.f;  // Low frequency for large island clusters
	
	float Value = CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq);
	return Value;  // [-1, 1]
}



// ---------------------------------------------------------------------------
//  Zone indices  (LocalChunkZ within the Primordial Cavern layer, 0 = shallowest)
// ---------------------------------------------------------------------------

// CEILING_SOLID_Z and CEILING_FRINGE_Z are now in LayerBase.h

static constexpr int32 TERRAIN_START_Z     = 5;   // 3 chunks of terrain (LocalChunkZ 5-7)
static constexpr int32 TERRAIN_END_Z       = 7;

// ---------------------------------------------------------------------------
//  GenerateChunk
// ---------------------------------------------------------------------------

void FPrimordialCavernLevelGenerator::GenerateBlocks(
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ,
	TArray<EBlockType>& OutBlocks)
{

	// ---- Solid stone ceiling ----------------------------------------
	if (LocalChunkZ == CEILING_SOLID_Z)
	{
		OutBlocks.Init(EBlockType::Stone, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		return;
	}



	OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

	// ---- Ceiling fringe (LocalChunkZ 1) - Stalactites hanging down --------
	if (LocalChunkZ == CEILING_FRINGE_Z)
	{
		for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
		{
			for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
			{
				const float WX = static_cast<float>(GlobalChunkX * CHUNK_SIZE_X + X);
				const float WY = static_cast<float>(GlobalChunkY * CHUNK_SIZE_Y + Y);

				// How many blocks of stone hang down (0..32)
				const int32 FringeBlocks = FMath::RoundToInt(SampleCeilingFringeNoise(WX, WY) * CHUNK_SIZE_Z);

				for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
				{
					// Z=31 is the shallowest face, adjacent to solid ceiling.
					// Stone hangs DOWN: fill the top FringeBlocks of the chunk.
					EBlockType Type = (Z >= CHUNK_SIZE_Z - FringeBlocks) ? EBlockType::Stone : EBlockType::Air;
					OutBlocks[BlockIdx(X, Y, Z)] = Type;
				}
			}
		}
		return;
	}

	// ---- Terrain section (LocalChunkZ 6-7) - Land vs water ----------------
	if (LocalChunkZ >= TERRAIN_START_Z && LocalChunkZ <= TERRAIN_END_Z)
	{
		for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
		{
			for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
			{
				// Calculate world position and sample terrain noise
				const float WX = static_cast<float>(GlobalChunkX * CHUNK_SIZE_X + X);
				const float WY = static_cast<float>(GlobalChunkY * CHUNK_SIZE_Y + Y);

				// 1. Large-scale continent shape
				const float ContinentNoise = SampleContinentNoise(WX, WY);

			// 2. Remap through spline control points to get base height fraction
			float ShapeValue = RemapShapeCurve(ContinentNoise);

			// 2.5 PIECE 3: Apply coast jitter — breaks up coastline with organic detail
			//     Fades in near the threshold, fades out as you move well into land or ocean
			if (ShapeValue > 0.42f && ShapeValue < 0.58f)  // ±0.08 from 0.50 (coastline)
			{
				const float CoastDistance = FMath::Abs(ShapeValue - 0.50f) / 0.08f;  // [0, 1]: 0=at coast, 1=far from coast
				const float JitterFade = 1.0f - (CoastDistance * CoastDistance);  // Fade^2 for smooth cutoff
				const float CoastJitter = SampleCoastJitter(WX, WY);  // [-1, 1]
				
				ShapeValue += CoastJitter * 0.12f * JitterFade;  // Jitter amplitude: 0.12, faded by zone proximity
				ShapeValue = FMath::Clamp(ShapeValue, 0.0f, 1.0f);
			}

			// 3. Detail amplitude varies by terrain zone with smooth interpolation:
			//    deep ocean / flat plains = low ripple, hills = more roughness
			//    Using smooth lerp to avoid hard ridge artifacts at zone boundaries
			float DetailAmplitude = 0.02f;  // ocean base
			if (ShapeValue > 0.55f)
			{
				// Smooth transition from 0.55 to 0.65: 0.02 → 0.06
				DetailAmplitude = FMath::Lerp(0.02f, 0.06f, FMath::Clamp((ShapeValue - 0.55f) / 0.10f, 0.f, 1.f));
			}
			if (ShapeValue > 0.65f)
			{
				// Smooth transition from 0.65 to 0.75: 0.06 → 0.18
				DetailAmplitude = FMath::Lerp(0.06f, 0.18f, FMath::Clamp((ShapeValue - 0.65f) / 0.10f, 0.f, 1.f));
			}

				// 4. Blend detail noise in (add-only, no downward drops)
			const float DetailNoise  = SampleDetailNoise(WX, WY);  // [-1, 1]
			const float DetailContribution = FMath::Max(0.0f, DetailNoise * DetailAmplitude);  // Only add, never subtract
			float FinalShape = FMath::Clamp(ShapeValue + DetailContribution, 0.f, 1.f);

			// 5. Map [0, 1] → world height [32, 95]
			int32 GroundHeight = FMath::Clamp(32 + FMath::RoundToInt(FinalShape * 63.f), 32, 95);

			// 6. PIECE 4: Apply island bump in deep ocean only — adds 5–12 blocks to rare island peaks
			//    Islands are sparse and isolated, gated to ShapeValue < 0.35 (deep ocean)
			if (ShapeValue < 0.35f)
			{
				const float IslandMask = SampleIslandMask(WX, WY);  // [-1, 1]
				
				// High threshold (0.70) ensures islands are rare and clustered
				if (IslandMask > 0.70f)
				{
					// Normalize mask intensity to [0, 1], then scale to bump height [0, 12]
					const float IslandIntensity = FMath::Clamp((IslandMask - 0.70f) / 0.30f, 0.0f, 1.0f);
					const int32 IslandBump = FMath::RoundToInt(IslandIntensity * 12.0f);  // CORRECTED: 12.0 (was 8.0)
					
					GroundHeight += IslandBump;
					GroundHeight = FMath::Clamp(GroundHeight, 32, 95);  // Safety clamp
				}
			}

			const int32 SEA_LEVEL = 64;
			
			// Calculate which block this column is in (LocalChunkZ 7 = 0-31, LocalChunkZ 6 = 32-63, LocalChunkZ 5 = 64-95, LocalChunkZ 4 = 96-127)
			const int32 ChunkBaseZ = (TERRAIN_END_Z - LocalChunkZ) * CHUNK_SIZE_Z;

				// Fill column with appropriate blocks
				for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
				{
					const int32 AbsoluteZ = ChunkBaseZ + Z;
					EBlockType BlockType = EBlockType::Air;
					
					if (AbsoluteZ < GroundHeight)
					{
						// Below ground: stone
						BlockType = EBlockType::Stone;
					}
					else if (AbsoluteZ == GroundHeight)
					{
						// Ground surface: dry land vs underwater
						if (GroundHeight > SEA_LEVEL)
						{
						// Dry land: Stone peaks (>85), then probabilistic coastal Sand blend, else Grass
						if (GroundHeight > 85)
							BlockType = EBlockType::Stone;
						else
						{
							// Coastal sand blend: continuous jittered proximity to ShapeValue 0.50 threshold
						// CoastalCloseness = 1.0 at coastline, fades to 0.0 when 0.05+ away in ShapeValue (band ~61-64)
						const float CoastalCloseness = 1.0f - FMath::Clamp(FMath::Abs(ShapeValue - 0.50f) / 0.05f, 0.0f, 1.0f);
							const float JitteredCloseness = FMath::Clamp(CoastalCloseness + DetailNoise * 0.25f, 0.0f, 1.0f);
							
							// Per-block random value for probabilistic Sand vs Grass
							const float BlockSalt = CavePerlin2D(WX * 0.15f, WY * 0.15f);
							const float BlockRandom = BlockSalt * 0.5f + 0.5f;  // [-1, 1] → [0, 1]
							
							BlockType = (BlockRandom < JitteredCloseness) ? EBlockType::Sand : EBlockType::Grass;
						}
						}
						else
						{
							// Underwater floor: blend Sand → Gravel with depth jitter
							const float DepthFraction = FMath::Clamp(
								static_cast<float>(SEA_LEVEL - GroundHeight) / static_cast<float>(SEA_LEVEL - 32),
								0.f, 1.f);
							
							const float JitteredFraction = FMath::Clamp(
								DepthFraction + DetailNoise * 0.15f,
								0.f, 1.f);
							
							BlockType = (JitteredFraction > 0.5f) ? EBlockType::Gravel : EBlockType::Sand;
						}
					}
					else if (AbsoluteZ <= SEA_LEVEL)
					{
						// Above ground but at/below sea level: water
						BlockType = EBlockType::Water;
					}
					else
					{
						// Above sea level: air
						BlockType = EBlockType::Air;
					}
					
					OutBlocks[BlockIdx(X, Y, Z)] = BlockType;
				}
			}
		}

		return;
	}

	// Fallback (shouldn't happen): fill with air as safe default
	OutBlocks.Init(EBlockType::Air, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
}

void FPrimordialCavernLevelGenerator::GenerateChunk(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ)
{
	TArray<EBlockType> Blocks;
	GenerateBlocks(GlobalChunkX, GlobalChunkY, LocalChunkZ, Blocks);
	Chunk.Initialize(Blocks);
}