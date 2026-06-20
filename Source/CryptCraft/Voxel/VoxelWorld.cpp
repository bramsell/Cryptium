// VoxelWorld.cpp

#include "VoxelWorld.h"
#include "Chunk.h"
#include "Engine/Texture2D.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TextureResource.h"
#include "EngineUtils.h"
#include "WorldGen/LayerSurface.h"         // FSurfaceLevelGenerator
#include "WorldGen/LayerCrystalCaves.h"      // FCrystalCavesLevelGenerator
#include "WorldGen/LayerPrimordialCavern.h"             // FPrimordialCavernLevelGenerator
#include "WorldGen/LayerHellscape.h"        // FHellscapeLevelGenerator
#include "WorldGen/LayerFrostbitten.h"      // FFrostbittenLevelGenerator

// ---------------------------------------------------------------------------
//  Default block definitions (used when the designer hasn't set any in-editor)
// ---------------------------------------------------------------------------
const FBlockDefinition AVoxelWorld::DefaultDefinition = {};

AVoxelWorld::AVoxelWorld()
{
	PrimaryActorTick.bCanEverTick = true;
}

// ---------------------------------------------------------------------------
//  BeginPlay – seed default definitions and do an initial streaming update
// ---------------------------------------------------------------------------
void AVoxelWorld::BeginPlay()
{
	Super::BeginPlay();
	EnsureDefaultDefinitions();
	
	// Auto-load default chunk material if not set
	if (!ChunkMaterial)
	{
		ChunkMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_VoxelChunk"));
	}
	
	BuildTextureAtlas();   // pack individual textures → runtime atlas before any chunks spawn

	// Build the world generation stack.
	WorldGenManager = MakeShared<FWorldGenerationManager>();
	ConfigureLayerStack();  // Edit ConfigureLayerStack() to reorder, add, or remove layers

	if (WorldGenType == EWorldGenType::Flat)
	{
		LoadFlatWorld();
	}
	else
	{
		// Pre-load chunks around the world origin immediately so the player
		// has solid ground to land on when they are teleported in BeginPlay.
		// Without this, the first streaming pass doesn't fire until
		// VOXEL_STREAM_INTERVAL seconds into Tick, which is too late.
		UpdateStreamingPosition(FVector::ZeroVector);
	}
}

// ---------------------------------------------------------------------------
//  Layer stack configuration
//  *** Edit this function to reorder, add, or remove underground layers. ***
//
//  Rules:
//    - Level 0 must always be the surface generator.
//    - Levels are stacked top-down; index N sits directly below index N-1.
//    - Each generator declares its own depth via GetDepthInChunks()
//      (default 8 chunks = 256 blocks). Change the override there, not here.
//    - To insert a new layer between two existing ones, renumber those below it.
//    - To swap two layers, just swap their RegisterLevel indices.
//
//  Current stack:
//    0  Surface
//    1  Crystal Caves      (  -1 ..  -256 )
//    2  Primordial Cavern  ( -257 ..  -512 )
//    3  Hellscape          ( -513 ..  -768 )
//    4  Frostbitten        ( -769 .. -1024 )
// ---------------------------------------------------------------------------
void AVoxelWorld::ConfigureLayerStack()
{
	WorldGenManager->RegisterLevel(0, MakeShared<FSurfaceLevelGenerator>());
	WorldGenManager->RegisterLevel(1, MakeShared<FCrystalCavesLevelGenerator>());
	WorldGenManager->RegisterLevel(2, MakeShared<FPrimordialCavernLevelGenerator>());
	WorldGenManager->RegisterLevel(3, MakeShared<FHellscapeLevelGenerator>());
	WorldGenManager->RegisterLevel(4, MakeShared<FFrostbittenLevelGenerator>());
}

