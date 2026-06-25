#pragma once

#include "CoreMinimal.h"
#include "ILevelGenerator.h"
#include "Voxel/VoxelTypes.h"

class AChunk;

/**
 * FCrystalCavesLevelGenerator - Crystal Cavern Layer
 * 
 * Block layout (256 blocks = 8 chunks, LocalChunkZ 0 = shallowest/top):
 *   LocalChunkZ 0   : Solid stone ceiling                  ( 32 blocks)
 *   LocalChunkZ 1-3 : Top cavern layer (air)              ( 96 blocks)
 *   LocalChunkZ 4-6 : Bottom cavern layer (cobblestone)   ( 96 blocks)
 *   LocalChunkZ 7   : Solid stone floor                   ( 32 blocks)
 */
class CRYPTCRAFT_API FCrystalCavesLevelGenerator : public ILevelGenerator
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

	virtual FString GetLevelName() const override { return TEXT("Crystal Caves"); }
	virtual int32 GetDepthInChunks() const override { return 8; }

	/** Set the world seed for deterministic feature generation */
	void SetWorldSeed(uint32 InWorldSeed) { WorldSeed = InWorldSeed; }

private:
	uint32 WorldSeed = 54321u;  // Default seed, overridden by VoxelWorld

	// Helper functions for noise-based cavern generation
	float SampleUnifiedHeight(int32 X, int32 Z, uint32 FeatureSeedVariant = 0) const;
	EBlockType GenerateNoise2DCavern(
		int32 X, int32 Z, int32 LocalChunkZ, int32 VoxelZ,
		int32 CeilingChunk, int32 FloorChunk, EBlockType SolidBlockType) const;
};
