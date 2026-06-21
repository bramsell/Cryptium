// LayerHellscape.h
// Hellscape layer generator (Level 3, GlobalChunkZ -17 .. -24).
//
// World Z range : -513 to -768
// Theme         : Scorched stone, lava pools, fire geysers, ash drifts (planned)
// Planned       : Lava block fills, magma ore, hellstone, fire-based hazard structures

#pragma once

#include "CoreMinimal.h"
#include "ILevelGenerator.h"
#include "Voxel/VoxelTypes.h"

class AChunk;

class CRYPTCRAFT_API FHellscapeLevelGenerator : public ILevelGenerator
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

	virtual FString GetLevelName() const override { return TEXT("Hellscape"); }
};
