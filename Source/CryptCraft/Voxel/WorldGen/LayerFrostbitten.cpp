// LayerFrostbitten.cpp
// Frostbitten layer generation (Level 4, GlobalChunkZ -25 .. -32, world Z -769 .. -1024).

#include "LayerFrostbitten.h"
#include "LayerBase.h"
#include "Voxel/Chunk.h"

void FFrostbittenLevelGenerator::GenerateBlocks(
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
				// Placeholder: solid stone — replace with ice / frozen stone blocks
				// once new EBlockType entries are added
				OutBlocks[BlockIdx(X, Y, Z)] = EBlockType::Stone;
			}
		}
	}

	// TODO: Add ice / frozen stone block variants (EBlockType additions needed)
	// TODO: Add cryo-crystal ore deposits
	// TODO: Add frozen cavern chamber carving
	// TODO: Add cryo-structure / permafrost formation placement
}

void FFrostbittenLevelGenerator::GenerateChunk(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ)
{
	TArray<EBlockType> Blocks;
	GenerateBlocks(GlobalChunkX, GlobalChunkY, LocalChunkZ, Blocks);
	Chunk.Initialize(Blocks);
}
