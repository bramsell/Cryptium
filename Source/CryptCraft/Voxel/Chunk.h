// Chunk.h – A CHUNK_SIZE_X × CHUNK_SIZE_Y × CHUNK_SIZE_Z region of voxels.
// Owns its own ProceduralMeshComponent and rebuilds the mesh via greedy meshing
// whenever the block data changes.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelTypes.h"
#include "Chunk.generated.h"

class UProceduralMeshComponent;
class AVoxelWorld;

UCLASS()
class CRYPTCRAFT_API AChunk : public AActor
{
	GENERATED_BODY()

public:
	AChunk();

	// -----------------------------------------------------------------------
	//  Identity
	// -----------------------------------------------------------------------

	/** Integer chunk grid coordinate (not world-space position). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
	FIntVector ChunkCoord;

	/** The owning world – used to query neighbor chunks during face culling. */
	UPROPERTY()
	TObjectPtr<AVoxelWorld> VoxelWorld;

	// -----------------------------------------------------------------------
	//  Block access
	// -----------------------------------------------------------------------

	/** Returns the block at local voxel coordinates.  Out-of-bounds → Air. */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	EBlockType GetBlock(int32 X, int32 Y, int32 Z) const;

	/**
	 * Sets the block at local voxel coordinates and optionally rebuilds the mesh.
	 * Out-of-bounds coordinates are silently ignored.
	 */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	void SetBlock(int32 X, int32 Y, int32 Z, EBlockType Type, bool bRebuildMesh = true);

	// -----------------------------------------------------------------------
	//  Lifecycle
	// -----------------------------------------------------------------------

	/** Populate the chunk with pre-generated block data and build the first mesh. */
	void Initialize(const TArray<EBlockType>& InBlocks);

	/** Rebuild the ProceduralMesh from the current block data. */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	void RebuildMesh();

	/**
	 * When true, collision is cooked synchronously so it is ready the same frame
	 * the mesh is built. Use for flat/pre-loaded worlds where the player spawns
	 * immediately. Leave false for streamed terrain (async is cheaper).
	 */
	bool bUseSyncCollision = false;

private:
	// Flat voxel storage: index = X + CHUNK_SIZE_X * (Y + CHUNK_SIZE_Y * Z)
	TArray<EBlockType> Blocks;

	UPROPERTY(VisibleAnywhere, Category = "Voxel")
	TObjectPtr<UProceduralMeshComponent> ProceduralMesh;

	// -----------------------------------------------------------------------
	//  Mesh building
	// -----------------------------------------------------------------------

	/**
	 * Returns true if the block at (X, Y, Z) occludes adjacent faces.
	 * Queries the owning VoxelWorld for coordinates that fall outside this chunk.
	 */
	bool IsBlockOpaque(int32 X, int32 Y, int32 Z) const;

	/** Returns the block type at local coords, querying the world for out-of-bounds. */
	EBlockType GetBlockWithNeighbors(int32 X, int32 Y, int32 Z) const;

	/**
	 * Greedy meshing: for each of the 6 face directions, sweep slices and merge
	 * adjacent same-type faces into the fewest possible quads.
	 */
	// OutColors encodes the atlas address per vertex:
	//   R = atlas U offset (0..1),  G = atlas V offset (0..1)
	//   B = tile size (1 / AtlasTileCount),  A = 1
	// Material formula:  atlasUV = frac(UV0) * Color.b + Color.rg
	void BuildGreedyMesh(
		TArray<FVector>&       OutVertices,
		TArray<int32>&         OutTriangles,
		TArray<FVector>&       OutNormals,
		TArray<FVector2D>&     OutUVs,
		TArray<FLinearColor>&  OutColors);

	/** Flat array index with bounds check (returns false if out of range). */
	static bool BlockIndex(int32 X, int32 Y, int32 Z, int32& OutIndex);
};
