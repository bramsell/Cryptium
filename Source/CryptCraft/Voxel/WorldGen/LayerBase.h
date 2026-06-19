// LayerBase.h
// Shared constants, helpers, and the layer-index calculation used by all
// world generation layers.
//
// World Z layout (in blocks):
//   Surface     :  Z >= 0          (ChunkZ >= 0)
//   Underground :  Z = -1  .. -256 (ChunkZ = -1 .. -8)
//   Deep        :  Z = -257.. -512 (ChunkZ = -9 .. -16)
//   (add more layers below by extending LayerIndex return values)

#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"

// ---------------------------------------------------------------------------
//  Layout constants
// ---------------------------------------------------------------------------

/** Depth of each underground layer in voxel blocks. */
static constexpr int32 LAYER_DEPTH_BLOCKS = 256;

/** Depth of each underground layer in chunks (must divide evenly). */
static constexpr int32 LAYER_DEPTH_CHUNKS = LAYER_DEPTH_BLOCKS / CHUNK_SIZE_Z;   // = 8

// ---------------------------------------------------------------------------
//  Layer index helpers
// ---------------------------------------------------------------------------

/**
 * Maps a ChunkZ coordinate to an underground layer index.
 *
 *  ChunkZ >= 0  → -1  (surface — handled by LayerSurface)
 *  ChunkZ -1..-8  →  0  (Underground layer 1)
 *  ChunkZ -9..-16 →  1  (Deep layer 2)
 *  etc.
 *
 * Add a new layer generator for each new index value.
 */
inline int32 ChunkZToLayerIndex(int32 ChunkZ)
{
	if (ChunkZ >= 0) return -1;                         // Surface
	return (-ChunkZ - 1) / LAYER_DEPTH_CHUNKS;          // 0, 1, 2, …
}

// ---------------------------------------------------------------------------
//  Flat-array index helper
// ---------------------------------------------------------------------------

/** Converts local chunk voxel coordinates to the flat block array index. */
FORCEINLINE int32 BlockIdx(int32 X, int32 Y, int32 Z)
{
	return X + CHUNK_SIZE_X * (Y + CHUNK_SIZE_Y * Z);
}

// ---------------------------------------------------------------------------
//  Deterministic per-chunk hash (used by all layers for object placement)
// ---------------------------------------------------------------------------

inline uint32 LayerChunkHash(FIntVector Coord, uint32 Salt)
{
	uint32 H = (uint32)(Coord.X * 2654435761u)
	         ^ (uint32)(Coord.Y *  805459861u)
	         ^ (uint32)(Coord.Z * 1234567891u)
	         ^ Salt;
	H ^= H >> 16;
	H *= 0x45d9f3bu;
	H ^= H >> 16;
	return H;
}

// ---------------------------------------------------------------------------
//  Shared Perlin noise (used by multiple layer generators)
// ---------------------------------------------------------------------------

// Standard Ken Perlin reference permutation table, doubled to 512.
static const uint8 CavePerm[512] =
{
	151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
	140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
	247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
	 57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
	 74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
	 60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
	 65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
	200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
	 52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
	207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
	119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
	129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
	218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
	 81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
	184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
	222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180,
	// repeat
	151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
	140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
	247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
	 57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
	 74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
	 60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
	 65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
	200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
	 52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
	207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
	119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
	129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
	218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
	 81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
	184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
	222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180,
};

static FORCEINLINE float CavePerlinFade(float T)
{
	return T * T * T * (T * (T * 6.f - 15.f) + 10.f);
}

static FORCEINLINE float CavePerlinGrad2(uint8 Hash, float X, float Y)
{
	switch (Hash & 7)
	{
		case 0: return  X + Y;
		case 1: return -X + Y;
		case 2: return  X - Y;
		case 3: return -X - Y;
		case 4: return  X;
		case 5: return -X;
		case 6: return  Y;
		default: return -Y;
	}
}

static float CavePerlin2D(float X, float Y)
{
	const int32 IX = FMath::FloorToInt(X) & 255;
	const int32 IY = FMath::FloorToInt(Y) & 255;
	const float FX = X - FMath::FloorToInt(X);
	const float FY = Y - FMath::FloorToInt(Y);

	const float UX = CavePerlinFade(FX);
	const float UY = CavePerlinFade(FY);

	const uint8 A = CavePerm[IX    ] + IY;
	const uint8 B = CavePerm[IX + 1] + IY;

	return FMath::Lerp(
		FMath::Lerp(CavePerlinGrad2(CavePerm[A    ], FX,       FY      ),
		            CavePerlinGrad2(CavePerm[B    ], FX - 1.f, FY      ), UX),
		FMath::Lerp(CavePerlinGrad2(CavePerm[A + 1], FX,       FY - 1.f),
		            CavePerlinGrad2(CavePerm[B + 1], FX - 1.f, FY - 1.f), UX),
		UY
	);
}

// ---------------------------------------------------------------------------
//  Shared zone indices (LocalChunkZ constants for layer generators)
// ---------------------------------------------------------------------------

inline constexpr int32 CEILING_SOLID_Z  = 0;
inline constexpr int32 CEILING_FRINGE_Z = 1;
