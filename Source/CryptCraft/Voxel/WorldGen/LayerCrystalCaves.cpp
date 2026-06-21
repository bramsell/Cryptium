// LayerCrystalCaves.cpp
// Crystal Caves layer generation (Level 1, GlobalChunkZ -1 .. -8, world Z -1 .. -256).
//
// Block layout (256 blocks = 8 chunks, LocalChunkZ 0 = shallowest):
//   LocalChunkZ 0   : Solid stone ceiling              ( 32 blocks)
//   LocalChunkZ 1   : Ceiling fringe — Perlin stalactites, up to 32 blocks deep
//   LocalChunkZ 2–5 : Pure open air void               (128 blocks)
//   LocalChunkZ 6   : Floor fringe   — Perlin stalagmites, up to 32 blocks tall
//   LocalChunkZ 7   : Solid stone floor                ( 32 blocks)
//
// Within every chunk, local block Z=31 is the shallowest face (highest world Z)
// and Z=0 is the deepest face (most negative world Z).
//
// ---------------------------------------------------------------------------
// PERFORMANCE ARCHITECTURE NOTE (read before modifying):
//
// The cavern/worm "nearby search" (FindNearbyCaverns, worm cell scanning) is
// expensive relative to a single noise sample — it scans a neighborhood of
// coarse grid cells. A chunk is only 16x16x16 blocks, which is small compared
// to the search radii involved (80-512 blocks), so the answer to "what's
// nearby" is effectively constant across an entire chunk.
//
// Therefore: nearby caverns and nearby worms (with their paths) are fetched
// EXACTLY ONCE PER CHUNK, before the per-voxel loop runs. The per-voxel loop
// only ever does cheap distance/math checks against these small pre-fetched
// lists. Do not move FindNearbyCaverns, GetWormAtCell, or worm path
// simulation back inside the per-voxel loop — that reintroduces an
// editor-freezing performance bug (verified: O(voxels x cells x steps)
// blowup, billions of ops per chunk column).
//
// Worm paths themselves are cached process-wide by WormSeed (GWormPathCache)
// since the same worm can be queried by multiple neighboring chunks. This
// cache is runtime-only (rebuilt each session) — never persisted to save
// data. Only WorldSeed + per-chunk block diffs are saved; everything here
// must remain a pure, re-derivable function of those.
// ---------------------------------------------------------------------------

#include "LayerCrystalCaves.h"
#include "LayerBase.h"
#include "CaveNoiseUtilities.h"
#include "Voxel/Chunk.h"

// -----------------------------------------------------------------------
//  STEP 1: Cavern Layout Implementation
// -----------------------------------------------------------------------

namespace CavernLayout
{
	// Configuration Constants
	static constexpr int32 CAVERN_GRID_CELL_SIZE = 128;
	static constexpr float LAYOUT_NOISE_FREQUENCY = 1.0f / 64.0f;
	static constexpr float CHECKERBOARD_FREQUENCY = 1.0f / 256.0f;
	static constexpr float BUBBLE_RADIUS_BASE = 32.0f;
	static constexpr float BUBBLE_RADIUS_VARIATION = 12.0f;

	ECavernLayer GetLayerForColumn(uint32 WorldSeed, int32 WorldX, int32 WorldZ)
	{
		const float X = static_cast<float>(WorldX) * CHECKERBOARD_FREQUENCY;
		const float Z = static_cast<float>(WorldZ) * CHECKERBOARD_FREQUENCY;
		const float Pattern = CaveNoise::SeededNoise2D(WorldSeed, X, Z);
		return (Pattern > 0.5f) ? ECavernLayer::Upper : ECavernLayer::Lower;
	}

	/**
	 * Get the layer for a specific cavern grid CELL (not world position).
	 * Uses discrete checkerboard pattern: adjacent cells always have different layers.
	 * This prevents caverns from stacking directly on top of each other.
	 */
	ECavernLayer GetLayerForCell(int32 CellX, int32 CellZ)
	{
		// Simple checkerboard: if (CellX + CellZ) is even, use Upper; else use Lower
		return ((CellX + CellZ) % 2 == 0) ? ECavernLayer::Upper : ECavernLayer::Lower;
	}

	static FORCEINLINE FIntVector WorldToCellCoord(int32 WorldX, int32 WorldZ)
	{
		return FIntVector(
			FMath::FloorToInt(static_cast<float>(WorldX) / CAVERN_GRID_CELL_SIZE),
			0,
			FMath::FloorToInt(static_cast<float>(WorldZ) / CAVERN_GRID_CELL_SIZE)
		);
	}

	// -------------------------------------------------------------------
	// Per-(WorldSeed,Cell) cache for bubble placement.
	// Cheap to look up; eliminates redundant noise work for repeated
	// queries of the same cell (e.g. from FindNearbyCaverns's neighborhood
	// scan, which is itself now only called once per chunk - see below).
	// -------------------------------------------------------------------
	static FORCEINLINE uint64 PackCellKey(uint32 WorldSeed, int32 CellX, int32 CellZ)
	{
		const uint32 CellHash = CaveNoise::HashCellSeed(WorldSeed, FIntVector(CellX, 0, CellZ));
		return (static_cast<uint64>(WorldSeed) << 32) | static_cast<uint64>(CellHash);
	}

	static TMap<uint64, FCavernBubble> GBubbleCellCache;

