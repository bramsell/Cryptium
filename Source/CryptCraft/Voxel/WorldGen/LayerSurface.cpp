// LayerSurface.cpp
// Surface terrain generation: Perlin fBm height-map + boulder placement.

#include "LayerSurface.h"
#include "LayerBase.h"
#include "Voxel/Chunk.h"

// ---------------------------------------------------------------------------
//  Perlin noise implementation
// ---------------------------------------------------------------------------

// Fixed 256-entry permutation table (doubled to 512 to avoid modulo in lookup).
static const uint8 SurfPerm[512] =
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

static FORCEINLINE float PerlinFade(float T)
{
	return T * T * T * (T * (T * 6.f - 15.f) + 10.f);
}

static FORCEINLINE float PerlinGrad2(uint8 Hash, float X, float Y)
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
		case 7: return -Y;
		default: return 0.f;
	}
}

static float Perlin2D(float X, float Y)
{
	const int32 IX = FMath::FloorToInt(X) & 255;
	const int32 IY = FMath::FloorToInt(Y) & 255;
	const float FX = X - FMath::FloorToInt(X);
	const float FY = Y - FMath::FloorToInt(Y);

	const float UX = PerlinFade(FX);
	const float UY = PerlinFade(FY);

	const uint8 A  = SurfPerm[IX    ] + IY;
	const uint8 B  = SurfPerm[IX + 1] + IY;

	return FMath::Lerp(
		FMath::Lerp(PerlinGrad2(SurfPerm[A    ], FX,       FY      ),
		            PerlinGrad2(SurfPerm[B    ], FX - 1.f, FY      ), UX),
		FMath::Lerp(PerlinGrad2(SurfPerm[A + 1], FX,       FY - 1.f),
		            PerlinGrad2(SurfPerm[B + 1], FX - 1.f, FY - 1.f), UX),
		UY
	);
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

float FSurfaceLevelGenerator::SampleHeight(float WX, float WY)
{
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 128.f;
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 5; ++Oct)
	{
		Value += Perlin2D(WX * Freq, WY * Freq) * Amp;
		Total += Amp;
		Amp   *= 0.5f;
		Freq  *= 2.f;
	}

	return (Value / Total) * 0.5f + 0.5f;   // remap -1..1 → 0..1
}

void FSurfaceLevelGenerator::GenerateBlocks(
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ,
	TArray<EBlockType>& OutBlocks)
{
	// For Level 0 (surface), LocalChunkZ == GlobalChunkZ — no transformation needed.
	const FIntVector ChunkCoord(GlobalChunkX, GlobalChunkY, LocalChunkZ);

	OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

	const int32 DirtTopOffset = 1;          // grass is 1 above dirt band
	const int32 DirtBottom    = -DIRT_DEPTH;

	for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
	{
		for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
		{
			const float WX = static_cast<float>(ChunkCoord.X * CHUNK_SIZE_X + X);
			const float WY = static_cast<float>(ChunkCoord.Y * CHUNK_SIZE_Y + Y);

			const float Noise    = SampleHeight(WX, WY);
			const int32 SurfaceZ = BASE_HEIGHT + FMath::RoundToInt(Noise * static_cast<float>(HEIGHT_RANGE));
			const int32 DirtMinZ = SurfaceZ - DIRT_DEPTH;

			for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
			{
				const int32 WorldZ = LocalChunkZ * CHUNK_SIZE_Z + Z;
				EBlockType Type;

				if      (WorldZ > SurfaceZ)   Type = EBlockType::Air;
				else if (WorldZ == SurfaceZ)  Type = EBlockType::Grass;
				else if (WorldZ >= DirtMinZ)  Type = EBlockType::Dirt;
				else                          Type = EBlockType::Stone;

				OutBlocks[BlockIdx(X, Y, Z)] = Type;
			}
		}
	}

	PlaceSurfaceObjects(ChunkCoord, OutBlocks);
}

void FSurfaceLevelGenerator::GenerateChunk(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ)
{
	TArray<EBlockType> Blocks;
	GenerateBlocks(GlobalChunkX, GlobalChunkY, LocalChunkZ, Blocks);
	Chunk.Initialize(Blocks);
}

// ---------------------------------------------------------------------------
//  Surface object placement
// ---------------------------------------------------------------------------

static int32 FindSurfaceZ(const TArray<EBlockType>& Blocks, int32 X, int32 Y)
{
	for (int32 Z = CHUNK_SIZE_Z - 1; Z >= 0; --Z)
	{
		if (Blocks[BlockIdx(X, Y, Z)] != EBlockType::Air) return Z;
	}
	return -1;
}

static void PlaceBlock(TArray<EBlockType>& Blocks, int32 X, int32 Y, int32 Z, EBlockType Type)
{
	if (X < 0 || X >= CHUNK_SIZE_X ||
	    Y < 0 || Y >= CHUNK_SIZE_Y ||
	    Z < 0 || Z >= CHUNK_SIZE_Z) return;
	Blocks[BlockIdx(X, Y, Z)] = Type;
}

void FSurfaceLevelGenerator::PlaceSurfaceObjects(FIntVector ChunkCoord, TArray<EBlockType>& InOutBlocks)
{
	// -----------------------------------------------------------------------
	//  Boulder pass  (~1 per 8 terrain chunks)
	// -----------------------------------------------------------------------
	if ((LayerChunkHash(ChunkCoord, 0xBEEF1234u) % 8) != 0) return;

	const int32 CX = 4 + (int32)(LayerChunkHash(ChunkCoord, 0xDEADBEEFu) % 8);
	const int32 CY = 4 + (int32)(LayerChunkHash(ChunkCoord, 0xCAFEBABEu) % 8);

	const int32 SurfZ = FindSurfaceZ(InOutBlocks, CX, CY);
	if (SurfZ < 0) return;

	const float BaseRadius = 1.5f + (float)(LayerChunkHash(ChunkCoord, 0x12345678u) % 101) * 0.01f;
	const float ScaleX     = 0.80f + (float)(LayerChunkHash(ChunkCoord, 0xAABBCCDDu) % 41) * 0.01f;
	const float ScaleY     = 0.80f + (float)(LayerChunkHash(ChunkCoord, 0x11223344u) % 41) * 0.01f;
	const float ScaleZ     = 0.70f + (float)(LayerChunkHash(ChunkCoord, 0x55667788u) % 41) * 0.01f;
	const float CZ         = (float)SurfZ + BaseRadius * 0.70f;

	const int32 IRadius = FMath::CeilToInt(BaseRadius) + 1;

	for (int32 dz = -IRadius; dz <= IRadius; ++dz)
	for (int32 dy = -IRadius; dy <= IRadius; ++dy)
	for (int32 dx = -IRadius; dx <= IRadius; ++dx)
	{
		const float nx = (float)dx / (BaseRadius * ScaleX);
		const float ny = (float)dy / (BaseRadius * ScaleY);
		const float nz = (float)dz / (BaseRadius * ScaleZ);

		const uint32 PH = LayerChunkHash(
			FIntVector(CX + dx, CY + dy, SurfZ + dz), 0x99887766u);
		const float Perturb = (float)(PH % 21) * 0.01f - 0.10f;

		if ((nx*nx + ny*ny + nz*nz) <= (1.0f + Perturb))
		{
			PlaceBlock(InOutBlocks, CX + dx, CY + dy,
			           FMath::RoundToInt(CZ) + dz, EBlockType::Stone);
		}
	}

	// -----------------------------------------------------------------------
	//  Add more surface object types here (trees, ruins, chests, etc.)
	// -----------------------------------------------------------------------
}
