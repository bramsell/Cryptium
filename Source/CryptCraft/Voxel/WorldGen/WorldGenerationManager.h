// WorldGenerationManager.h
// Routes chunk generation requests to the correct ILevelGenerator based on
// the chunk's global Z coordinate.
//
// Canonical layer stack. Each underground level declares its own height via
// ILevelGenerator::GetDepthInChunks() (default 8 chunks = 256 blocks).
// Boundaries are computed automatically when levels are registered, so each
// layer can have a completely independent height.
//
//   Index 0  Surface           GlobalChunkZ >= 0         (unbounded upward)
//   Index 1  Crystal Caves     8 chunks  = 256 blocks    (world Z    -1 ..  -256)
//   Index 2  Primordial Cavern 8 chunks  = 256 blocks    (world Z  -257 ..  -512)
//   Index 3  Hellscape         8 chunks  = 256 blocks    (world Z  -513 ..  -768)
//   Index 4  Frostbitten       8 chunks  = 256 blocks    (world Z  -769 .. -1024)
//
// A layer that overrides GetDepthInChunks() automatically shifts all deeper
// levels without any changes to this file or VoxelWorld.
//
// Unregistered levels receive solid bedrock as a safe fallback.

#pragma once

#include "CoreMinimal.h"
#include "ILevelGenerator.h"

class AChunk;

class CRYPTCRAFT_API FWorldGenerationManager
{
public:
	FWorldGenerationManager()  = default;
	~FWorldGenerationManager() = default;

	// Non-copyable — owns TSharedPtr generators
	FWorldGenerationManager(const FWorldGenerationManager&)            = delete;
	FWorldGenerationManager& operator=(const FWorldGenerationManager&) = delete;

	// -----------------------------------------------------------------------
	//  Registration
	// -----------------------------------------------------------------------

	/**
	 * Register a generator at the given level index.
	 * Level 0 = surface, Level 1 = first underground layer, etc.
	 * Replaces any existing generator at that index.
	 */
	void RegisterLevel(int32 LevelIndex, TSharedPtr<ILevelGenerator> Generator);

	// -----------------------------------------------------------------------
	//  Routing
	// -----------------------------------------------------------------------

	/**
	 * Background-thread-safe routing: resolves GlobalChunkZ to a generator and
	 * calls GenerateBlocks() to fill OutBlocks.  Does NOT call Initialize().
	 * Safe to call from any thread — no UObject access.
	 */
	void RouteBlockGeneration(
		int32 GlobalChunkX,
		int32 GlobalChunkY,
		int32 GlobalChunkZ,
		TArray<EBlockType>& OutBlocks) const;

	/**
	 * Identify which level owns GlobalChunkZ, compute LocalChunkZ, and call
	 * that level's GenerateChunk(). The chunk must already have ChunkCoord
	 * and VoxelWorld set; Initialize() is called inside the generator.
	 *
	 * @param Chunk         Chunk with ChunkCoord and VoxelWorld already set.
	 * @param GlobalChunkX  World grid X.
	 * @param GlobalChunkY  World grid Y.
	 * @param GlobalChunkZ  World grid Z (negative = underground).
	 */
	void RouteChunkGeneration(
		AChunk& Chunk,
		int32 GlobalChunkX,
		int32 GlobalChunkY,
		int32 GlobalChunkZ) const;

	// -----------------------------------------------------------------------
	//  Routing math (instance methods — use registered level depths)
	// -----------------------------------------------------------------------

	/**
	 * Returns the level index that owns GlobalChunkZ.
	 * Respects each registered level's GetDepthInChunks(), so layers of
	 * different heights are handled correctly.
	 */
	int32 GetLevelIndex(int32 GlobalChunkZ) const;

	/**
	 * Returns the LocalChunkZ within its level (0 = top/shallowest chunk).
	 * Derived from the accumulated depths of all levels above it.
	 */
	int32 GetLocalChunkZ(int32 GlobalChunkZ) const;

	/** Number of registered levels. */
	int32 GetLevelCount() const { return Levels.Num(); }

private:
	/** Generators indexed by level. Index 0 = surface, 1 = underground, etc. */
	TArray<TSharedPtr<ILevelGenerator>> Levels;

	/**
	 * Walks the registered level stack to resolve a GlobalChunkZ into a level
	 * index and LocalChunkZ. Each level's GetDepthInChunks() determines how
	 * many chunks it consumes before the next level begins.
	 *
	 * Returns true if a valid registered generator owns this chunk.
	 * Returns false (and sets OutLevelIndex = Levels.Num()) for bedrock territory.
	 */
	bool ResolveChunkZ(int32 GlobalChunkZ, int32& OutLevelIndex, int32& OutLocalChunkZ) const;
};
