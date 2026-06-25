// VoxelWorld.h – Manages chunk loading / unloading around a tracked actor
// (typically the player) and provides the global block-query API that chunks
// use when sampling their neighbors.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelTypes.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WorldGen/WorldGenerationManager.h"
#include "VoxelWorld.generated.h"

class AChunk;

/** How often (seconds) the terrain streaming pass re-evaluates visible chunks. */
static constexpr float VOXEL_STREAM_INTERVAL = 0.5f;

UCLASS()
class CRYPTCRAFT_API AVoxelWorld : public AActor
{
	GENERATED_BODY()

public:
	AVoxelWorld();

	// -----------------------------------------------------------------------
	//  Designer-facing settings
	// -----------------------------------------------------------------------

	/** Controls how the world is generated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|World")
	EWorldGenType WorldGenType = EWorldGenType::Terrain;

	/**
	 * How many chunks to load in each direction (X/Y) around the player.
	 * E.g. 8 → 17×17 area (8 chunks in each direction + the center chunk).
	 * Only used when WorldGenType == Terrain.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|World", meta = (EditCondition = "WorldGenType == EWorldGenType::Terrain"))
	int32 RenderDistance = 8;

	/**
	 * Side length (in chunks) of the flat world grid.
	 * Default 8 → spawns an 8×8 chunk plane (128×128 blocks = 128 m × 128 m).
	 * Only used when WorldGenType == Flat.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|World", meta = (EditCondition = "WorldGenType == EWorldGenType::Flat", ClampMin = "1"))
	int32 FlatExtentChunks = 8;

	/**
	 * Block height of the flat surface (Z voxel index of the grass layer).
	 * Default 8 → grass top is at 900 UE units (9 m).
	 * Only used when WorldGenType == Flat.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|World", meta = (EditCondition = "WorldGenType == EWorldGenType::Flat", ClampMin = "1"))
	int32 FlatSurfaceHeight = 8;

	/**
	 * World generation seed. Used by terrain generation to deterministically
	 * place features like caverns, worms, and procedural structures.
	 * Change this value to get a different world layout while keeping the same map.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Voxel|World")
	uint32 WorldSeed = 54321u;

	/**
	 * Material applied to every chunk section.
	 * Must have a Texture2D parameter named "AtlasTexture" so the runtime
	 * atlas can be injected.  UVs are encoded in vertex colour:
	 *   atlasUV = frac(UV0) * VertexColor.b + VertexColor.rg
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|World")
	TObjectPtr<UMaterialInterface> ChunkMaterial;

	/**
	 * Base content path that is searched automatically for block textures.
	 * Any texture key (e.g. "grass_top") that is NOT already in BlockTextures
	 * will be loaded from  <TextureBasePath>/<key>.<key>  at BeginPlay.
	 * Example: /Game/Textures/Blocks/  →  looks for /Game/Textures/Blocks/grass_top.grass_top
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Textures")
	FString TextureBasePath = TEXT("/Game/Textures/Blocks/");

	/**
	 * Individual block textures.  Key = texture name used in FBlockDefinition
	 * (e.g. "grass_top", "stone").  Value = the imported 16×16 PNG asset.
	 * Each texture must have Compression=VectorDisplacementmap (or UserInterface2D)
	 * so its pixels are CPU-readable at runtime.
	 * Entries here override the auto-load from TextureBasePath.
	 * Leave empty to rely entirely on automatic discovery.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Textures")
	TMap<FString, TObjectPtr<UTexture2D>> BlockTextures;

	/** Static properties for each block type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Blocks")
	TMap<EBlockType, FBlockDefinition> BlockDefinitions;

	/**
	 * Minimum brightness floor for block faces. Prevents completely black faces.
	 * Range: 0.0 (no clamping) to 1.0 (all faces fully bright).
	 * Recommended: 0.30 (30% minimum brightness on any face).
	 * This is applied in the material via a Max(LightingResult, MinBrightness) operation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumBrightness = 0.30f;

	// -----------------------------------------------------------------------
	//  Runtime API
	// -----------------------------------------------------------------------

	/** Returns the block type at the given world-voxel coordinate. */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	EBlockType GetBlockAt(FIntVector WorldVoxelPos) const;

	/** Returns the loaded chunk at the given chunk coordinate, or nullptr if not loaded. */
	AChunk* GetChunkAt(FIntVector ChunkCoord) const;

	/** Sets the block at a world-voxel coordinate and rebuilds affected chunks. */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	void SetBlockAt(FIntVector WorldVoxelPos, EBlockType Type);

	/** Returns static properties for a given block type (safe – returns default if missing). */
	const FBlockDefinition& GetBlockDefinition(EBlockType Type) const;

	/**
	 * Returns the atlas tile index for a named texture.
	 * Returns 0 (first slot) if the name is not registered – renders as the
	 * first texture in the atlas which makes missing textures obvious.
	 */
	int32 GetTileIndex(const FString& TextureName) const;

