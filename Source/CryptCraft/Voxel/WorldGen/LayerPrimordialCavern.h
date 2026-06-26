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

// Biome classification for Primordial Cavern layer (layer-prefixed to avoid collisions with other layers)
enum class EPrimordialBiomeType : uint8
{
	PrimordialPlains,
	PrimordialForest,
	PrimordialMountains,
	// Room for future: PrimordialVolcano, PrimordialProtoaxiteField, etc.
	Ocean,  // Not a biome, but a classification for water columns
	MAX
};

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

	// Public utility functions for biome queries (debug/testing)
	/**
	 * Query the biome at a world position.
	 * Runs through the same pipeline as terrain generation (continent noise, remap, coast jitter, vegetation).
	 * Can be called from anywhere to determine what biome the player is standing in.
	 */
	static EPrimordialBiomeType QueryBiomeAtWorldPosition(float WorldX, float WorldY);

	/**
	 * Convert biome enum to display string.
	 */
	static FString GetBiomeDisplayName(EPrimordialBiomeType Biome);

	/**
	 * Debug structure containing terrain generation intermediate values.
	 */
	struct FTerrainDebugInfo
	{
		float ContinentNoise = 0.f;    // Raw Perlin output [-1, 1]
		float ShapeValue = 0.f;        // After RemapShapeCurve [0, 1]
		int32 EstimatedHeight = 0;     // Estimated ground height in blocks
		EPrimordialBiomeType Biome = EPrimordialBiomeType::Ocean;
		FString BiomeName;
		float MountainMask = 0.f;      // Mountain region gate [0, 1] (for mountains only)
		float PeakNoise = 0.f;         // Peak detail bonus [0, 1] (for mountains only)
		float HeightBoost = 0.f;       // Final mountain height boost in blocks (for mountains only)
	};

	/**
	 * Query detailed terrain debug information at a world position.
	 * Returns intermediate noise values for analysis.
	 */
	static FTerrainDebugInfo QueryTerrainDebugInfo(float BlockX, float BlockY);

	/**
	 * Grid scan for terrain analysis across a large area.
	 * Searches for highest ShapeValue (mountain regions) in a grid pattern.
	 * Logs peaks with coordinates for easy travel/teleport.
	 */
	static void GridScanForPeaks(float CenterBlockX, float CenterBlockY, float GridSizeBlocks, float StepBlocks);

	/**
	 * Debug utility: sample MountainMask raw-to-smoothstep transformation at 20 scattered points.
	 * Logs both raw Perlin values and final smoothstep outputs to see actual mapping behavior.
	 */
	static void DebugMountainMaskDistribution(float CenterBlockX, float CenterBlockY, float GridSizeBlocks);
};