void AVoxelWorld::EnsureDefaultDefinitions()
{
	// Rebuild the map from scratch every BeginPlay so that hot-reload / Live
	// Coding patches don't leave stale USTRUCT layout data in the TMap.
	// Designer overrides can be set via a child Blueprint's BeginPlay AFTER
	// calling Super::BeginPlay(), which runs this function first.
	BlockDefinitions.Empty();

	auto AddBlock = [&](EBlockType Type, FLinearColor Color,
	                    FString TexTop, FString TexSide, FString TexBottom,
	                    bool bOpaque = true, bool bSolid = true)
	{
		if (!BlockDefinitions.Contains(Type))
		{
			FBlockDefinition Def;
			Def.Color         = Color;
			Def.bIsOpaque     = bOpaque;
			Def.bIsSolid      = bSolid;
			Def.TextureTop    = MoveTemp(TexTop);
			Def.TextureSide   = MoveTemp(TexSide);
			Def.TextureBottom = MoveTemp(TexBottom);
			BlockDefinitions.Add(Type, MoveTemp(Def));
		}
	};

	// Air has no texture
	if (!BlockDefinitions.Contains(EBlockType::Air))
	{
		FBlockDefinition Air;
		Air.Color     = FLinearColor(0.f, 0.f, 0.f, 0.f);
		Air.bIsOpaque = false;
		Air.bIsSolid  = false;
		BlockDefinitions.Add(EBlockType::Air, Air);
	}

	// Default texture name convention: add matching PNGs with these keys to BlockTextures.
	//  Block                   Color (fallback)                         Top                   Side                  Bottom                  Opaque  Solid
	AddBlock(EBlockType::Grass,         FLinearColor(0.29f,0.56f,0.16f),        TEXT("grass_top"),      TEXT("grass_side"),     TEXT("dirt"));
	AddBlock(EBlockType::Dirt,          FLinearColor(0.42f,0.27f,0.13f),        TEXT("dirt"),           TEXT("dirt"),           TEXT("dirt"));
	AddBlock(EBlockType::Stone,         FLinearColor(0.50f,0.50f,0.50f),        TEXT("stone"),          TEXT("stone"),          TEXT("stone"));
	AddBlock(EBlockType::Sand,          FLinearColor(0.87f,0.80f,0.55f),        TEXT("sand"),           TEXT("sand"),           TEXT("sand"));
	AddBlock(EBlockType::Gravel,        FLinearColor(0.45f,0.42f,0.38f),        TEXT("gravel"),         TEXT("gravel"),         TEXT("gravel"));
	AddBlock(EBlockType::Water,         FLinearColor(0.10f,0.30f,0.80f,0.50f),  TEXT("water"),          TEXT("water"),          TEXT("water"),          false, false);
	AddBlock(EBlockType::Lava,          FLinearColor(0.90f,0.35f,0.05f,0.80f),  TEXT("lava"),           TEXT("lava"),           TEXT("lava"),           false, false);
	AddBlock(EBlockType::Bedrock,       FLinearColor(0.15f,0.15f,0.15f),        TEXT("bedrock"),        TEXT("bedrock"),        TEXT("bedrock"));
	// Oak  (oaklog_end = top/bottom cap face, oaklog_side = bark)
	AddBlock(EBlockType::OakLog,        FLinearColor(0.40f,0.27f,0.12f),        TEXT("oaklog_end"),     TEXT("oaklog_side"),    TEXT("oaklog_end"));
	AddBlock(EBlockType::OakLeaves,     FLinearColor(0.15f,0.45f,0.10f),        TEXT(""),               TEXT(""),               TEXT(""),               false, false);  // no texture yet
	AddBlock(EBlockType::OakPlanks,     FLinearColor(0.60f,0.45f,0.25f),        TEXT("oakplanks"),      TEXT("oakplanks"),      TEXT("oakplanks"));
	// Birch  (no textures on disk yet – will render with fallback colour)
	AddBlock(EBlockType::BirchLog,      FLinearColor(0.80f,0.75f,0.60f),        TEXT(""),               TEXT(""),               TEXT(""));
	AddBlock(EBlockType::BirchLeaves,   FLinearColor(0.40f,0.65f,0.30f),        TEXT(""),               TEXT(""),               TEXT(""),               false, false);
	AddBlock(EBlockType::BirchPlanks,   FLinearColor(0.80f,0.72f,0.50f),        TEXT(""),               TEXT(""),               TEXT(""));
	// Spruse  (no textures on disk yet – will render with fallback colour)
	AddBlock(EBlockType::SpruseLog,     FLinearColor(0.30f,0.18f,0.08f),        TEXT(""),               TEXT(""),               TEXT(""));
	AddBlock(EBlockType::SpruseLeaves,  FLinearColor(0.08f,0.28f,0.08f),        TEXT(""),               TEXT(""),               TEXT(""),              false, false);
	AddBlock(EBlockType::SprusePlanks,  FLinearColor(0.50f,0.32f,0.15f),        TEXT(""),               TEXT(""),               TEXT(""));
	// Decorative / structural
	AddBlock(EBlockType::Cobblestone,   FLinearColor(0.45f,0.45f,0.45f),        TEXT("cobblestone"),    TEXT("cobblestone"),    TEXT("cobblestone"));
	AddBlock(EBlockType::Glass,         FLinearColor(0.70f,0.85f,0.95f,0.30f),  TEXT("glass"),          TEXT("glass"),          TEXT("glass"),          false, true);
	// Ship system
	AddBlock(EBlockType::ShipController, FLinearColor(0.45f,0.45f,0.45f),        TEXT("cobblestone"),    TEXT("cobblestone"),    TEXT("cobblestone"));
	// Ores – keys match the actual .uasset filenames on disk
	AddBlock(EBlockType::CoalOre,       FLinearColor(0.32f,0.32f,0.32f),        TEXT("coalore"),        TEXT("coalore"),        TEXT("coalore"));
	AddBlock(EBlockType::CopperOre,     FLinearColor(0.50f,0.36f,0.28f),        TEXT(""),               TEXT(""),               TEXT(""));               // no texture yet
	AddBlock(EBlockType::IronOre,       FLinearColor(0.52f,0.42f,0.38f),        TEXT(""),               TEXT(""),               TEXT(""));               // no texture yet
	AddBlock(EBlockType::SilverOre,     FLinearColor(0.70f,0.70f,0.72f),        TEXT("silverore"),      TEXT("silverore"),      TEXT("silverore"));
	AddBlock(EBlockType::GoldOre,       FLinearColor(0.55f,0.50f,0.22f),        TEXT("goldore"),        TEXT("goldore"),        TEXT("goldore"));
	AddBlock(EBlockType::PlatinumOre,   FLinearColor(0.65f,0.68f,0.72f),        TEXT("platinumore"),    TEXT("platinumore"),    TEXT("platinumore"));
	// Gem ores – bright colors for visibility/glow effect in caves
	AddBlock(EBlockType::JadeOre,       FLinearColor(0.45f,0.75f,0.55f),        TEXT("jade"),           TEXT("jade"),           TEXT("jade"));
	AddBlock(EBlockType::RubyOre,       FLinearColor(0.95f,0.20f,0.20f),        TEXT("rubyore"),        TEXT("rubyore"),        TEXT("rubyore"));
	AddBlock(EBlockType::EmeraldOre,    FLinearColor(0.20f,0.85f,0.35f),        TEXT(""),               TEXT(""),               TEXT(""));               // no texture yet
	AddBlock(EBlockType::SapphireOre,   FLinearColor(0.25f,0.50f,0.95f),        TEXT(""),               TEXT(""),               TEXT(""));               // no texture yet
	AddBlock(EBlockType::DiamondOre,    FLinearColor(0.55f,0.85f,0.95f),        TEXT("diamondore"),     TEXT("diamondore"),     TEXT("diamondore"));
	AddBlock(EBlockType::MythrilOre,    FLinearColor(0.32f,0.62f,0.70f),        TEXT("mythrilore"),     TEXT("mythrilore"),     TEXT("mythrilore"));
}