	FCavernBubble GetBubbleAtCell(
		uint32 WorldSeed,
		int32 CellX,
		int32 CellZ,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig)
	{
		FCavernBubble Result;
		Result.bExists = false;

		const uint32 CellSeed = CaveNoise::HashCellSeed(WorldSeed, FIntVector(CellX, 0, CellZ));

		const float CellCenterX = (CellX * CAVERN_GRID_CELL_SIZE + CAVERN_GRID_CELL_SIZE / 2.0f);
		const float CellCenterZ = (CellZ * CAVERN_GRID_CELL_SIZE + CAVERN_GRID_CELL_SIZE / 2.0f);

		const float LayoutNoise = CaveNoise::SeededNoise2D(
			CellSeed,
			CellCenterX * LAYOUT_NOISE_FREQUENCY,
			CellCenterZ * LAYOUT_NOISE_FREQUENCY
		);

		// Discrete checkerboard pattern based on cell coordinates (not continuous noise)
		// ensures adjacent cells always have different layers.
		const ECavernLayer Layer = GetLayerForCell(CellX, CellZ);
		const FCavernLayerConfig& Config = (Layer == ECavernLayer::Upper) ? UpperConfig : LowerConfig;

		if (LayoutNoise <= Config.LayoutThreshold)
		{
			return Result;
		}

		Result.bExists = true;
		Result.Layer = Layer;

		const float OffsetX = (LayoutNoise - 0.5f) * (CAVERN_GRID_CELL_SIZE * 0.3f);
		const float OffsetZNoise = CaveNoise::SeededNoise2D(
			CellSeed ^ 0xDEADBEEFu,
			CellCenterX * LAYOUT_NOISE_FREQUENCY,
			CellCenterZ * LAYOUT_NOISE_FREQUENCY
		) - 0.5f;
		const float OffsetZValue = OffsetZNoise * (CAVERN_GRID_CELL_SIZE * 0.3f);

		Result.Center = FVector(CellCenterX + OffsetX, (Config.MinZ + Config.MaxZ) * 0.5f, CellCenterZ + OffsetZValue);

		const float RadiusNoise = CaveNoise::SeededNoise1D(
			CellSeed ^ 0x12345678u,
			static_cast<float>(CellX ^ CellZ) * 0.1f
		);
		Result.Radius = BUBBLE_RADIUS_BASE + (RadiusNoise - 0.5f) * 2.0f * BUBBLE_RADIUS_VARIATION;

		return Result;
	}

	FCavernBubble GetBubbleAtCellCached(
		uint32 WorldSeed,
		int32 CellX,
		int32 CellZ,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig)
	{
		const uint64 Key = PackCellKey(WorldSeed, CellX, CellZ);
		if (const FCavernBubble* Cached = GBubbleCellCache.Find(Key))
		{
			return *Cached;
		}

		FCavernBubble Result = GetBubbleAtCell(WorldSeed, CellX, CellZ, UpperConfig, LowerConfig);
		GBubbleCellCache.Add(Key, Result);
		return Result;
	}

	void FindNearbyCaverns(
		uint32 WorldSeed,
		FVector WorldPos,
		float SearchRadius,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig,
		TArray<FCavernBubble>& OutBubbles)
	{
		OutBubbles.Reset();

		const FIntVector CenterCell = WorldToCellCoord((int32)WorldPos.X, (int32)WorldPos.Z);
		const int32 SearchCellRadius = FMath::CeilToInt(SearchRadius / CAVERN_GRID_CELL_SIZE) + 1;

		for (int32 DX = -SearchCellRadius; DX <= SearchCellRadius; ++DX)
		{
			for (int32 DZ = -SearchCellRadius; DZ <= SearchCellRadius; ++DZ)
			{
				const int32 CellX = CenterCell.X + DX;
				const int32 CellZ = CenterCell.Z + DZ;

				const FCavernBubble Bubble = GetBubbleAtCellCached(WorldSeed, CellX, CellZ, UpperConfig, LowerConfig);

				if (Bubble.bExists)
				{
					const float DistToCenter = FVector::Dist(WorldPos, Bubble.Center);
					if (DistToCenter <= SearchRadius + Bubble.Radius)
					{
						OutBubbles.Add(Bubble);
					}
				}
			}
		}
	}

	void DebugVisualizeBubbles(
		int32 CenterX,
		int32 CenterZ,
		int32 AreaSize,
		uint32 WorldSeed,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig)
	{
		UE_LOG(LogTemp, Warning, TEXT("\n=== STEP 1: Cavern Layout Debug Visualization ==="));
		UE_LOG(LogTemp, Warning, TEXT("Area: [%d, %d] to [%d, %d], Size: %d blocks"),
			CenterX - AreaSize/2, CenterZ - AreaSize/2, CenterX + AreaSize/2, CenterZ + AreaSize/2, AreaSize);

		int32 BubbleCountUpper = 0;
		int32 BubbleCountLower = 0;
		int32 TotalCellsChecked = 0;
		float AvgRadius = 0.0f;

		const int32 StartX = CenterX - AreaSize / 2;
		const int32 StartZ = CenterZ - AreaSize / 2;
		const int32 GridSize = 128;
		const int32 GridSteps = AreaSize / GridSize;

		TArray<FCavernBubble> AllBubbles;

		for (int32 GX = 0; GX < GridSteps; ++GX)
		{
			for (int32 GZ = 0; GZ < GridSteps; ++GZ)
			{
				const int32 CellX = (StartX / GridSize) + GX;
				const int32 CellZ = (StartZ / GridSize) + GZ;

				const FCavernBubble Bubble = GetBubbleAtCellCached(WorldSeed, CellX, CellZ, UpperConfig, LowerConfig);

				if (Bubble.bExists)
				{
					AllBubbles.Add(Bubble);
					AvgRadius += Bubble.Radius;

					if (Bubble.Layer == ECavernLayer::Upper)
					{
						BubbleCountUpper++;
					}
					else
					{
						BubbleCountLower++;
					}
				}

				TotalCellsChecked++;
			}
		}

		const int32 TotalBubbles = BubbleCountUpper + BubbleCountLower;
		if (TotalBubbles > 0)
		{
			AvgRadius /= TotalBubbles;
		}

		UE_LOG(LogTemp, Warning, TEXT("Results:"));
		UE_LOG(LogTemp, Warning, TEXT("  Total cells checked: %d"), TotalCellsChecked);
		UE_LOG(LogTemp, Warning, TEXT("  Total bubbles found: %d"), TotalBubbles);
		UE_LOG(LogTemp, Warning, TEXT("  Upper layer bubbles: %d"), BubbleCountUpper);
		UE_LOG(LogTemp, Warning, TEXT("  Lower layer bubbles: %d"), BubbleCountLower);
		UE_LOG(LogTemp, Warning, TEXT("  Average bubble radius: %.1f blocks"), AvgRadius);
		UE_LOG(LogTemp, Warning, TEXT("  Bubble density: %.2f%%"), (TotalBubbles * 100.0f) / TotalCellsChecked);

		bool bAnyStacking = false;
		for (int32 i = 0; i < AllBubbles.Num() && !bAnyStacking; ++i)
		{
			for (int32 j = i + 1; j < AllBubbles.Num(); ++j)
			{
				const FCavernBubble& B1 = AllBubbles[i];
				const FCavernBubble& B2 = AllBubbles[j];

				const float DXZ = FMath::Sqrt(
					FMath::Square(B1.Center.X - B2.Center.X) +
					FMath::Square(B1.Center.Z - B2.Center.Z)
				);

				if (DXZ < 64.0f && B1.Layer != B2.Layer)
				{
					UE_LOG(LogTemp, Warning, TEXT("  WARNING: Potential stacking detected at (%.0f, %.0f)"),
						B1.Center.X, B1.Center.Z);
					bAnyStacking = true;
					break;
				}
			}
		}

		if (!bAnyStacking && TotalBubbles > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("  Stacking check: PASS - No bubbles stacked vertically"));
		}

