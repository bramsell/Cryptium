// LayerPrimordialCavern.h
// Primordial Cavern layer generator (Level 2, GlobalChunkZ -9 .. -16).
//
// World Z range : -257 to -512
// Theme         : Ancient stone, massive open cavern chambers, ancient structures (planned)
// Planned       : Rare ore veins, giant stalactites/stalagmites, ancient ruins

#pragma once

#include "CoreMinimal.h"
#include "ILevelGenerator.h"
#include "Voxel/VoxelTypes.h"

class AChunk;

class CRYPTCRAFT_API FPrimordialCavernLevelGenerator : public ILevelGenerator
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

	virtual FString GetLevelName() const override { return TEXT("Primordial Cavern"); }
};