// ---------------------------------------------------------------------------
//  Tick – stream chunks every VOXEL_STREAM_INTERVAL seconds
// ---------------------------------------------------------------------------
void AVoxelWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Flat worlds are fully loaded at BeginPlay – no streaming needed.
	if (WorldGenType == EWorldGenType::Flat) return;

	StreamingTimer += DeltaTime;
	if (StreamingTimer < VOXEL_STREAM_INTERVAL) return;
	StreamingTimer = 0.f;

	// Find the local player pawn
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			UpdateStreamingPosition(Pawn->GetActorLocation());
		}
	}
}

// ---------------------------------------------------------------------------
//  Coordinate helpers
// ---------------------------------------------------------------------------

FIntVector AVoxelWorld::WorldVoxelToChunkCoord(FIntVector WV)
{
	// Integer floor division (handles negatives correctly)
	auto FloorDiv = [](int32 A, int32 B) -> int32
	{
		return A / B - (A % B != 0 && (A ^ B) < 0 ? 1 : 0);
	};
	return FIntVector(
		FloorDiv(WV.X, CHUNK_SIZE_X),
		FloorDiv(WV.Y, CHUNK_SIZE_Y),
		FloorDiv(WV.Z, CHUNK_SIZE_Z)
	);
}

FIntVector AVoxelWorld::WorldVoxelToLocalVoxel(FIntVector WV)
{
	FIntVector CC = WorldVoxelToChunkCoord(WV);
	return FIntVector(
		WV.X - CC.X * CHUNK_SIZE_X,
		WV.Y - CC.Y * CHUNK_SIZE_Y,
		WV.Z - CC.Z * CHUNK_SIZE_Z
	);
}

FIntVector AVoxelWorld::WorldPosToChunkCoord(FVector WorldPos)
{
	// Convert UE units → voxel coordinates, then to chunk coordinates
	FIntVector WV(
		FMath::FloorToInt(WorldPos.X / BLOCK_SIZE),
		FMath::FloorToInt(WorldPos.Y / BLOCK_SIZE),
		FMath::FloorToInt(WorldPos.Z / BLOCK_SIZE)
	);
	return WorldVoxelToChunkCoord(WV);
}

// ---------------------------------------------------------------------------
//  Block API
// ---------------------------------------------------------------------------

EBlockType AVoxelWorld::GetBlockAt(FIntVector WorldVoxelPos) const
{
	FIntVector CC = WorldVoxelToChunkCoord(WorldVoxelPos);
	const TObjectPtr<AChunk>* Found = LoadedChunks.Find(CC);
	if (!Found || !(*Found)) return EBlockType::Air;

	FIntVector LV = WorldVoxelToLocalVoxel(WorldVoxelPos);
	return (*Found)->GetBlock(LV.X, LV.Y, LV.Z);
}

void AVoxelWorld::SetBlockAt(FIntVector WorldVoxelPos, EBlockType Type)
{
	FIntVector CC = WorldVoxelToChunkCoord(WorldVoxelPos);
	TObjectPtr<AChunk>* Found = LoadedChunks.Find(CC);
	if (!Found || !(*Found)) return;

	FIntVector LV    = WorldVoxelToLocalVoxel(WorldVoxelPos);
	AChunk*    Chunk = Found->Get();
	Chunk->SetBlock(LV.X, LV.Y, LV.Z, Type, /*bRebuildMesh=*/true);

	// Rebuild neighbor chunks if the modified block sits on a boundary
	auto RebuildNeighbor = [&](FIntVector NeighborCoord)
	{
		TObjectPtr<AChunk>* N = LoadedChunks.Find(NeighborCoord);
		if (N && *N) (*N)->RebuildMesh();
	};

	if (LV.X == 0)               RebuildNeighbor(CC + FIntVector(-1, 0, 0));
	if (LV.X == CHUNK_SIZE_X - 1) RebuildNeighbor(CC + FIntVector( 1, 0, 0));
	if (LV.Y == 0)               RebuildNeighbor(CC + FIntVector( 0,-1, 0));
	if (LV.Y == CHUNK_SIZE_Y - 1) RebuildNeighbor(CC + FIntVector( 0, 1, 0));
	if (LV.Z == 0)               RebuildNeighbor(CC + FIntVector( 0, 0,-1));
	if (LV.Z == CHUNK_SIZE_Z - 1) RebuildNeighbor(CC + FIntVector( 0, 0, 1));
}

