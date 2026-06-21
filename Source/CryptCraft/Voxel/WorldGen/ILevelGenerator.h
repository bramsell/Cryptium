// ILevelGenerator.h
// Abstract base class for a single world layer's chunk generation.
//
// Implement this interface to define the block composition, cave systems,
// ore placement, or any other content specific to one vertical layer.
//
// Each instance is registered with FWorldGenerationManager at a level index:
//   Level 0  — Surface      (GlobalChunkZ >= 0)
//   Level 1  — Underground  (GlobalChunkZ -1 .. -8,   world Z -1 .. -256)
//   Level 2  — Deep         (GlobalChunkZ -9 .. -16,  world Z -257 .. -512)
//   Level N  — ...

#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"

class AChunk;

class CRYPTCRAFT_API ILevelGenerator
{
public:
	virtual ~ILevelGenerator() = default;

	/**
	 * Generate and initialize a chunk for this level.
	 *
	 * The chunk actor already has ChunkCoord and VoxelWorld set before this
	 * is called. This function is responsible for calling Chunk.Initialize()
	 * with the generated block data to build the mesh.
	 *
	 * @param Chunk         The chunk to fill. Call Initialize() on it here.
	 * @param GlobalChunkX  World grid X of this chunk.
	 * @param GlobalChunkY  World grid Y of this chunk.
	 * @param LocalChunkZ   Z index within this level (0 = top, 7 = bottom).
	 *                      For Level 0 (surface), this equals GlobalChunkZ.
	 */
	/**
	 * Pure-computation half of generation: fills OutBlocks with the block data
	 * for this chunk.  No UObject access — safe to call from a background thread.
	 *
	 * @param OutBlocks  Receives exactly CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z entries.
	 */
	virtual void GenerateBlocks(
		int32 GlobalChunkX,
		int32 GlobalChunkY,
		int32 LocalChunkZ,
		TArray<EBlockType>& OutBlocks) = 0;

	/**
	 * Convenience wrapper: calls GenerateBlocks then Chunk.Initialize().
	 * Behaviour is identical to the old synchronous path and is still used
	 * by any caller that wants a fully-ready chunk in one call.
	 */
	virtual void GenerateChunk(
		AChunk& Chunk,
		int32 GlobalChunkX,
		int32 GlobalChunkY,
		int32 LocalChunkZ) = 0;

	/** Human-readable name shown in logs and debug output. */
	virtual FString GetLevelName() const = 0;

	/**
	 * Height of this level in chunks (1 chunk = CHUNK_SIZE_Z blocks, typically 32).
	 * Override to declare a non-standard height for this layer.
	 *
	 * Common values:
	 *   2  →  64 blocks  (thin transition layer)
	 *   8  → 256 blocks  (standard underground level, default)
	 *  16  → 512 blocks  (large biome-scale layer)
	 *
	 * The WorldGenerationManager accumulates these depths when routing chunks,
	 * so every layer can have an independent height with no extra configuration.
	 */
	virtual int32 GetDepthInChunks() const { return 8; }
};
