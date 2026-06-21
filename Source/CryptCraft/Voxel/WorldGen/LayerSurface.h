// LayerSurface.h
// Surface layer generator (Level 0, GlobalChunkZ >= 0).
// Procedural Perlin fBm terrain: grass / dirt / stone columns + boulder placement.

#pragma once

#include "CoreMinimal.h"
#include "ILevelGenerator.h"
#include "Voxel/VoxelTypes.h"

class AChunk;

class CRYPTCRAFT_API FSurfaceLevelGenerator : public ILevelGenerator
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

	virtual FString GetLevelName() const override { return TEXT("Surface"); }

	/**
	 * 5-octave fBm height sampler, returns [0, 1].
	 * Public so VoxelWorld can query surface elevation for player spawn.
	 */
	static float SampleHeight(float WorldX, float WorldY);

	// Terrain shape parameters
	static constexpr int32 BASE_HEIGHT  = 50;   // Minimum surface Z in blocks
	static constexpr int32 HEIGHT_RANGE = 30;   // Maximum extra height above base
	static constexpr int32 DIRT_DEPTH   =  4;   // Dirt blocks between grass and stone

private:
	/** Second-pass boulder / object placement. Runs after base terrain fill. */
	static void PlaceSurfaceObjects(FIntVector ChunkCoord, TArray<EBlockType>& InOutBlocks);
};
