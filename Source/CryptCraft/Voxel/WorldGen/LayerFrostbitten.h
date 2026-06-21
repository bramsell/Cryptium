// LayerFrostbitten.h
// Frostbitten layer generator (Level 4, GlobalChunkZ -25 .. -32).
//
// World Z range : -769 to -1024
// Theme         : Frozen stone, ice formations, permafrost, cryo-crystal deposits (planned)
// Planned       : Ice block fills, frost ore, frozen cavern chambers, cryo-structures

#pragma once

#include "CoreMinimal.h"
#include "ILevelGenerator.h"
#include "Voxel/VoxelTypes.h"

class AChunk;

class CRYPTCRAFT_API FFrostbittenLevelGenerator : public ILevelGenerator
{
public:
	virtual void GenerateBlocks(
		int32 GlobalChunkX,
		int32 GlobalChunkY,
		int32 LocalChunkZ,
		TArray<EBlockType>& OutBlocks) override;

	virtual void GenerateChunk(
		AChunk& Chunk,
		int32 GlobalChunkX,
		int32 GlobalChunkY,
		int32 LocalChunkZ) override;

	virtual FString GetLevelName() const override { return TEXT("Frostbitten"); }
};
