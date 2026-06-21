// LayerHellscape.cpp
// Hellscape layer generation (Level 3, GlobalChunkZ -17 .. -24, world Z -513 .. -768).

#include "LayerHellscape.h"
#include "LayerBase.h"
#include "Voxel/Chunk.h"

void FHellscapeLevelGenerator::GenerateBlocks(
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ,
	TArray<EBlockType>& OutBlocks)
{
	OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

	for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
	{
		for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
		{
			for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
			{
				// Placeholder: solid stone — replace with scorched / hellstone blocks
				// once new EBlockType entries are added
				OutBlocks[BlockIdx(X, Y, Z)] = EBlockType::Stone;
			}
		}
	}

	// TODO: Add scorched stone / hellstone block variants (EBlockType additions needed)
	// TODO: Add lava pool generation at lower LocalChunkZ slices
	// TODO: Add magma ore deposits
	// TODO: Add fire geyser / hazard structure placement
}

void FHellscapeLevelGenerator::GenerateChunk(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ)
{
	TArray<EBlockType> Blocks;
	GenerateBlocks(GlobalChunkX, GlobalChunkY, LocalChunkZ, Blocks);
	Chunk.Initialize(Blocks);
}