		UE_LOG(LogTemp, Warning, TEXT("  First 5 bubbles:"));
		for (int32 i = 0; i < FMath::Min(5, AllBubbles.Num()); ++i)
		{
			const FCavernBubble& B = AllBubbles[i];
			const FString LayerName = (B.Layer == ECavernLayer::Upper) ? TEXT("Upper") : TEXT("Lower");
			UE_LOG(LogTemp, Warning, TEXT("    [%d] %s at (%.0f, %.0f, %.0f), radius %.1f"),
				i, *LayerName, B.Center.X, B.Center.Y, B.Center.Z, B.Radius);
		}

		UE_LOG(LogTemp, Warning, TEXT("=== Visualization Complete ===\n"));
	}

	// Console command
	static FAutoConsoleCommand CavernDebugCmd(
		TEXT("cav_DebugBubbles"),
		TEXT("Visualize cavern bubble placement (args: CenterX CenterZ AreaSize WorldSeed, defaults: 0 0 2048 12345)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			int32 CenterX = 0, CenterZ = 0, AreaSize = 2048;
			uint32 WorldSeed = 12345;

			if (Args.Num() > 0) CenterX = FCString::Atoi(*Args[0]);
			if (Args.Num() > 1) CenterZ = FCString::Atoi(*Args[1]);
			if (Args.Num() > 2) AreaSize = FCString::Atoi(*Args[2]);
			if (Args.Num() > 3) WorldSeed = FCString::Atoi(*Args[3]);

			FCavernLayerConfig UpperConfig;
			UpperConfig.MinZ = 64;
			UpperConfig.MaxZ = 128;
			UpperConfig.LayoutThreshold = 0.4f;

			FCavernLayerConfig LowerConfig;
			LowerConfig.MinZ = -64;
			LowerConfig.MaxZ = 0;
			LowerConfig.LayoutThreshold = 0.4f;

			DebugVisualizeBubbles(CenterX, CenterZ, AreaSize, WorldSeed, UpperConfig, LowerConfig);
		})
	);
}

// -----------------------------------------------------------------------
//  STEP 2: Cavern Shape Carving Implementation
// -----------------------------------------------------------------------

namespace CavernShape
{
	static constexpr float SEARCH_RADIUS = 80.0f;
	static constexpr float GEM_SPAWN_CHANCE = 0.02f;  // 2% per gem type
	static constexpr float EDGE_ROUGHNESS_STRENGTH = 0.15f;  // 0.0 = perfect sphere, higher = more jagged edges
	static constexpr float CAVE_ELONGATION_X = 1.0f;  // 1.0 = sphere, >1.0 = elongate in X direction
	static constexpr float CAVE_ELONGATION_Y = 1.5f;  // 1.0 = sphere, >1.0 = elongate in Y (vertical) direction
	static constexpr float CAVE_ELONGATION_Z = 1.0f;  // 1.0 = sphere, >1.0 = elongate in Z direction