const FBlockDefinition& AVoxelWorld::GetBlockDefinition(EBlockType Type) const
{
	const FBlockDefinition* Def = BlockDefinitions.Find(Type);
	return Def ? *Def : DefaultDefinition;
}

int32 AVoxelWorld::GetTileIndex(const FString& TextureName) const
{
	if (TextureName.IsEmpty()) return 0;
	const int32* Found = TextureToTileIndex.Find(TextureName);
	return Found ? *Found : 0;
}

// ---------------------------------------------------------------------------
//  Runtime atlas builder
// ---------------------------------------------------------------------------

void AVoxelWorld::BuildTextureAtlas()
{
	// ----------------------------------------------------------------
	//  1. Collect every unique texture name referenced by block defs.
	// ----------------------------------------------------------------
	TArray<FString> UniqueNames;
	for (const auto& Pair : BlockDefinitions)
	{
		const FBlockDefinition& Def = Pair.Value;
		if (!Def.TextureTop.IsEmpty())    UniqueNames.AddUnique(Def.TextureTop);
		if (!Def.TextureSide.IsEmpty())   UniqueNames.AddUnique(Def.TextureSide);
		if (!Def.TextureBottom.IsEmpty()) UniqueNames.AddUnique(Def.TextureBottom);
	}
	UniqueNames.Sort();  // stable ordering so tile indices don't shift between runs

	// ----------------------------------------------------------------
	//  2. Build the tile index map.
	// ----------------------------------------------------------------
	TextureToTileIndex.Empty();
	for (int32 i = 0; i < UniqueNames.Num(); ++i)
	{
		TextureToTileIndex.Add(UniqueNames[i], i);
	}

	// ----------------------------------------------------------------
	//  2b. Auto-load any texture key not already in BlockTextures.
	// ----------------------------------------------------------------
	if (!TextureBasePath.IsEmpty())
	{
		for (const FString& Name : UniqueNames)
		{
			if (BlockTextures.Contains(Name)) continue;  // manual override wins

			// UE asset path: /Game/Textures/Blocks/grass_top.grass_top
			const FString AssetPath = FString::Printf(TEXT("%s%s.%s"),
			                                          *TextureBasePath, *Name, *Name);
			UTexture2D* Loaded = LoadObject<UTexture2D>(nullptr, *AssetPath);
			if (Loaded)
			{
				BlockTextures.Add(Name, Loaded);
				UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Auto-loaded texture '%s'"), *AssetPath);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: Could not auto-load '%s' – file missing or wrong path."), *AssetPath);
			}
		}
	}

	if (UniqueNames.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: No block textures registered – atlas not built."));
		return;
	}

	// ----------------------------------------------------------------
	//  2c. NOTE on source texture settings:
	//      We intentionally do NOT modify CompressionSettings, MipGenSettings,
	//      or Filter on source textures at runtime.  In the editor those are
	//      import properties – touching them triggers the async texture
	//      compiler and causes a fatal "Registering a texture from inside a
	//      postcompilation" error.
	//      Source textures must be imported with CPU-readable compression
	//      (TC_EditorIcon / UserInterface2D) in the editor before play.
	//      If a texture's BulkData.Lock() returns null below we log a warning.
	// ----------------------------------------------------------------

	// ----------------------------------------------------------------
	//  3. Determine atlas grid dimensions (as square as possible).
	// ----------------------------------------------------------------
	static constexpr int32 TilePx = 16;   // each tile is 16×16 pixels

	const int32 NumTiles  = UniqueNames.Num();
	ComputedAtlasCols     = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt((float)NumTiles)));
	const int32 AtlasRows = FMath::CeilToInt((float)NumTiles / (float)ComputedAtlasCols);
	const int32 AtlasW    = ComputedAtlasCols * TilePx;
	const int32 AtlasH    = AtlasRows         * TilePx;

	// ----------------------------------------------------------------
	//  4. Create a transient BGRA8 atlas texture.
	// ----------------------------------------------------------------
	RuntimeAtlas = UTexture2D::CreateTransient(AtlasW, AtlasH, PF_B8G8R8A8, TEXT("VoxelAtlas"));
	if (!RuntimeAtlas)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelWorld: Failed to create runtime atlas texture."));
		return;
	}
	RuntimeAtlas->Filter         = TF_Nearest;   // pixel-art look, no blurring between tiles
	RuntimeAtlas->AddressX       = TA_Clamp;
	RuntimeAtlas->AddressY       = TA_Clamp;
	RuntimeAtlas->SRGB           = true;
	// Do NOT set CompressionSettings here. Assigning it on any UTexture2D in the
	// editor marks the texture dirty and registers it with FTextureCompilingManager.
	// If this runs inside (or triggered by) a source-texture post-compilation
	// callback, UE fatally asserts. CreateTransient already produces an
	// uncompressed BGRA8 texture – no DXT pass is needed or desired.

	FTexture2DMipMap& AtlasMip = RuntimeAtlas->GetPlatformData()->Mips[0];
	uint8* AtlasData = static_cast<uint8*>(AtlasMip.BulkData.Lock(LOCK_READ_WRITE));
	if (!AtlasData)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelWorld: Failed to lock atlas mip buffer – aborting atlas build."));
		AtlasMip.BulkData.Unlock();
		return;
	}
	FMemory::Memset(AtlasData, 0xFF, AtlasW * AtlasH * 4);  // default white (visible if a slot is unfilled)

	// ----------------------------------------------------------------
	//  5. Copy each source texture into its slot.
	// ----------------------------------------------------------------
	for (const FString& Name : UniqueNames)
	{
		const TObjectPtr<UTexture2D>* FoundPtr = BlockTextures.Find(Name);
		if (!FoundPtr || !(*FoundPtr))
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: Texture '%s' is referenced but not in BlockTextures map."), *Name);
			continue;
		}

		UTexture2D* SrcTex = FoundPtr->Get();
		if (!SrcTex->GetPlatformData() || SrcTex->GetPlatformData()->Mips.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: Texture '%s' has no accessible mip data. "
			       "Set its Compression to 'VectorDisplacementmap' or 'UserInterface2D' and re-import."), *Name);
			continue;
		}

		const int32 TileIdx = TextureToTileIndex[Name];
		const int32 TileCol = TileIdx % ComputedAtlasCols;
		const int32 TileRow = TileIdx / ComputedAtlasCols;

		bool bTileCopied = false;

