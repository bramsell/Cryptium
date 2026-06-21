// LayerPrimordialCavern.cpp
// Primordial Cavern layer generation (Level 2, GlobalChunkZ -9 .. -16, world Z -257 .. -512).
//
// Block layout (256 blocks = 8 chunks, LocalChunkZ 0 = shallowest):
//   LocalChunkZ 0   : Solid stone ceiling                                ( 32 blocks)
//   LocalChunkZ 1   : Ceiling fringe — Perlin stalactites, up to 32 blocks deep
//   LocalChunkZ 2–5 : Pure open air void                                 (128 blocks)
//   LocalChunkZ 6–7 : Land/Water terrain based on 2-octave Perlin        ( 64 blocks)

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
	float Freq  = 1.f / 300.f;
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
// Control points: (input, output)
//   (-1.0, 0.03) — deep ocean abyss
//   (-0.4, 0.08) — shallow ocean
//   (-0.15, 0.45) — continental shelf
//   ( 0.0,  0.50) — coastline / sea level
//   ( 0.20, 0.52) — low coastal plain
//   ( 0.35, 0.55) — end of plains / foothills start
//   ( 0.50, 0.68) — foothills
//   ( 0.70, 0.88) — mid hills
//   ( 1.0,  1.00) — mountain peaks
static float RemapShapeCurve(float t)
{
	struct FControlPoint { float In; float Out; };
	static constexpr FControlPoint Points[] = {
		{ -1.00f, 0.03f },
		{ -0.40f, 0.08f },
		{ -0.15f, 0.45f },
		{  0.00f, 0.50f },
		{  0.20f, 0.52f },
		{  0.35f, 0.55f },
		{  0.50f, 0.68f },
		{  0.70f, 0.88f },
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

// ---------------------------------------------------------------------------
//  Zone indices  (LocalChunkZ within the Primordial Cavern layer, 0 = shallowest)
// ---------------------------------------------------------------------------

// CEILING_SOLID_Z and CEILING_FRINGE_Z are now in LayerBase.h
static constexpr int32 AIR_VOID_START_Z    = 2;
static constexpr int32 AIR_VOID_END_Z      = 4;   // 3 chunks of air (LocalChunkZ 2-4)
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

	// ---- Pure air void middle section --------------------------------
	if (LocalChunkZ >= AIR_VOID_START_Z && LocalChunkZ <= AIR_VOID_END_Z)
	{
		OutBlocks.Init(EBlockType::Air, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
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

				// Debug: track noise range across this chunk to confirm hill inputs are reachable
				static float DbgMin =  1.f, DbgMax = -1.f;
				static int32 DbgCount = 0;
				if (ContinentNoise < DbgMin) DbgMin = ContinentNoise;
				if (ContinentNoise > DbgMax) DbgMax = ContinentNoise;
				if (++DbgCount >= 1024)
				{
					UE_LOG(LogTemp, Warning, TEXT("[PrimordialCavern] ContinentNoise range over last 1024 columns: min=%.3f  max=%.3f"), DbgMin, DbgMax);
					DbgMin = 1.f; DbgMax = -1.f; DbgCount = 0;
				}

				// 2. Remap through spline control points to get base height fraction
				const float ShapeValue = RemapShapeCurve(ContinentNoise);

				// 3. Detail amplitude varies by terrain zone:
				//    deep ocean / flat plains = low ripple, hills = more roughness
				float DetailAmplitude;
				if (ShapeValue < 0.55f)
					DetailAmplitude = 0.02f;   // ocean / coastal plains
				else if (ShapeValue < 0.65f)
					DetailAmplitude = 0.06f;   // plains / foothills transition
				else
					DetailAmplitude = 0.35f;   // hilly / mountain range

				// 4. Blend detail noise in
				const float DetailNoise  = SampleDetailNoise(WX, WY);  // [-1, 1]
				const float FinalShape   = FMath::Clamp(ShapeValue + DetailNoise * DetailAmplitude, 0.f, 1.f);

				// 5. Map [0, 1] → world height [32, 95]
				// Capped at 95 so the grass surface block always lands inside the top terrain chunk (LocalChunkZ 5, Z 64-95).
				const int32 GroundHeight = FMath::Clamp(32 + FMath::RoundToInt(FinalShape * 63.f), 32, 95);
				const int32 SEA_LEVEL = 64;
				
				// Calculate which block this column is in (LocalChunkZ 6 = 32-63, LocalChunkZ 5 = 64-95)
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
						// Ground surface: sand near sea level (beach band), grass elsewhere
						BlockType = (GroundHeight >= 60 && GroundHeight <= 68) ? EBlockType::Sand : EBlockType::Grass;
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

	// Fallback (shouldn't happen)
	OutBlocks.Init(EBlockType::Stone, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
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