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

// ---------------------------------------------------------------------------
//  Snapshot of all data required to run BuildGreedyMesh on a background thread.
//  Constructed on the game thread; contains no UObject pointers, safe to read
//  from any thread.
// ---------------------------------------------------------------------------
struct FChunkMeshSnapshot
{
	// Own chunk's block data
	TArray<EBlockType> OwnBlocks;          // empty when bOwnIsUniform == true
	bool       bOwnIsUniform   = false;
	EBlockType OwnUniformType  = EBlockType::Air;

	// One face slice from each of the 6 face-adjacent neighbors.
	// Ordering: [0]=+X, [1]=-X, [2]=+Y, [3]=-Y, [4]=+Z, [5]=-Z
	// An empty array means the neighbor is not loaded — all its blocks treated as Air.
	TArray<EBlockType> NeighborFaces[6];

	// World rendering constants — value-copied, no live UObject references.
	TMap<EBlockType, FBlockDefinition> BlockDefs;
	TMap<FString, int32>               TileIndexMap;
	int32   AtlasCols         = 1;
	FVector SunDirection      = FVector(0.f, 0.f, 1.f);
	float   MinimumBrightness = 0.3f;
};

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
	// Flat voxel storage: index = X + CHUNK_SIZE_X * (Y + CHUNK_SIZE_Y * Z).
	// Empty when bIsUniform == true.
	TArray<EBlockType> Blocks;

	// Uniform-chunk optimization: when all 32768 blocks are identical,
	// Blocks is freed and UniformBlockType holds the single value.
	bool       bIsUniform       = false;
	EBlockType UniformBlockType = EBlockType::Air;

	/** Expand Blocks back to a full per-block array before the first write to a uniform chunk. */
	void DeUniformify();

	UPROPERTY(VisibleAnywhere, Category = "Voxel")
	TObjectPtr<UProceduralMeshComponent> ProceduralMesh;

	// -----------------------------------------------------------------------
	//  Mesh building
	// -----------------------------------------------------------------------

	/** Snapshot all game-thread data needed for meshing into a self-contained struct. */
	FChunkMeshSnapshot BuildMeshSnapshot() const;

	/**
	 * Pure function: builds mesh geometry from a snapshot with no live UObject access.
	 * Safe to call from any thread.
	 */
	static void BuildGreedyMeshFromSnapshot(
		const FChunkMeshSnapshot& Snap,
		TArray<FVector>&          OutVertices,
		TArray<int32>&            OutTriangles,
		TArray<FVector>&          OutNormals,
		TArray<FVector2D>&        OutUVs,
		TArray<FLinearColor>&     OutColors);

	/** Flat array index with bounds check (returns false if out of range). */
	static bool BlockIndex(int32 X, int32 Y, int32 Z, int32& OutIndex);
};