#if WITH_EDITORONLY_DATA
		// ----------------------------------------------------------------
		//  Editor / PIE path: read from the texture's Source art.
		//  This is the original uncompressed pixel data stored inside the
		//  .uasset file.  It is always CPU-resident in the editor regardless
		//  of compression settings, streaming state, or how many times PIE
		//  has been entered.  This is the correct path for atlas building in
		//  the editor and avoids all the PlatformData / BulkData eviction
		//  issues that make the atlas go white on the second play session.
		// ----------------------------------------------------------------
		if (SrcTex->Source.IsValid())
		{
			TArray64<uint8> SrcRaw;
			SrcTex->Source.GetMipData(SrcRaw, 0);

			const ETextureSourceFormat SourceFmt = SrcTex->Source.GetFormat();
			const int32 SrcW = SrcTex->Source.GetSizeX();
			const int32 SrcH = SrcTex->Source.GetSizeY();

			// Diagnostic: log format and first pixel bytes so mismatched formats are easy to spot.
			if (SrcRaw.Num() >= 8)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("VoxelWorld: Texture '%s'  fmt=%d  size=%dx%d  rawBytes=%d  px0=[%02X %02X %02X %02X %02X %02X %02X %02X]"),
					*Name, (int32)SourceFmt, SrcW, SrcH, SrcRaw.Num(),
					SrcRaw[0], SrcRaw[1], SrcRaw[2], SrcRaw[3],
					SrcRaw[4], SrcRaw[5], SrcRaw[6], SrcRaw[7]);
			}

			// TSF_G8:    grayscale 8-bit – 1 byte per pixel (e.g. stone, cobblestone).
			//             Expanded to opaque BGRA by replicating the grey value.
			// TSF_BGRA8:  standard 8-bit BGRA – most PNG imports.
			// TSF_RGBA16: 16-bit per-channel RGBA little-endian – some higher-quality
			//             imports. Downsampled to 8-bit by taking the high byte of each channel.
			if (SourceFmt == TSF_G8 && SrcRaw.Num() > 0)
			{
				for (int32 py = 0; py < TilePx; ++py)
				{
					for (int32 px = 0; px < TilePx; ++px)
					{
						const int32 SrcX   = (px * SrcW) / TilePx;
						const int32 SrcY   = (py * SrcH) / TilePx;
						const int32 SrcIdx = SrcY * SrcW + SrcX;  // 1 byte per pixel

						const int32 DstX   = TileCol * TilePx + px;
						const int32 DstY   = TileRow * TilePx + py;
						const int32 DstIdx = (DstY * AtlasW + DstX) * 4;

						const uint8 Gray = SrcRaw[SrcIdx];
						AtlasData[DstIdx + 0] = Gray;  // B
						AtlasData[DstIdx + 1] = Gray;  // G
						AtlasData[DstIdx + 2] = Gray;  // R
						AtlasData[DstIdx + 3] = 255;   // A
					}
				}
				bTileCopied = true;
			}
			else if (SourceFmt == TSF_BGRA8 && SrcRaw.Num() > 0)
			{
				for (int32 py = 0; py < TilePx; ++py)
				{
					for (int32 px = 0; px < TilePx; ++px)
					{
						const int32 SrcX   = (px * SrcW) / TilePx;
						const int32 SrcY   = (py * SrcH) / TilePx;
						const int32 SrcIdx = (SrcY * SrcW + SrcX) * 4;  // 4 bytes per pixel

						const int32 DstX   = TileCol * TilePx + px;
						const int32 DstY   = TileRow * TilePx + py;
						const int32 DstIdx = (DstY * AtlasW + DstX) * 4;

						// BGRA8 source → BGRA8 atlas: direct copy
						AtlasData[DstIdx + 0] = SrcRaw[SrcIdx + 0];  // B
						AtlasData[DstIdx + 1] = SrcRaw[SrcIdx + 1];  // G
						AtlasData[DstIdx + 2] = SrcRaw[SrcIdx + 2];  // R
						AtlasData[DstIdx + 3] = SrcRaw[SrcIdx + 3];  // A
					}
				}
				bTileCopied = true;
			}
			else if (SourceFmt == TSF_RGBA16 && SrcRaw.Num() > 0)
			{
				// 8 bytes per pixel: R0R1 G0G1 B0B1 A0A1 (little-endian, so [+1] = high byte)
				for (int32 py = 0; py < TilePx; ++py)
				{
					for (int32 px = 0; px < TilePx; ++px)
					{
						const int32 SrcX   = (px * SrcW) / TilePx;
						const int32 SrcY   = (py * SrcH) / TilePx;
						const int32 SrcIdx = (SrcY * SrcW + SrcX) * 8;  // 8 bytes per pixel

						const int32 DstX   = TileCol * TilePx + px;
						const int32 DstY   = TileRow * TilePx + py;
						const int32 DstIdx = (DstY * AtlasW + DstX) * 4;

						// RGBA16 (LE) → BGRA8: take high byte of each channel, swap R↔B
						AtlasData[DstIdx + 0] = SrcRaw[SrcIdx + 5];  // B ← high byte of src B (LE offset +5)
						AtlasData[DstIdx + 1] = SrcRaw[SrcIdx + 3];  // G ← high byte of src G (LE offset +3)
						AtlasData[DstIdx + 2] = SrcRaw[SrcIdx + 1];  // R ← high byte of src R (LE offset +1)
						AtlasData[DstIdx + 3] = SrcRaw[SrcIdx + 7];  // A ← high byte of src A (LE offset +7)
					}
				}
				bTileCopied = true;
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("VoxelWorld: Texture '%s' has unsupported source format %d – tile will appear white. Expected TSF_G8 (%d), TSF_BGRA8 (%d), or TSF_RGBA16 (%d)."),
					*Name, (int32)SourceFmt, (int32)TSF_G8, (int32)TSF_BGRA8, (int32)TSF_RGBA16);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("VoxelWorld: Texture '%s' has no Source art – tile will appear white. Re-import the texture."), *Name);
		}