	// NOTE: Takes PreFetchedCaverns instead of searching internally.
	// Caller (GenerateChunk) fetches nearby caverns ONCE per chunk via
	// CavernLayout::FindNearbyCaverns and passes the small result list in here.
	// Do not call FindNearbyCaverns from inside this function - that's what
	// caused the per-voxel search blowup.
	bool IsCavernVoxel(
		int32 WorldSeed,
		FIntVector WorldPos,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig,
		const TArray<FCavernBubble>& PreFetchedCaverns)
	{
		// Step 1: Determine which layer is active for this voxel's position
		const FIntVector CellCoord = CavernLayout::WorldToCellCoord(WorldPos.X, WorldPos.Y);
		const ECavernLayer Layer = CavernLayout::GetLayerForCell(CellCoord.X, CellCoord.Z);
		const FCavernLayerConfig& LayerConfig = (Layer == ECavernLayer::Upper) ? UpperConfig : LowerConfig;

		// Step 2: Check if Z (vertical) is within this layer's range
		if (WorldPos.Z < LayerConfig.MinZ || WorldPos.Z > LayerConfig.MaxZ)
		{
			return false;
		}

		// Step 3: Evaluate against the pre-fetched bubble list (no searching here)
		const FVector VoxelInCavernCoords = FVector(
			static_cast<float>(WorldPos.X),
			static_cast<float>(WorldPos.Z),  // vertical
			static_cast<float>(WorldPos.Y)   // second horizontal axis
		);

		for (const FCavernBubble& Bubble : PreFetchedCaverns)
		{
			// Only consider bubbles in the same layer as this voxel
			if (Bubble.Layer != Layer)
			{
				continue;
			}

			// Calculate ellipsoid distance using elongation factors
			FVector ScaledOffset = VoxelInCavernCoords - Bubble.Center;
			ScaledOffset.X /= CAVE_ELONGATION_X;
			ScaledOffset.Y /= CAVE_ELONGATION_Y;  // Y is vertical, elongating this creates taller caves
			ScaledOffset.Z /= CAVE_ELONGATION_Z;
			const float DistToCenter = ScaledOffset.Length();
			const float DistRatio = DistToCenter / Bubble.Radius;

			// Quick reject: way outside this bubble's influence
			if (DistRatio > 1.3f)
			{
				continue;
			}

			// Base smooth sphere falloff (0 = center, 1 = edge)
			float SphereValue = 1.0f - DistRatio;

			// Apply subtle noise at boundaries only.
			// Noise weight fades in near surface, vanishes toward center.
			if (DistRatio < 1.0f)
			{
				const FVector NoisePos = FVector(
					static_cast<float>(WorldPos.X) * 0.1f,
					static_cast<float>(WorldPos.Y) * 0.1f,
					static_cast<float>(WorldPos.Z) * 0.1f
				);

				const uint32 BubbleSeed = CaveNoise::HashCellSeed(
					WorldSeed,
					FIntVector(
						FMath::FloorToInt(Bubble.Center.X / 128.0f),
						0,
						FMath::FloorToInt(Bubble.Center.Z / 128.0f)
					)
				);

				const float SurfaceNoise = CaveNoise::SeededNoise3D(BubbleSeed, NoisePos);

				// Noise strength concentrated near surface, fades toward center
				const float NoiseWeight = FMath::Clamp(DistRatio, 0.0f, 1.0f);
				SphereValue += (SurfaceNoise - 0.5f) * EDGE_ROUGHNESS_STRENGTH * NoiseWeight;
			}

			if (SphereValue > 0.0f)
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * Get the block type for a cavern voxel (stone or gem).
	 * Determines if a stone voxel should be replaced with a glowing gem.
	 * Pure per-voxel noise lookup - no searching involved, cheap as-is.
	 */
	EBlockType GetCavernBlockType(
		int32 WorldSeed,
		FIntVector WorldPos)
	{
		const uint32 VoxelSeed = CaveNoise::HashCellSeed(
			WorldSeed,
			FIntVector(
				WorldPos.X / 4,
				WorldPos.Y / 4,
				WorldPos.Z / 4
			)
		);

		const float GemRoll = CaveNoise::SeededNoise3D(
			VoxelSeed,
			FVector(
				static_cast<float>(WorldPos.X % 4) * 0.25f,
				static_cast<float>(WorldPos.Y % 4) * 0.25f,
				static_cast<float>(WorldPos.Z % 4) * 0.25f
			)
		);

		// 2% chance per voxel for a gem block
		if (GemRoll > 0.98f)
		{
			const float GemType = CaveNoise::SeededNoise1D(VoxelSeed ^ 0xABCDEF00u, static_cast<float>(WorldPos.X + WorldPos.Z));
			EBlockType GemBlock;
			if (GemType < 0.33f)
				GemBlock = EBlockType::RubyOre;
			else if (GemType < 0.66f)
				GemBlock = EBlockType::DiamondOre;
			else
				GemBlock = EBlockType::EmeraldOre;

			return GemBlock;
		}

		return EBlockType::Stone;
	}
}

// -----------------------------------------------------------------------
//  STEP 3: Tunnel/Worm Path System Implementation
// -----------------------------------------------------------------------

namespace TunnelWorms
{
	static FORCEINLINE FIntVector WorldToWormCellCoord(int32 WorldX, int32 WorldZ)
	{
		return FIntVector(
			FMath::FloorToInt(static_cast<float>(WorldX) / WORM_GRID_CELL_SIZE),
			0,
			FMath::FloorToInt(static_cast<float>(WorldZ) / WORM_GRID_CELL_SIZE)
		);
	}

	// -------------------------------------------------------------------
	// Per-(WorldSeed,Cell) cache for worm spawn queries. Same rationale
	// as GBubbleCellCache above.
	// -------------------------------------------------------------------
	static FORCEINLINE uint64 PackWormCellKey(uint32 WorldSeed, int32 CellX, int32 CellZ)
	{
		const uint32 CellHash = CaveNoise::HashCellSeed(WorldSeed, FIntVector(CellX, 1, CellZ));
		return (static_cast<uint64>(WorldSeed) << 32) | static_cast<uint64>(CellHash);
	}

	static TMap<uint64, FWormSpawn> GWormCellCache;

	FWormSpawn GetWormAtCell(
		uint32 WorldSeed,
		int32 CellX,
		int32 CellZ,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig)
	{
		FWormSpawn Result;
		Result.bExists = false;

		const uint32 CellSeed = CaveNoise::HashCellSeed(WorldSeed, FIntVector(CellX, 1, CellZ));

		// Spawn noise: if > 0.6, this cell has a worm tunnel
		const float CellCenterX = (CellX * WORM_GRID_CELL_SIZE + WORM_GRID_CELL_SIZE / 2.0f);
		const float CellCenterZ = (CellZ * WORM_GRID_CELL_SIZE + WORM_GRID_CELL_SIZE / 2.0f);

		const float SpawnNoise = CaveNoise::SeededNoise2D(
			CellSeed,
			CellCenterX / 128.0f,
			CellCenterZ / 128.0f
		);

		if (SpawnNoise <= 0.6f)
		{
			return Result;  // No worm at this cell
		}

		Result.bExists = true;
		Result.WormSeed = CellSeed;

		// Pick a layer for the worm using discrete checkerboard.
		// CRITICAL FIX: Convert worm-grid cell coords to cavern-grid cell coords
		// so both systems agree on layer assignment for any given world position.
		// Worm grid = 256-block cells, Cavern grid = 128-block cells.
		// Use the worm cell's center world position to determine its cavern grid layer.
		const int32 CavernCellX = FMath::FloorToInt(CellCenterX / 128.0f);  // CAVERN_GRID_CELL_SIZE = 128
		const int32 CavernCellZ = FMath::FloorToInt(CellCenterZ / 128.0f);
		const ECavernLayer Layer = CavernLayout::GetLayerForCell(CavernCellX, CavernCellZ);
		const FCavernLayerConfig& LayerConfig = (Layer == ECavernLayer::Upper) ? UpperConfig : LowerConfig;

		// Random starting position within the cell with some offset
		const float StartOffsetX = (CaveNoise::SeededNoise1D(CellSeed ^ 0xAAAA, static_cast<float>(CellX)) - 0.5f) * (WORM_GRID_CELL_SIZE * 0.4f);
		const float StartOffsetZ = (CaveNoise::SeededNoise1D(CellSeed ^ 0xBBBB, static_cast<float>(CellZ)) - 0.5f) * (WORM_GRID_CELL_SIZE * 0.4f);

		Result.StartPosition = FVector(
			CellCenterX + StartOffsetX,
			LayerConfig.TunnelBaseAltitude,  // Y (vertical) at layer's base altitude
			CellCenterZ + StartOffsetZ
		);

		const float StartYaw = CaveNoise::SeededNoise1D(CellSeed ^ 0xCCCC, static_cast<float>(CellX + CellZ)) * 2.0f * PI;
		Result.StartDirection = FVector(
			FMath::Cos(StartYaw),
			0.0f,  // Initial worms don't start with vertical bias
			FMath::Sin(StartYaw)
		);
		Result.StartDirection.Normalize();

		// Find nearest target cavern for steering.
		// This FindNearbyCaverns call only happens when a NEW worm spawn is
		// computed (i.e. on a cache miss in GetWormAtCellCached below) - not
		// per-voxel - so its cost is fine here.
		TArray<FCavernBubble> NearbyCaverns;
		CavernLayout::FindNearbyCaverns(
			WorldSeed,
			Result.StartPosition,
			512.0f,
			UpperConfig,
			LowerConfig,
			NearbyCaverns);

		if (NearbyCaverns.Num() > 0)
		{
			float NearestDist = FLT_MAX;
			for (const FCavernBubble& Cavern : NearbyCaverns)
			{
				if (Cavern.Layer == Layer)
				{
					const float Dist = FVector::Dist(Result.StartPosition, Cavern.Center);
					if (Dist < NearestDist)
					{
						NearestDist = Dist;
						Result.TargetCavern = Cavern;
					}
				}
			}
		}

		return Result;
	}

	FWormSpawn GetWormAtCellCached(
		uint32 WorldSeed,
		int32 CellX,
		int32 CellZ,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig)
	{
		const uint64 Key = PackWormCellKey(WorldSeed, CellX, CellZ);
		if (const FWormSpawn* Cached = GWormCellCache.Find(Key))
		{
			return *Cached;
		}

		FWormSpawn Result = GetWormAtCell(WorldSeed, CellX, CellZ, UpperConfig, LowerConfig);
		GWormCellCache.Add(Key, Result);
		return Result;
	}

	FVector GetWormDirectionAtStep(
		uint32 WormSeed,
		int32 Step,
		FVector TargetCenter,
		FVector CurrentPos,
		float SteerWeight)
	{
		// Wander component: low-frequency noise for smooth wandering
		const float WanderNoise1D = CaveNoise::SeededNoise1D(
			WormSeed ^ 0x11111111u,
			static_cast<float>(Step) * 0.05f
		);
		const float WanderNoise2D = CaveNoise::SeededNoise1D(
			WormSeed ^ 0x22222222u,
			static_cast<float>(Step) * 0.05f + 0.5f
		);

		const float WanderYaw = WanderNoise1D * 2.0f * PI;
		FVector WanderDir = FVector(
			FMath::Cos(WanderYaw),
			(WanderNoise2D - 0.5f) * 0.3f,
			FMath::Sin(WanderYaw)
		);
		WanderDir.Normalize();

		FVector SteerDir = (TargetCenter - CurrentPos).GetSafeNormal();

		FVector ResultDir = FMath::Lerp(WanderDir, SteerDir, SteerWeight);
		ResultDir.Normalize();

		return ResultDir;
	}

	FVector GetWormPositionAtStep(
		uint32 WormSeed,
		int32 Step,
		FVector StartPos,
		FVector StartDir,
		FVector TargetCenter,
		float StepLength,
		int32 MaxSteps)
	{
		FVector CurrentPos = StartPos;
		FVector CurrentDir = StartDir;

		const int32 EffectiveSteps = FMath::Min(Step, MaxSteps);

		for (int32 s = 0; s < EffectiveSteps; ++s)
		{
			const float DistToTarget = FVector::Dist(CurrentPos, TargetCenter);
			const float SteerWeight = FMath::Clamp(1.0f - (DistToTarget / 256.0f), 0.1f, 0.5f);

			CurrentDir = GetWormDirectionAtStep(WormSeed, s, TargetCenter, CurrentPos, SteerWeight);
			CurrentPos += CurrentDir * StepLength;
		}

		return CurrentPos;
	}

	// -------------------------------------------------------------------
	// Process-wide cache of fully-simulated worm paths, keyed by WormSeed.
	// A worm's path is simulated ONCE (regardless of how many chunks/voxels
	// query it) and the resulting point array reused for cheap distance
	// checks thereafter. Runtime-only - never saved to disk.
	// -------------------------------------------------------------------
	static TMap<uint32, TArray<FVector>> GWormPathCache;

	static constexpr float WORM_STEP_LENGTH = 1.0f;
	static constexpr int32 WORM_MAX_STEPS = 500;
	static constexpr float WORM_TUNNEL_RADIUS = 6.0f;

	const TArray<FVector>& GetOrBuildWormPath(const FWormSpawn& Worm)
	{
		if (const TArray<FVector>* Cached = GWormPathCache.Find(Worm.WormSeed))
		{
			return *Cached;
		}

		TArray<FVector> Path;
		Path.Reserve(WORM_MAX_STEPS + 1);

		FVector CurrentPos = Worm.StartPosition;
		Path.Add(CurrentPos);

		for (int32 s = 0; s < WORM_MAX_STEPS; ++s)
		{
			const float DistToTarget = FVector::Dist(CurrentPos, Worm.TargetCavern.Center);
			const float SteerWeight = FMath::Clamp(1.0f - (DistToTarget / 256.0f), 0.1f, 0.5f);

			const FVector Dir = GetWormDirectionAtStep(Worm.WormSeed, s, Worm.TargetCavern.Center, CurrentPos, SteerWeight);
			CurrentPos += Dir * WORM_STEP_LENGTH;
			Path.Add(CurrentPos);
		}

		return GWormPathCache.Add(Worm.WormSeed, MoveTemp(Path));
	}

	/**
	 * Check whether a world position lies within tunnel radius of any
	 * worm in the pre-fetched list. Pre-fetched worms already have their
	 * paths cached/built (see GenerateChunk) - this is a pure distance
	 * check, safe to call per-voxel.
	 */
	bool IsTunnelVoxel(const FVector& WorldPos, const TArray<FWormSpawn>& PreFetchedWorms)
	{
		for (const FWormSpawn& Worm : PreFetchedWorms)
		{
			const TArray<FVector>& Path = GetOrBuildWormPath(Worm);

			for (const FVector& PathPos : Path)
			{
				if (FVector::DistSquared(WorldPos, PathPos) < FMath::Square(WORM_TUNNEL_RADIUS))
				{
					return true;
				}
			}
		}
		return false;
	}

	void DebugVisualizeWorms(
		int32 CenterX,
		int32 CenterZ,
		int32 AreaSize,
		uint32 WorldSeed,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig)
	{
		UE_LOG(LogTemp, Warning, TEXT("\n=== STEP 3: Worm Tunnel Path Debug Visualization ==="));
		UE_LOG(LogTemp, Warning, TEXT("Area: [%d, %d] to [%d, %d], Size: %d blocks"),
			CenterX - AreaSize/2, CenterZ - AreaSize/2, CenterX + AreaSize/2, CenterZ + AreaSize/2, AreaSize);

		int32 WormCount = 0;
		const int32 StartX = CenterX - AreaSize / 2;
		const int32 StartZ = CenterZ - AreaSize / 2;
		const int32 GridSteps = AreaSize / WORM_GRID_CELL_SIZE;

		TArray<FVector> AllPathPoints;

		for (int32 GX = 0; GX < GridSteps; ++GX)
		{
			for (int32 GZ = 0; GZ < GridSteps; ++GZ)
			{
				const int32 CellX = (StartX / WORM_GRID_CELL_SIZE) + GX;
				const int32 CellZ = (StartZ / WORM_GRID_CELL_SIZE) + GZ;

				const FWormSpawn Worm = GetWormAtCellCached(WorldSeed, CellX, CellZ, UpperConfig, LowerConfig);

				if (Worm.bExists)
				{
					WormCount++;
					UE_LOG(LogTemp, Warning, TEXT("  Worm %d: Start=(%.0f, %.0f, %.0f), Target=(%.0f, %.0f, %.0f)"),
						WormCount, Worm.StartPosition.X, Worm.StartPosition.Y, Worm.StartPosition.Z,
						Worm.TargetCavern.Center.X, Worm.TargetCavern.Center.Y, Worm.TargetCavern.Center.Z);

					const TArray<FVector>& Path = GetOrBuildWormPath(Worm);
					for (int32 Step = 0; Step < Path.Num(); Step += 10)
					{
						AllPathPoints.Add(Path[Step]);
					}
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("Results:"));
		UE_LOG(LogTemp, Warning, TEXT("  Total worms found: %d"), WormCount);
		UE_LOG(LogTemp, Warning, TEXT("  Total path points sampled: %d"), AllPathPoints.Num());
		UE_LOG(LogTemp, Warning, TEXT("=== Visualization Complete (use DrawDebugPoints in engine to visualize) ===\n"));
	}

	// Console command for worm visualization
	static FAutoConsoleCommand WormDebugCmd(
		TEXT("cav_DebugWorms"),
		TEXT("Visualize worm tunnel paths (args: CenterX CenterZ AreaSize WorldSeed, defaults: 0 0 2048 12345)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			int32 CenterX = 0, CenterZ = 0, AreaSize = 2048;
			uint32 WorldSeed = 12345;

			if (Args.Num() > 0) CenterX = FCString::Atoi(*Args[0]);
			if (Args.Num() > 1) CenterZ = FCString::Atoi(*Args[1]);
			if (Args.Num() > 2) AreaSize = FCString::Atoi(*Args[2]);
			if (Args.Num() > 3) WorldSeed = FCString::Atoi(*Args[3]);

			FCavernLayerConfig UpperConfig;
			UpperConfig.MinZ = 64;
			UpperConfig.MaxZ = 128;
			UpperConfig.LayoutThreshold = 0.4f;
			UpperConfig.TunnelBaseAltitude = 96;

			FCavernLayerConfig LowerConfig;
			LowerConfig.MinZ = -64;
			LowerConfig.MaxZ = 0;
			LowerConfig.LayoutThreshold = 0.4f;
			LowerConfig.TunnelBaseAltitude = -32;

			DebugVisualizeWorms(CenterX, CenterZ, AreaSize, WorldSeed, UpperConfig, LowerConfig);
		})
	);
}

// Perlin noise utility functions are now in LayerBase.h

// fBm over 3 octaves; result clamped to [0, 1].
// Sampling with a large offset so the cave pattern is independent of surface terrain.
static float SampleCaveNoise(float WX, float WY)
{
	static constexpr float NoiseOffset = 10000.5f;
	float Value = 0.f;
	float Amp   = 1.f;
	float Freq  = 1.f / 96.f;
	float Total = 0.f;

	for (int32 Oct = 0; Oct < 3; ++Oct)
	{
		Value += CavePerlin2D((WX + NoiseOffset) * Freq, (WY + NoiseOffset) * Freq) * Amp;
		Total += Amp;
		Amp  *= 0.5f;
		Freq *= 2.f;
	}

	return FMath::Clamp(Value / Total * 0.5f + 0.5f, 0.f, 1.f);
}

// ---------------------------------------------------------------------------
//  Zone indices  (LocalChunkZ within the Crystal Caves layer, 0 = shallowest)
// ---------------------------------------------------------------------------

// CEILING_SOLID_Z and CEILING_FRINGE_Z are now in LayerBase.h
// LocalChunkZ 2–5 : pure air (CEILING_FRINGE_Z+1 .. FLOOR_FRINGE_Z-1)
static constexpr int32 FLOOR_FRINGE_Z   = 6;
static constexpr int32 FLOOR_SOLID_Z    = 7;

// ---------------------------------------------------------------------------
//  GenerateChunk
// ---------------------------------------------------------------------------

void FCrystalCavesLevelGenerator::GenerateChunk(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ)
{
	// ===== DIAGNOSTIC: GenerateChunk call logging =====
	// Track how many chunks are being generated, in what range, and how long they take.
	// If freeze occurs, force-close and check Saved/Logs/CryptCraft.log to see call count/range.
	static int32 GenerateChunkCallCount = 0;
	GenerateChunkCallCount++;
	static double FirstCallTime = 0.0;
	if (FirstCallTime == 0.0) FirstCallTime = FPlatformTime::Seconds();
	
	const double ElapsedSeconds = FPlatformTime::Seconds() - FirstCallTime;
	UE_LOG(LogTemp, Warning, TEXT("GenerateChunk call #%d: [%d,%d,%d] (elapsed: %.2fs)"),
		GenerateChunkCallCount, GlobalChunkX, GlobalChunkY, LocalChunkZ, ElapsedSeconds);

	TArray<EBlockType> OutBlocks;

	// ---- Open air top (LocalChunkZ 0) for faster traversal ----
	if (LocalChunkZ == 0)
	{
		OutBlocks.Init(EBlockType::Air, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		Chunk.Initialize(OutBlocks);
		return;
	}

	// ---- Solid stone ceiling and floor ----------------------------------------
	if (LocalChunkZ == CEILING_SOLID_Z || LocalChunkZ == FLOOR_SOLID_Z)
	{
		OutBlocks.Init(EBlockType::Stone, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		Chunk.Initialize(OutBlocks);
		return;
	}

	// ---- Pure open air with cavern carving (LocalChunkZ 2..5) ------------------
	if (LocalChunkZ > CEILING_FRINGE_Z && LocalChunkZ < FLOOR_FRINGE_Z)
	{
		OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

		FCavernLayerConfig UpperConfig;
		UpperConfig.MinZ = 64;
		UpperConfig.MaxZ = 128;
		UpperConfig.LayoutThreshold = 0.4f;
		UpperConfig.TunnelBaseAltitude = 96;

		FCavernLayerConfig LowerConfig;
		LowerConfig.MinZ = -64;
		LowerConfig.MaxZ = 0;
		LowerConfig.LayoutThreshold = 0.4f;
		LowerConfig.TunnelBaseAltitude = -32;

		uint32 WorldSeed = 12345; // TODO: Get from world/game mode

		// ===== DIAGNOSTIC TIMING LOGGING START =====
		const double PrefetchStartTime = FPlatformTime::Seconds();

		// -----------------------------------------------------------
		// PREFETCH (once per chunk, NOT per voxel):
		// Gather nearby caverns and nearby worms for this chunk's
		// region. Search radius is padded by the chunk's half-diagonal
		// so we don't miss bubbles/worms whose influence reaches into
		// this chunk from just outside it.
		// -----------------------------------------------------------
		const FVector ChunkCenterCavernCoords(
			GlobalChunkX * CHUNK_SIZE_X + CHUNK_SIZE_X * 0.5f,
			0.0f, // unused by FindNearbyCaverns's distance check (XZ only matters via Center)
			GlobalChunkY * CHUNK_SIZE_Y + CHUNK_SIZE_Y * 0.5f
		);
		const float ChunkHalfDiagonal = FMath::Sqrt(
			FMath::Square(CHUNK_SIZE_X * 0.5f) + FMath::Square(CHUNK_SIZE_Y * 0.5f));

		TArray<FCavernBubble> ChunkNearbyCaverns;
		CavernLayout::FindNearbyCaverns(
			WorldSeed,
			ChunkCenterCavernCoords,
			CavernShape::SEARCH_RADIUS + ChunkHalfDiagonal,
			UpperConfig,
			LowerConfig,
			ChunkNearbyCaverns);

		// Gather nearby worm cells once for the chunk (3x3 neighborhood of
		// worm grid cells around the chunk center, using the cached lookup).
		TArray<FWormSpawn> ChunkNearbyWorms;
		{
			const FIntVector ChunkWormCell = TunnelWorms::WorldToWormCellCoord(
				GlobalChunkX * CHUNK_SIZE_X, GlobalChunkY * CHUNK_SIZE_Y);

			for (int32 DX = -1; DX <= 1; ++DX)
			{
				for (int32 DZ = -1; DZ <= 1; ++DZ)
				{
					const FWormSpawn Worm = TunnelWorms::GetWormAtCellCached(
						WorldSeed, ChunkWormCell.X + DX, ChunkWormCell.Z + DZ, UpperConfig, LowerConfig);

					if (Worm.bExists)
					{
						// Pre-build the path now so the per-voxel loop below
						// never triggers a first-time simulation mid-loop.
						TunnelWorms::GetOrBuildWormPath(Worm);
						ChunkNearbyWorms.Add(Worm);
					}
				}
			}
		}

		const double PrefetchEndTime = FPlatformTime::Seconds();
		const double PrefetchTime = (PrefetchEndTime - PrefetchStartTime) * 1000.0; // ms

		int32 CavernVoxelCount = 0;
		int32 TunnelVoxelCount = 0;

		const double VoxelLoopStartTime = FPlatformTime::Seconds();

		for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
		{
			for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
			{
				for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
				{
					const int32 WorldX = GlobalChunkX * CHUNK_SIZE_X + X;
					const int32 WorldY = GlobalChunkY * CHUNK_SIZE_Y + Y;
					const int32 WorldZ = LocalChunkZ * CHUNK_SIZE_Z + Z;

					// Cheap: just distance/sphere math against the small
					// pre-fetched list, no searching.
					const bool bIsCavern = CavernShape::IsCavernVoxel(
						WorldSeed,
						FIntVector(WorldX, WorldY, WorldZ),
						UpperConfig,
						LowerConfig,
						ChunkNearbyCaverns);

					if (bIsCavern)
					{
						CavernVoxelCount++;
					}

					bool bIsTunnel = false;
					// ===== DIAGNOSTIC: TUNNEL CARVING DISABLED =====
					// Temporarily disabling tunnel carving to isolate freeze cause.
					// If freeze disappears with this disabled, tunnels are the bottleneck (16,384 voxels × 3 worms × 501 path points = 24M distance checks per chunk).
					// Re-enable after applying bounding-box fix below.
					/*
					if (!bIsCavern)
					{
						// Cheap: distance check against pre-fetched, pre-built
						// worm paths. No per-voxel cell searching or simulation.
						const FVector VoxelWorldPos(WorldX, WorldZ, WorldY);
						bIsTunnel = TunnelWorms::IsTunnelVoxel(VoxelWorldPos, ChunkNearbyWorms);
						if (bIsTunnel)
						{
							TunnelVoxelCount++;
						}
					}
					*/

					const EBlockType BlockType = (bIsCavern || bIsTunnel)
						? EBlockType::Air
						: CavernShape::GetCavernBlockType(WorldSeed, FIntVector(WorldX, WorldY, WorldZ));
					OutBlocks[BlockIdx(X, Y, Z)] = BlockType;
				}
			}
		}

		const double VoxelLoopEndTime = FPlatformTime::Seconds();
		const double VoxelLoopTime = (VoxelLoopEndTime - VoxelLoopStartTime) * 1000.0; // ms

		// Log detailed timing and counts for diagnostics
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d,%d] Timings: Prefetch=%.2fms, VoxelLoop=%.2fms | Caverns=%d Worms=%d CavernVoxels=%d TunnelVoxels=%d"),
			GlobalChunkX, GlobalChunkY, LocalChunkZ,
			PrefetchTime, VoxelLoopTime,
			ChunkNearbyCaverns.Num(), ChunkNearbyWorms.Num(),
			CavernVoxelCount, TunnelVoxelCount);

		// ===== DIAGNOSTIC TIMING LOGGING END =====
		{
			UE_LOG(LogTemp, Warning, TEXT("CavernChunk [%d,%d,%d]: Carved %d cavern voxels, %d tunnel voxels"),
				GlobalChunkX, GlobalChunkY, LocalChunkZ, CavernVoxelCount, TunnelVoxelCount);
		}

		Chunk.Initialize(OutBlocks);
		return;
	}

	// ---- Fringe chunks (ceiling or floor) -------------------------------------
	OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

	const bool bCeiling = (LocalChunkZ == CEILING_FRINGE_Z);

	for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
	{
		for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
		{
			const float WX = static_cast<float>(GlobalChunkX * CHUNK_SIZE_X + X);
			const float WY = static_cast<float>(GlobalChunkY * CHUNK_SIZE_Y + Y);

			const int32 FringeBlocks = FMath::RoundToInt(SampleCaveNoise(WX, WY) * CHUNK_SIZE_Z);

			for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
			{
				EBlockType Type;

				if (bCeiling)
				{
					Type = (Z >= CHUNK_SIZE_Z - FringeBlocks) ? EBlockType::Stone : EBlockType::Air;
				}
				else
				{
					Type = (Z < FringeBlocks) ? EBlockType::Stone : EBlockType::Air;
				}

				OutBlocks[BlockIdx(X, Y, Z)] = Type;
			}
		}
	}

	Chunk.Initialize(OutBlocks);
}
