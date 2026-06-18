// VoxelTypes.h – Shared constants, enums, and structs for the voxel system.

#pragma once

#include "CoreMinimal.h"
#include "VoxelTypes.generated.h"

// ---------------------------------------------------------------------------
//  Chunk dimensions  (width × depth × height in blocks)
// ---------------------------------------------------------------------------
static constexpr int32 CHUNK_SIZE_X  = 32;
static constexpr int32 CHUNK_SIZE_Y  = 32;
static constexpr int32 CHUNK_SIZE_Z  = 32;
static constexpr float BLOCK_SIZE    = 100.f;  // Unreal units per block (exactly 1 m; 1 UU = 1 cm)

// ---------------------------------------------------------------------------
//  World generation mode
// ---------------------------------------------------------------------------
UENUM(BlueprintType)
enum class EWorldGenType : uint8
{
	/** Classic procedural height-map terrain that streams around the player. */
	Terrain = 0  UMETA(DisplayName = "Terrain"),

	/**
	 * Spawns a fixed flat grid of chunks (FlatExtentChunks × FlatExtentChunks).
	 * No streaming – good for building / testing.
	 */
	Flat    = 1  UMETA(DisplayName = "Flat"),
};

// ---------------------------------------------------------------------------
//  Block types
// ---------------------------------------------------------------------------
UENUM(BlueprintType)
enum class EBlockType : uint8
{
	Air			= 0  UMETA(DisplayName = "Air"),
	Grass		= 1  UMETA(DisplayName = "Grass"),
	Dirt		= 2  UMETA(DisplayName = "Dirt"),
	Stone		= 3  UMETA(DisplayName = "Stone"),
	Sand		= 4  UMETA(DisplayName = "Sand"),
	Gravel		= 5   UMETA(DisplayName = "Gravel"),

    Water       = 6  UMETA(DisplayName = "Water"),
    Lava        = 7  UMETA(DisplayName = "Lava"),
	Bedrock     = 8  UMETA(DisplayName = "Bedrock"),

	OakLog		= 9  UMETA(DisplayName = "Oak Log"),
	OakLeaves	= 10  UMETA(DisplayName = "Oak Leaves"),
	OakPlanks	= 11  UMETA(DisplayName = "Oak Planks"),
	BirchLog	= 12  UMETA(DisplayName = "Birch Log"),
	BirchLeaves	= 13  UMETA(DisplayName = "Birch Leaves"),
	BirchPlanks	= 14  UMETA(DisplayName = "Birch Planks"),
	SpruseLog	= 15  UMETA(DisplayName = "Spruse Log"),
	SpruseLeaves	= 16  UMETA(DisplayName = "Spruse Leaves"),
	SprusePlanks	= 17  UMETA(DisplayName = "Spruse Planks"),
	
    Cobblestone = 18  UMETA(DisplayName = "Cobblestone"),
    Glass       = 19  UMETA(DisplayName = "Glass"),

    CoalOre      = 20  UMETA(DisplayName = "Coal Ore"),
    CopperOre    = 21  UMETA(DisplayName = "Copper Ore"),
    IronOre      = 22  UMETA(DisplayName = "Iron Ore"),
    SilverOre    = 23  UMETA(DisplayName = "Silver Ore"),
    GoldOre      = 24  UMETA(DisplayName = "Gold Ore"),
    PlatinumOre  = 25  UMETA(DisplayName = "Platinum Ore"),
    
    JadeOre      = 26  UMETA(DisplayName = "Jade Ore"),
    RubyOre      = 27  UMETA(DisplayName = "Ruby Ore"),
    EmeraldOre   = 28  UMETA(DisplayName = "Emerald Ore"),
    SapphireOre  = 29  UMETA(DisplayName = "Sapphire Ore"),
    DiamondOre   = 30  UMETA(DisplayName = "Diamond Ore"),

    MythrilOre   = 31  UMETA(DisplayName = "Mythril Ore"),
};

/** Total number of block types (kept outside the enum so it doesn't pollute TMap key serialization). */
static constexpr int32 BLOCK_TYPE_COUNT = 32;

// ---------------------------------------------------------------------------
//  Per-block static properties – configured on AVoxelWorld
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct CRYPTCRAFT_API FBlockDefinition
{
	GENERATED_BODY()

	/** Fallback tint used when no atlas texture is available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	FLinearColor Color = FLinearColor::White;

	/** Fully opaque = no transparency; used by the face-culling pass */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	bool bIsOpaque = true;

	/** Whether physics/player can collide with this block */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	bool bIsSolid = true;

	// -------------------------------------------------------------------
	//  Per-face texture names.
	//  Each name is a key into AVoxelWorld::BlockTextures.
	//  Top    = face whose normal points in +Z  (e.g. "grass_top")
	//  Side   = any ±X or ±Y face               (e.g. "grass_side")
	//  Bottom = face whose normal points in −Z  (e.g. "dirt")
	//  Leave a field empty ("") to share the same texture as Side.
	// -------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Textures")
	FString TextureTop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Textures")
	FString TextureSide;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Textures")
	FString TextureBottom;
};