#endif  // WITH_EDITORONLY_DATA

		// ----------------------------------------------------------------
		//  Packaged / runtime fallback: PlatformData BulkData path.
		//  Only reached when Source art is unavailable (cooked builds).
		//  Requires the texture to be imported as UserInterface2D / BGRA8.
		// ----------------------------------------------------------------
		if (!bTileCopied)
		{
			if (SrcTex->GetPlatformData() && !SrcTex->GetPlatformData()->Mips.IsEmpty())
			{
				FTexture2DMipMap& SrcMip = SrcTex->GetPlatformData()->Mips[0];
				if (!SrcMip.BulkData.IsBulkDataLoaded())
				{
					SrcMip.BulkData.ForceBulkDataResident();
				}

				const uint8* SrcData = static_cast<const uint8*>(SrcMip.BulkData.Lock(LOCK_READ_ONLY));
				if (SrcData)
				{
					const int32 SrcW = SrcTex->GetSizeX();
					const int32 SrcH = SrcTex->GetSizeY();

					for (int32 py = 0; py < TilePx; ++py)
					{
						for (int32 px = 0; px < TilePx; ++px)
						{
							const int32 SrcX   = (px * SrcW) / TilePx;
							const int32 SrcY   = (py * SrcH) / TilePx;
							const int32 SrcIdx = (SrcY * SrcW + SrcX) * 4;

							const int32 DstX   = TileCol * TilePx + px;
							const int32 DstY   = TileRow * TilePx + py;
							const int32 DstIdx = (DstY * AtlasW + DstX) * 4;

							AtlasData[DstIdx + 0] = SrcData[SrcIdx + 0];  // B
							AtlasData[DstIdx + 1] = SrcData[SrcIdx + 1];  // G
							AtlasData[DstIdx + 2] = SrcData[SrcIdx + 2];  // R
							AtlasData[DstIdx + 3] = SrcData[SrcIdx + 3];  // A
						}
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning,
						TEXT("VoxelWorld: Could not lock PlatformData mip for '%s' – tile will appear white."), *Name);
				}
				SrcMip.BulkData.Unlock();
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("VoxelWorld: No PlatformData mips for '%s' – tile will appear white."), *Name);
			}
		}
	}

	AtlasMip.BulkData.Unlock();
	RuntimeAtlas->UpdateResource();

	UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Built %d×%d atlas (%d tiles) from %d unique textures."),
		AtlasW, AtlasH, NumTiles, NumTiles);
	// ----------------------------------------------------------------
	//  DIAGNOSTIC: confirm atlas state before any chunk spawns.
	// ----------------------------------------------------------------
	UE_LOG(LogTemp, Warning,
		TEXT("[AtlasDiag] ComputedAtlasCols=%d  TileU=%.4f  TileV=%.4f  TileIndexMap entries=%d"),
		ComputedAtlasCols,
		1.f / FMath::Max(1.f, (float)ComputedAtlasCols),
		1.f / FMath::Max(1.f, FMath::CeilToFloat((float)NumTiles / FMath::Max(1.f, (float)ComputedAtlasCols))),
		TextureToTileIndex.Num());
	// Dump the first 8 tile→index entries so we can confirm keys match block defs.
	{
		int32 DumpCount = 0;
		for (const auto& KV : TextureToTileIndex)
		{
			UE_LOG(LogTemp, Warning, TEXT("[AtlasDiag]  tile[%d] = '%s'"), KV.Value, *KV.Key);
			if (++DumpCount >= 8) break;
		}
	}
	// ----------------------------------------------------------------
	//  6. Create a dynamic material instance and bind the atlas.
	// ----------------------------------------------------------------
	if (ChunkMaterial)
	{
		RuntimeChunkMaterial = UMaterialInstanceDynamic::Create(ChunkMaterial, this);
		RuntimeChunkMaterial->SetTextureParameterValue(TEXT("AtlasTexture"), RuntimeAtlas);
		RuntimeChunkMaterial->SetScalarParameterValue(TEXT("MinimumBrightness"), MinimumBrightness);
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: Set MinimumBrightness = %.2f on dynamic material"), MinimumBrightness);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: ChunkMaterial is not set – atlas was built but cannot be bound to a material."));
	}
}

