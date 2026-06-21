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
	 * Uses noise-based pattern.
	 *
	 * @param WorldSeed     The world's seed
	 * @param WorldX        World X coordinate (in blocks)
	 * @param WorldZ        World Z coordinate (in blocks)
	 * @return              Upper or Lower layer for this column
	 */
	ECavernLayer GetLayerForColumn(uint32 WorldSeed, int32 WorldX, int32 WorldZ);

	/**
	 * Determine which cavern layer is active for a given grid CELL coordinate.
	 * Uses discrete checkerboard pattern: adjacent cells always have different layers.
	 * This prevents caverns from stacking directly on top of each other.
	 *
	 * @param CellX         Coarse grid X coordinate (cell index, not world blocks)
	 * @param CellZ         Coarse grid Z coordinate (cell index)
	 * @return              Upper or Lower layer for this cell (deterministic: same cell always returns same layer)
	 */
	ECavernLayer GetLayerForCell(int32 CellX, int32 CellZ);

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
	 * Cached version of GetBubbleAtCell.
	 * Uses a runtime hash map (GBubbleCellCache) to avoid redundant noise computation.
	 * Safe to call multiple times for the same cell — subsequent calls are O(1) lookups.
	 *
	 * @param WorldSeed     The world's seed
	 * @param CellX         Coarse grid X (cell coordinate, not world block coordinate)
	 * @param CellZ         Coarse grid Z (cell coordinate)
	 * @param UpperConfig   Configuration for upper cavern layer
	 * @param LowerConfig   Configuration for lower cavern layer
	 * @return              Bubble info (bExists=false if no bubble at this cell)
	 */
	FCavernBubble GetBubbleAtCellCached(
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
//  Tunnel/Worm Path System (Step 3)
// -----------------------------------------------------------------------

/**
 * Result of a worm spawn query at a coarse grid cell.
 * Indicates whether a worm tunnel spawns at this cell, and its properties.
 */
struct FWormSpawn
{
	/** Does this cell have a worm tunnel? */
	bool bExists = false;

	/** Starting world position of the worm (only valid if bExists=true). */
	FVector StartPosition = FVector::ZeroVector;

	/** Starting direction (normalized) for the worm path (only valid if bExists=true). */
	FVector StartDirection = FVector(1.0f, 0.0f, 0.0f);

	/** Unique seed for this worm's path generation (only valid if bExists=true). */
	uint32 WormSeed = 0;

	/** Deterministically chosen target cavern for steering (only valid if bExists=true). */
	FCavernBubble TargetCavern;
};

namespace TunnelWorms
{
	/**
	 * Coarse worm-spawn grid cell size (blocks).
	 * Each cell can have at most one worm spawning from it.
	 * Larger = fewer, longer worms; smaller = more, shorter worms.
	 */
	static constexpr int32 WORM_GRID_CELL_SIZE = 256;

	/**
	 * Query whether a worm tunnel spawns at a coarse grid cell.
	 * Returns full spawn info if a worm is placed there.
	 *
	 * @param WorldSeed     The world's seed
	 * @param CellX         Coarse grid X (cell coordinate, not world block coordinate)
	 * @param CellZ         Coarse grid Z (cell coordinate)
	 * @param UpperConfig   Configuration for upper cavern layer
	 * @param LowerConfig   Configuration for lower cavern layer
	 * @return              Worm spawn info (bExists=false if no worm at this cell)
	 */
	FWormSpawn GetWormAtCell(
		uint32 WorldSeed,
		int32 CellX,
		int32 CellZ,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig);

	/**
	 * Cached version of GetWormAtCell.
	 * Uses a runtime hash map (GWormCellCache) to avoid redundant noise computation.
	 * Safe to call multiple times for the same cell — subsequent calls are O(1) lookups.
	 *
	 * @param WorldSeed     The world's seed
	 * @param CellX         Coarse grid X (cell coordinate, not world block coordinate)
	 * @param CellZ         Coarse grid Z (cell coordinate)
	 * @param UpperConfig   Configuration for upper cavern layer
	 * @param LowerConfig   Configuration for lower cavern layer
	 * @return              Worm spawn info (bExists=false if no worm at this cell)
	 */
	FWormSpawn GetWormAtCellCached(
		uint32 WorldSeed,
		int32 CellX,
		int32 CellZ,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig);

	/**
	 * Calculate the worm's direction at a specific step along its path.
	 * Blends wander noise (random walk) with steering toward target cavern.
	 * Steering weight increases as worm approaches target.
	 *
	 * @param WormSeed      Unique seed for this worm
	 * @param Step          Current step along the path (0 = start)
	 * @param TargetCenter  World position of target cavern center
	 * @param CurrentPos    Current position of the worm (for distance to target)
	 * @param SteerWeight   How much to blend toward steering (0=wander only, 1=aim directly at target)
	 * @return              Normalized direction vector
	 */
	FVector GetWormDirectionAtStep(
		uint32 WormSeed,
		int32 Step,
		FVector TargetCenter,
		FVector CurrentPos,
		float SteerWeight = 0.3f);

	/**
	 * Calculate the worm's position at a specific step along its path.
	 * Accumulates direction steps from the worm's starting position.
	 * Fully deterministic: same inputs always produce same output.
	 *
	 * @param WormSeed      Unique seed for this worm
	 * @param Step          Step number along the path (0 = start position)
	 * @param StartPos      Starting world position of the worm
	 * @param StartDir      Starting normalized direction
	 * @param TargetCenter  Target cavern center (for steering)
	 * @param StepLength    How many blocks per step to advance (default ~1.0)
	 * @param MaxSteps      Stop accumulation at this step (safety limit)
	 * @return              World position at the given step
	 */
	FVector GetWormPositionAtStep(
		uint32 WormSeed,
		int32 Step,
		FVector StartPos,
		FVector StartDir,
		FVector TargetCenter,
		float StepLength = 1.0f,
		int32 MaxSteps = 1000);

	/**
	 * Get or build the full path for a worm tunnel.
	 * Pre-computes all steps from start to max length, caches result.
	 * Subsequent calls with the same worm return cached path (O(1)).
	 * Safe to call once per chunk during GenerateChunk, then reuse for all voxel distance checks.
	 *
	 * @param Worm          The worm spawn info (must have bExists=true and valid TargetCavern)
	 * @return              Reference to cached TArray<FVector> of path points
	 */
	const TArray<FVector>& GetOrBuildWormPath(const FWormSpawn& Worm);

	/**
	 * Debug visualization: render worm tunnel paths over a large area.
	 *
	 * @param CenterX        World X to center visualization around
	 * @param CenterZ        World Z to center visualization around
	 * @param AreaSize       Size of area to visualize (in blocks)
	 * @param WorldSeed      World seed to use
	 * @param UpperConfig    Upper layer config
	 * @param LowerConfig    Lower layer config
	 */
	void DebugVisualizeWorms(
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
	 * @param PreFetchedCaverns  Array of nearby caverns (pre-computed once per chunk, not per-voxel)
	 * @return               true = air (carved), false = stone
	 */
	bool IsCavernVoxel(
		int32 WorldSeed,
		FIntVector WorldPos,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig,
		const TArray<FCavernBubble>& PreFetchedCaverns);
}

// -----------------------------------------------------------------------
//  Crystal Caves Layer Generator
// -----------------------------------------------------------------------

class CRYPTCRAFT_API FCrystalCavesLevelGenerator : public ILevelGenerator
{
public:
	virtual void GenerateChunk(
		AChunk& Chunk,
		int32 GlobalChunkX,
		int32 GlobalChunkY,
		int32 LocalChunkZ) override;

	virtual FString GetLevelName() const override { return TEXT("Crystal Caves"); }
};
