// LayerCrystalCaves.h
// Crystal Caves layer generator (Level 1, GlobalChunkZ -1 .. -8).
//
// World Z range : -1 to -256
// Theme         : Crystal formations, luminous stone, underground rivers (planned)
// Planned       : Crystal ore deposits, glowing stone variants, cave pools
//
// Cavern Layout (Step 1):
//  - Determines cavern bubble placement via coarse 2D layout noise
//  - Vertical layers assigned via staggered checkerboard to prevent stacking
//  - One cavern per (X,Z) column, alternating upper/lower layers

#pragma once

#include "CoreMinimal.h"
#include "ILevelGenerator.h"
#include "Voxel/VoxelTypes.h"

class AChunk;

// -----------------------------------------------------------------------
//  Cavern Layer & Placement Configuration
// -----------------------------------------------------------------------

/** Enum for cavern vertical layer indices within Crystal Caves. */
UENUM(BlueprintType)
enum class ECavernLayer : uint8
{
	Upper = 0  UMETA(DisplayName = "Upper Cavern Layer"),
	Lower = 1  UMETA(DisplayName = "Lower Cavern Layer"),
};

/**
 * Configuration for a cavern vertical layer.
 * Defines the Y-range, layout noise threshold, and associated tunnel altitude.
 */
struct FCavernLayerConfig
{
	/** Minimum world Z (blocks) for this layer's cavern interiors. */
	int32 MinZ = 0;

	/** Maximum world Z (blocks) for this layer's cavern interiors. */
	int32 MaxZ = 64;

	/**
	 * Layout noise threshold (0..1) for determining if a cavern spawns in a region.
	 * If layout noise > threshold, a cavern bubble is placed here.
	 * Higher threshold = sparser caverns.
	 */
	float LayoutThreshold = 0.5f;

	/**
	 * Average altitude for tunnel paths within this layer (used by Step 3).
	 * Tunnels will wander around this altitude.
	 */
	int32 TunnelBaseAltitude = 32;
};

/**
 * Cached result of a cavern bubble placement query.
 * Indicates whether a bubble exists at a grid cell, and if so, its properties.
 */
struct FCavernBubble
{
	/** Does this cell have a cavern bubble? */
	bool bExists = false;

	/** Which layer (Upper/Lower) this bubble occupies (only valid if bExists=true). */
	ECavernLayer Layer = ECavernLayer::Upper;

	/** World-space center of the bubble (only valid if bExists=true). */
	FVector Center = FVector::ZeroVector;

	/** Approximate radius of the bubble in blocks (only valid if bExists=true). */
	float Radius = 0.f;
};

// -----------------------------------------------------------------------
//  Cavern Layout Queries (Step 1)
// -----------------------------------------------------------------------

namespace CavernLayout
{
	/**
	 * Determine which cavern layer is active for a given (X, Z) column.
	 * Uses staggered checkerboard pattern to prevent vertical stacking.
	 *
	 * @param WorldSeed     The world's seed
	 * @param WorldX        World X coordinate (in blocks)
	 * @param WorldZ        World Z coordinate (in blocks)
	 * @return              Upper or Lower layer for this column
	 */
	ECavernLayer GetLayerForColumn(uint32 WorldSeed, int32 WorldX, int32 WorldZ);

	/**
	 * Query whether a cavern bubble exists at a coarse grid cell.
	 * Returns full bubble info if one is placed there.
	 *
	 * @param WorldSeed     The world's seed
	 * @param CellX         Coarse grid X (cell coordinate, not world block coordinate)
	 * @param CellZ         Coarse grid Z (cell coordinate)
	 * @param UpperConfig   Configuration for upper cavern layer
	 * @param LowerConfig   Configuration for lower cavern layer
	 * @return              Bubble info (bExists=false if no bubble at this cell)
	 */
	FCavernBubble GetBubbleAtCell(
		uint32 WorldSeed,
		int32 CellX,
		int32 CellZ,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig);

	/**
	 * Find all cavern bubbles near a world position (within search radius).
	 * Used by Step 2 (cavern carving) to determine if a voxel is inside any bubble.
	 *
	 * @param WorldSeed      The world's seed
	 * @param WorldPos       World position to query around
	 * @param SearchRadius   Search radius in blocks
	 * @param UpperConfig    Configuration for upper layer
	 * @param LowerConfig    Configuration for lower layer
	 * @param OutBubbles     Array to receive nearby bubbles
	 */
	void FindNearbyCaverns(
		uint32 WorldSeed,
		FVector WorldPos,
		float SearchRadius,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig,
		TArray<FCavernBubble>& OutBubbles);

	/**
	 * Debug visualization: render cavern bubble placement over a large area.
	 *
	 * @param CenterX        World X to center visualization around
	 * @param CenterZ        World Z to center visualization around
	 * @param AreaSize       Size of area to visualize (in blocks)
	 * @param WorldSeed      World seed to use
	 * @param UpperConfig    Upper layer config
	 * @param LowerConfig    Lower layer config
	 */
	void DebugVisualizeBubbles(
		int32 CenterX,
		int32 CenterZ,
		int32 AreaSize,
		uint32 WorldSeed,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig);
}

// -----------------------------------------------------------------------
//  Cavern Shape Carving (Step 2)
// -----------------------------------------------------------------------

namespace CavernShape
{
	/**
	 * Determine if a world voxel should be carved as cavern air or left as stone.
	 * Uses 3D density noise within bubble boundaries to create organic shapes.
	 *
	 * Algorithm:
	 * 1. Get layer for (X, Z) column
	 * 2. Check if Y is within that layer's range
	 * 3. Find nearby cavern bubbles
	 * 4. For each bubble within radius, evaluate 3D density noise
	 * 5. If density > threshold, return true (this voxel is air)
	 *
	 * @param WorldSeed      The world's seed
	 * @param WorldPos       World voxel position in blocks
	 * @param UpperConfig    Configuration for upper cavern layer
	 * @param LowerConfig    Configuration for lower cavern layer
	 * @return               true = air (carved), false = stone
	 */
	bool IsCavernVoxel(
		int32 WorldSeed,
		FIntVector WorldPos,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig);
}

// -----------------------------------------------------------------------
//  Crystal Caves Layer Generator
// -----------------------------------------------------------------------

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
};