// ---------------------------------------------------------------------------
//  Streaming
// ---------------------------------------------------------------------------

void AVoxelWorld::UpdateStreamingPosition(FVector WorldPosition)
{
	FIntVector PlayerChunk = WorldPosToChunkCoord(WorldPosition);
	UE_LOG(LogTemp, Warning, TEXT("UpdateStreamingPosition: WorldPosition=(%.1f, %.1f, %.1f) → PlayerChunk=(%d,%d,%d)"), 
		WorldPosition.X, WorldPosition.Y, WorldPosition.Z, PlayerChunk.X, PlayerChunk.Y, PlayerChunk.Z);

	if (PlayerChunk == LastPlayerChunkCoord) return;
	LastPlayerChunkCoord = PlayerChunk;

	// Collect desired chunk set - now includes vertical streaming
	// Load RenderDistance chunks in XY, and ±3 chunks vertically
	static constexpr int32 VerticalRenderDistance = 3;
	TSet<FIntVector> Desired;
	for (int32 dx = -RenderDistance; dx <= RenderDistance; ++dx)
	{
		for (int32 dy = -RenderDistance; dy <= RenderDistance; ++dy)
		{
			for (int32 dz = -VerticalRenderDistance; dz <= VerticalRenderDistance; ++dz)
			{
				Desired.Add(PlayerChunk + FIntVector(dx, dy, dz));
			}
		}
	}

	// Load newly visible chunks
	for (const FIntVector& Coord : Desired)
	{
		if (!LoadedChunks.Contains(Coord))
		{
			LoadChunk(Coord);
		}
	}

	// Unload out-of-range chunks (add 2 as hysteresis to avoid churn)
	TArray<FIntVector> ToUnload;
	for (const auto& Pair : LoadedChunks)
	{
		FIntVector Delta = Pair.Key - PlayerChunk;
		if (FMath::Abs(Delta.X) > RenderDistance + 2 ||
			FMath::Abs(Delta.Y) > RenderDistance + 2 ||
			FMath::Abs(Delta.Z) > VerticalRenderDistance + 2)
		{
			ToUnload.Add(Pair.Key);
		}
	}
	for (const FIntVector& Coord : ToUnload)
	{
		UnloadChunk(Coord);
	}
}

// ---------------------------------------------------------------------------
//  Flat world
// ---------------------------------------------------------------------------