	/** Number of tiles along one axis of the runtime atlas (computed at BeginPlay). */
	int32 GetAtlasCols() const { return ComputedAtlasCols; }

	/** Texture-name → atlas tile index map; copied into mesh snapshots for background meshing. */
	const TMap<FString, int32>& GetTileIndexMap() const { return TextureToTileIndex; }

	/** Accessor used by chunks to retrieve the (possibly dynamic) chunk material. */
	UFUNCTION(BlueprintPure, Category = "Voxel")
	UMaterialInterface* GetChunkMaterial() const
	{
		return RuntimeChunkMaterial ? RuntimeChunkMaterial.Get() : ChunkMaterial.Get();
	}

	/**
	 * Returns the direction vector from the directional light (sun) in the world.
	 * The vector points in the direction the light is shining FROM.
	 * If no directional light is found, returns (0, 0, -1) as default (downward).
	 * Used for computing per-face brightness in mesh generation.
	 */
	UFUNCTION(BlueprintPure, Category = "Voxel")
	FVector GetSunDirection() const;

	/**
	 * Returns the world-space position a player should spawn at above this
	 * world's surface. Used by the GameMode to place a PlayerStart.
	 */
	UFUNCTION(BlueprintPure, Category = "Voxel")
	FVector GetPlayerSpawnLocation() const;

	/**
	 * Call this every frame (or as often as desired) to stream chunks in/out.
	 * Typically driven by the player pawn's world position.
	 */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	void UpdateStreamingPosition(FVector WorldPosition);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	// -----------------------------------------------------------------------
	//  Internal chunk registry
	// -----------------------------------------------------------------------
	UPROPERTY()
	TMap<FIntVector, TObjectPtr<AChunk>> LoadedChunks;

	// Chunks currently being generated on a background thread.
	// Kept as a UPROPERTY so GC doesn't collect the actor before Initialize() runs.
	UPROPERTY()
	TMap<FIntVector, TObjectPtr<AChunk>> PendingChunkActors;

	FIntVector LastPlayerChunkCoord = FIntVector(INT_MAX, INT_MAX, INT_MAX);
	float      StreamingTimer       = 0.f;

	// -----------------------------------------------------------------------
	//  Coordinate helpers
	// -----------------------------------------------------------------------

	/** World-voxel position → chunk coordinate. */
	static FIntVector WorldVoxelToChunkCoord(FIntVector WorldVoxelPos);

	/** World-voxel position → local voxel position within its chunk [0..CHUNK_SIZE). */
	static FIntVector WorldVoxelToLocalVoxel(FIntVector WorldVoxelPos);

	/** World-space position (UE units) → chunk coordinate. */
	static FIntVector WorldPosToChunkCoord(FVector WorldPos);

	// -----------------------------------------------------------------------
	//  Streaming helpers
	// -----------------------------------------------------------------------
	void LoadChunk(FIntVector Coord);
	void UnloadChunk(FIntVector Coord);

	/** Spawn the entire flat grid at BeginPlay. */
	void LoadFlatWorld();

	/** Fill OutBlocks with flat-plane data (constant surface height). */
	void GenerateFlatChunkData(FIntVector Coord, TArray<EBlockType>& OutBlocks) const;

	/** World generation manager — routes each non-flat chunk to its level generator. */
	TSharedPtr<FWorldGenerationManager> WorldGenManager;

	/**
	 * Defines the vertical layer stack for procedural world generation.
	 * Edit this one method to reorder, add, or remove underground layers —
	 * no other code needs to change. Called once from BeginPlay.
	 */
	void ConfigureLayerStack();

	/** True once the flat grid has been spawned (prevents double-spawn). */
	bool bFlatLoaded = false;

	// -----------------------------------------------------------------------
	//  Runtime atlas
	// -----------------------------------------------------------------------

	/** Built at BeginPlay from BlockTextures.  Injected into RuntimeChunkMaterial. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RuntimeAtlas;

	/** Dynamic material instance with RuntimeAtlas bound to "AtlasTexture". */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RuntimeChunkMaterial;

	/** Texture name → tile index in RuntimeAtlas.  Populated by BuildTextureAtlas(). */
	TMap<FString, int32> TextureToTileIndex;

	/** Width/height of the atlas in tiles (ceil(sqrt(numTextures))). */
	int32 ComputedAtlasCols = 1;

	/**
	 * Packs all BlockTextures into a single UTexture2D atlas, builds the
	 * TextureToTileIndex map, and creates RuntimeChunkMaterial.
	 * Called once at BeginPlay, before any chunks are generated.
	 */
	void BuildTextureAtlas();

	// -----------------------------------------------------------------------
	//  Default block data (fallback when BlockDefinitions map is empty)
	// -----------------------------------------------------------------------
	static const FBlockDefinition DefaultDefinition;
	void EnsureDefaultDefinitions();
};
