// VoxelGenLayers.h – Layer-based world generation (3D underground with vertical streaming)
//
// Manages generation of a vertically-stratified world with:
// - Multiple layers (surface → deep underground)
// - Layer-specific terrain and material composition
// - Ore distribution by depth
// - Objects (boulders, veins, caverns, etc.) per layer

#pragma once

#include "CoreMinimal.h"
#include "VoxelTypes.h"

// Forward declarations
class AVoxelWorld;

// ---------------------------------------------------------------------------
//  Layer configuration
// ---------------------------------------------------------------------------

/**
 * Describes a single underground/surface layer.
 * Layers are indexed 0..N going downward (0 = surface, 1 = first below, etc.).
 */
struct FLayerDefinition
{
	/** Human-readable layer name (e.g., "Surface", "Shallow Underground", "Deep Caverns") */
	FString Name;

	/** Height in voxel blocks (e.g., 128 for a full chunk height) */
	int32 Height = CHUNK_SIZE_Z;

	/** Primary terrain block type for this layer (e.g., Grass→Dirt→Stone) */
	EBlockType PrimaryBlock = EBlockType::Stone;

	/** Secondary filler block (used for variation, alternate layers, etc.) */
	EBlockType SecondaryBlock = EBlockType::Stone;

	/** Percentage [0..100] of secondary blocks mixed in */
	float SecondaryPercent = 10.f;

	/** If true, this layer generates caves/caverns (future feature) */
	bool bHasCaverns = false;

	/** If true, this layer generates ore veins (future feature) */
	bool bHasOres = false;
};

// ---------------------------------------------------------------------------
//  Ore vein configuration (reserved for future use)
// ---------------------------------------------------------------------------

struct FOreVeinDef
{
	/** Ore block type to generate */
	EBlockType OreType = EBlockType::CoalOre;

	/** Approximate percentage of blocks in this layer that are ore */
	float Abundance = 1.f;

	/** Vein size (larger = bigger clusters) */
	int32 VeinSize = 8;

	/** Random seed offset for this ore type */
	uint32 SeedOffset = 0;
};

// ---------------------------------------------------------------------------
//  Layer generation utility class
// ---------------------------------------------------------------------------

class FVoxelGenLayers
{
public:
	/**
	 * Initialize layer definitions for the given world.
	 * Called once at world startup; creates the default layer stack.
	 */
	static void InitializeLayerDefinitions(TArray<FLayerDefinition>& OutLayers);

	/**
	 * Generate chunk data for a specific layer coordinate.
	 * Fills OutBlocks with terrain for chunk at ChunkCoord (including Z).
	 * 
	 * @param ChunkCoord  Chunk coordinate (X, Y, Z where Z is the layer index)
	 * @param OutBlocks   Output block array (must be pre-allocated to CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z)
	 * @param Layers      Layer definitions (from InitializeLayerDefinitions)
	 */
	static void GenerateLayeredChunkData(
		FIntVector ChunkCoord,
		TArray<EBlockType>& OutBlocks,
		const TArray<FLayerDefinition>& Layers);

	/**
	 * Determine the block type at a specific voxel within a layer chunk.
	 * Takes into account local noise, ore distribution, and layer rules.
	 *
	 * @param LocalX/Y/Z  Position within the chunk [0..CHUNK_SIZE)
	 * @param GlobalX/Y   World voxel coordinate (used for terrain noise)
	 * @param LayerIndex  Which layer this chunk belongs to (0=surface, 1+=underground)
	 * @param Layers      Layer definitions
	 * @return            Block type for this voxel
	 */
	static EBlockType GetBlockForLayerVoxel(
		int32 LocalX, int32 LocalY, int32 LocalZ,
		float GlobalX, float GlobalY,
		int32 LayerIndex,
		const TArray<FLayerDefinition>& Layers);

	/**
	 * Place objects (boulders, ore veins, etc.) in the layer chunk after base terrain fill.
	 * Modifies OutBlocks in-place.
	 *
	 * @param ChunkCoord  Chunk coordinate (used for deterministic placement)
	 * @param LayerIndex  Layer this chunk belongs to
	 * @param OutBlocks   Block array to modify
	 * @param Layers      Layer definitions
	 */
	static void PlaceLayerObjects(
		FIntVector ChunkCoord,
		int32 LayerIndex,
		TArray<EBlockType>& InOutBlocks,
		const TArray<FLayerDefinition>& Layers);

	/**
	 * Compute the voxel-space Z coordinate of the boundary between two layers.
	 * Used for terrain slope generation so layers blend smoothly.
	 *
	 * @param LayerIndex  Which layer to find the top boundary of
	 * @param WorldX/Y    World XY position (may affect height variation)
	 * @param Layers      Layer definitions
	 * @return            Absolute Z coordinate of layer boundary (in voxel space)
	 */
	static int32 GetLayerBoundaryZ(
		int32 LayerIndex,
		float WorldX, float WorldY,
		const TArray<FLayerDefinition>& Layers);

	/**
	 * Determine which layer index contains a given world voxel Z coordinate.
	 * Returns the layer index that "owns" that Z.
	 *
	 * @param WorldZ      Absolute voxel Z coordinate
	 * @param Layers      Layer definitions
	 * @return            Layer index (0=surface, 1+=underground, -1 if above all layers)
	 */
	static int32 GetLayerIndexForZ(int32 WorldZ, const TArray<FLayerDefinition>& Layers);
};