void AVoxelWorld::LoadFlatWorld()
{
	if (bFlatLoaded) return;
	bFlatLoaded = true;

	// Spawn FlatExtentChunks × FlatExtentChunks chunks centered near the origin.
	// With FlatExtentChunks=4: chunks (0,0) to (3,3) → 64 m × 64 m surface.
	for (int32 cx = 0; cx < FlatExtentChunks; ++cx)
	{
		for (int32 cy = 0; cy < FlatExtentChunks; ++cy)
		{
			LoadChunk(FIntVector(cx, cy, 0));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Loaded %d×%d flat chunk grid (%d m × %d m)."),
		FlatExtentChunks, FlatExtentChunks,
		FlatExtentChunks * CHUNK_SIZE_X,
		FlatExtentChunks * CHUNK_SIZE_Y);
}

void AVoxelWorld::GenerateFlatChunkData(FIntVector Coord, TArray<EBlockType>& OutBlocks) const
{
	OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

	const int32 GrassZ = FlatSurfaceHeight;      // top surface
	const int32 DirtMinZ = GrassZ - 3;           // 3 blocks of dirt below grass

	for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
	{
		for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
		{
			for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
			{
				EBlockType Type;
				if      (Z > GrassZ)               Type = EBlockType::Air;
				else if (Z == GrassZ)              Type = EBlockType::Grass;
				else if (Z >= DirtMinZ)            Type = EBlockType::Dirt;
				else                               Type = EBlockType::Stone;

				OutBlocks[X + CHUNK_SIZE_X * (Y + CHUNK_SIZE_Y * Z)] = Type;
			}
		}
	}
}

// ---------------------------------------------------------------------------

void AVoxelWorld::LoadChunk(FIntVector Coord)
{
	// Spawn chunk actor first, then fill it with generated block data.
	// Generators receive the spawned chunk and call Initialize() internally.
	const FVector WorldPos(
		static_cast<float>(Coord.X) * CHUNK_SIZE_X * BLOCK_SIZE,
		static_cast<float>(Coord.Y) * CHUNK_SIZE_Y * BLOCK_SIZE,
		static_cast<float>(Coord.Z) * CHUNK_SIZE_Z * BLOCK_SIZE
	);

	FActorSpawnParameters Params;
	Params.Owner = this;

	AChunk* NewChunk = GetWorld()->SpawnActor<AChunk>(AChunk::StaticClass(), WorldPos, FRotator::ZeroRotator, Params);
	if (!NewChunk) return;

	NewChunk->ChunkCoord        = Coord;
	NewChunk->VoxelWorld        = this;
	// Flat worlds need sync collision for proper setup
	NewChunk->bUseSyncCollision = (WorldGenType == EWorldGenType::Flat);

	if (WorldGenType == EWorldGenType::Flat)
	{
		TArray<EBlockType> Blocks;
		GenerateFlatChunkData(Coord, Blocks);
		NewChunk->Initialize(Blocks);
	}
	else
	{
		// Route to the correct level generator; Initialize() is called inside.
		WorldGenManager->RouteChunkGeneration(*NewChunk, Coord.X, Coord.Y, Coord.Z);
	}

	LoadedChunks.Add(Coord, NewChunk);

	// Ask border neighbors to rebuild so shared-boundary faces are correct
	const FIntVector Offsets[] = {
		{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
	};
	for (const FIntVector& Off : Offsets)
	{
		TObjectPtr<AChunk>* Neighbor = LoadedChunks.Find(Coord + Off);
		if (Neighbor && *Neighbor) (*Neighbor)->RebuildMesh();
	}
}

void AVoxelWorld::UnloadChunk(FIntVector Coord)
{
	TObjectPtr<AChunk>* Found = LoadedChunks.Find(Coord);
	if (!Found || !(*Found)) return;

	(*Found)->Destroy();
	LoadedChunks.Remove(Coord);
}

// ---------------------------------------------------------------------------
//  Player spawn location
// ---------------------------------------------------------------------------

FVector AVoxelWorld::GetPlayerSpawnLocation() const
{
	if (WorldGenType == EWorldGenType::Flat)
	{
		// Center of the flat chunk grid in XY.
		const float GridHalfX = (FlatExtentChunks * CHUNK_SIZE_X * BLOCK_SIZE) * 0.5f;
		const float GridHalfY = (FlatExtentChunks * CHUNK_SIZE_Y * BLOCK_SIZE) * 0.5f;

		// Top of the grass block = (SurfaceZ + 1) block-faces up from the chunk origin.
		const float GrassTopZ  = (FlatSurfaceHeight + 1) * BLOCK_SIZE;

		// Capsule half-height (96) keeps feet at the surface.
		// Extra 300-unit buffer lets the player fall onto confirmed collision
		// rather than spawning exactly at the surface edge.
		const float SpawnZ = GrassTopZ + 96.f + 300.f;

		return FVector(GridHalfX, GridHalfY, SpawnZ);
	}
	else
	{
		// Terrain: Query actual height at origin and spawn just above terrain surface
		const float TerrainNoise = FSurfaceLevelGenerator::SampleHeight(0.f, 0.f);
		const int32 TerrainSurfaceZ = FSurfaceLevelGenerator::BASE_HEIGHT + FMath::RoundToInt(TerrainNoise * static_cast<float>(FSurfaceLevelGenerator::HEIGHT_RANGE));
		
		// Spawn just 2 blocks above the terrain surface (where solid blocks are)
		// Player's capsule will land on the grass/dirt layer below
		const int32 SpawnBlockZ = TerrainSurfaceZ + 2;
		const float SpawnZ = (static_cast<float>(SpawnBlockZ) + 1.f) * BLOCK_SIZE + 96.f;  // +1 for block above surface, +96 for capsule
		
		return FVector(0.f, 0.f, SpawnZ);
	}
}

// ---------------------------------------------------------------------------
//  Sun direction (for per-face brightness computation)
// ---------------------------------------------------------------------------

FVector AVoxelWorld::GetSunDirection() const
{
	// Query the world for a DirectionalLight actor
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		ADirectionalLight* Light = *It;
		if (Light && Light->IsValidLowLevel())
		{
			// Return the forward vector (direction the light shines from)
			return Light->GetActorForwardVector();
		}
	}
	
	// No directional light found – return default downward direction
	return FVector(0.f, 0.f, -1.f);
}

// (GenerateChunkData removed — routing is now handled by FWorldGenerationManager::RouteChunkGeneration)
