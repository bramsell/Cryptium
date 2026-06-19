// LayerCrystalCaves.cpp
// Crystal Caves layer generation (Level 1, GlobalChunkZ -1 .. -8, world Z -1 .. -256).
//
// Block layout (256 blocks = 8 chunks, LocalChunkZ 0 = shallowest):
//   LocalChunkZ 0   : Solid stone ceiling              ( 32 blocks)
//   LocalChunkZ 1   : Ceiling fringe — Perlin stalactites, up to 32 blocks deep
//   LocalChunkZ 2–5 : Pure open air void               (128 blocks)
//   LocalChunkZ 6   : Floor fringe   — Perlin stalagmites, up to 32 blocks tall
//   LocalChunkZ 7   : Solid stone floor                ( 32 blocks)
//
// Within every chunk, local block Z=31 is the shallowest face (highest world Z)
// and Z=0 is the deepest face (most negative world Z).

#include "LayerCrystalCaves.h"
#include "LayerBase.h"
#include "Voxel/Chunk.h"

// Perlin noise utility functions are now in LayerBase.h

// fBm over 3 octaves; result clamped to [0, 1].
// Sampling with a large offset so the cave pattern is independent of surface terrain.
static float SampleCaveNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 10000.5f;
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

// ---------------------------------------------------------------------------
//  Zone indices  (LocalChunkZ within the Crystal Caves layer, 0 = shallowest)
// ---------------------------------------------------------------------------

// CEILING_SOLID_Z and CEILING_FRINGE_Z are now in LayerBase.h
// LocalChunkZ 2–5 : pure air (CEILING_FRINGE_Z+1 .. FLOOR_FRINGE_Z-1)
static constexpr int32 FLOOR_FRINGE_Z   = 6;
static constexpr int32 FLOOR_SOLID_Z    = 7;

// ---------------------------------------------------------------------------
//  GenerateChunk
// ---------------------------------------------------------------------------

void FCrystalCavesLevelGenerator::GenerateChunk(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ)
{
	TArray<EBlockType> OutBlocks;

	// ---- Solid stone ceiling and floor ----------------------------------------
	if (LocalChunkZ == CEILING_SOLID_Z || LocalChunkZ == FLOOR_SOLID_Z)
	{
		OutBlocks.Init(EBlockType::Stone, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		Chunk.Initialize(OutBlocks);
		return;
	}

	// ---- Pure open air (LocalChunkZ 2..5) -------------------------------------
	if (LocalChunkZ > CEILING_FRINGE_Z && LocalChunkZ < FLOOR_FRINGE_Z)
	{
		OutBlocks.Init(EBlockType::Air, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		Chunk.Initialize(OutBlocks);
		return;
	}

	// ---- Fringe chunks (ceiling or floor) -------------------------------------
	OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

	const bool bCeiling = (LocalChunkZ == CEILING_FRINGE_Z);

	for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
	{
		for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
		{
			const float WX = static_cast<float>(GlobalChunkX * CHUNK_SIZE_X + X);
			const float WY = static_cast<float>(GlobalChunkY * CHUNK_SIZE_Y + Y);

			// How many blocks of stone this column contributes (0..32).
			const int32 FringeBlocks = FMath::RoundToInt(SampleCaveNoise(WX, WY) * CHUNK_SIZE_Z);

			for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
			{
				EBlockType Type;

				if (bCeiling)
				{
					// Z=31 is the shallowest face, adjacent to solid ceiling.
					// Stone hangs DOWN: fill the top FringeBlocks of the chunk.
					Type = (Z >= CHUNK_SIZE_Z - FringeBlocks) ? EBlockType::Stone : EBlockType::Air;
				}
				else
				{
					// Z=0 is the deepest face, adjacent to solid floor.
					// Stone rises UP: fill the bottom FringeBlocks of the chunk.
					Type = (Z < FringeBlocks) ? EBlockType::Stone : EBlockType::Air;
				}

				OutBlocks[BlockIdx(X, Y, Z)] = Type;
			}
		}
	}

	Chunk.Initialize(OutBlocks);
}
