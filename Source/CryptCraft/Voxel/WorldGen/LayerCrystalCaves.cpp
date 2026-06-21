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

	static FORCEINLINE FIntVector WorldToCellCoord(int32 WorldX, int32 WorldZ)
	{
		return FIntVector(
			FMath::FloorToInt(static_cast<float>(WorldX) / CAVERN_GRID_CELL_SIZE),
			0,
			FMath::FloorToInt(static_cast<float>(WorldZ) / CAVERN_GRID_CELL_SIZE)
		);
	}

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

		const ECavernLayer Layer = GetLayerForColumn(WorldSeed, (int32)CellCenterX, (int32)CellCenterZ);
		const FCavernLayerConfig& Config = (Layer == ECavernLayer::Upper) ? UpperConfig : LowerConfig;

		if (LayoutNoise <= Config.LayoutThreshold)
		{
			return Result;
		}

		Result.bExists = true;
		Result.Layer = Layer;

		const float OffsetX = (LayoutNoise - 0.5f) * (CAVERN_GRID_CELL_SIZE * 0.3f);
		const float OffsetZ = CaveNoise::SeededNoise2D(
			CellSeed ^ 0xDEADBEEFu,
			CellCenterX * LAYOUT_NOISE_FREQUENCY,
			CellCenterZ * LAYOUT_NOISE_FREQUENCY
		) - 0.5f;
		const float OffsetZValue = OffsetZ * (CAVERN_GRID_CELL_SIZE * 0.3f);

		Result.Center = FVector(CellCenterX + OffsetX, (Config.MinZ + Config.MaxZ) * 0.5f, CellCenterZ + OffsetZValue);

		const float RadiusNoise = CaveNoise::SeededNoise1D(
			CellSeed ^ 0x12345678u,
			static_cast<float>(CellX ^ CellZ) * 0.1f
		);
		Result.Radius = BUBBLE_RADIUS_BASE + (RadiusNoise - 0.5f) * 2.0f * BUBBLE_RADIUS_VARIATION;

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

				const FCavernBubble Bubble = GetBubbleAtCell(WorldSeed, CellX, CellZ, UpperConfig, LowerConfig);

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

				const FCavernBubble Bubble = GetBubbleAtCell(WorldSeed, CellX, CellZ, UpperConfig, LowerConfig);

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
	static constexpr float DENSITY_THRESHOLD = 0.4f;
	static constexpr float SEARCH_RADIUS = 80.0f;
	static constexpr float GEM_SPAWN_CHANCE = 0.02f;  // 2% per gem type

	bool IsCavernVoxel(
		int32 WorldSeed,
		FIntVector WorldPos,
		const FCavernLayerConfig& UpperConfig,
		const FCavernLayerConfig& LowerConfig)
	{
		// Step 1: Determine which layer is active for this (X, Y) horizontal column
		// NOTE: Cavern functions use "X, Z" naming but really mean the 2D horizontal plane
		// In UE5 coords: X, Y are horizontal; Z is vertical
		const ECavernLayer Layer = CavernLayout::GetLayerForColumn(WorldSeed, WorldPos.X, WorldPos.Y);
		const FCavernLayerConfig& LayerConfig = (Layer == ECavernLayer::Upper) ? UpperConfig : LowerConfig;

		// Step 2: Check if Z (vertical) is within this layer's range
		if (WorldPos.Z < LayerConfig.MinZ || WorldPos.Z > LayerConfig.MaxZ)
		{
			return false;
		}

		// Step 3: Find nearby cavern bubbles
		// Cavern system expects (X_horizontal, Y_ignored, Z_horizontal) format
		FVector CavernWorldPos = FVector(
			static_cast<float>(WorldPos.X),
			0.f,
			static_cast<float>(WorldPos.Y)  // Cavern system uses "Z" field for second horizontal axis
		);

		TArray<FCavernBubble> NearbyCaverns;
		CavernLayout::FindNearbyCaverns(
			WorldSeed,
			CavernWorldPos,
			SEARCH_RADIUS,
			UpperConfig,
			LowerConfig,
			NearbyCaverns);

		// Step 4: For each nearby bubble, evaluate 3D density noise
		for (const FCavernBubble& Bubble : NearbyCaverns)
		{
			// Distance from voxel to bubble center
			// Bubble.Center is in cavern coords: (X_h, Y_vertical, Z_h)
			// Our voxel is (X_h, Y_h, Z_v)
			const FVector VoxelInCavernCoords = FVector(
				static_cast<float>(WorldPos.X),
				static_cast<float>(WorldPos.Z),  // vertical
				static_cast<float>(WorldPos.Y)   // second horizontal axis
			);

			const float DistToCenter = FVector::Dist(VoxelInCavernCoords, Bubble.Center);

			// Only evaluate density if voxel is within bubble radius
			if (DistToCenter < Bubble.Radius)
			{
				// Evaluate 3D Perlin noise at this position
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

				const float Density = CaveNoise::SeededNoise3D(BubbleSeed, NoisePos);

				// Step 5: If density above threshold, this voxel is carved air
				if (Density > DENSITY_THRESHOLD)
				{
					return true;
				}
			}
		}

		return false;
	}

	/**
	 * Get the block type for a cavern voxel (stone or gem).
	 * Determines if a stone voxel should be replaced with a glowing gem.
	 */
	EBlockType GetCavernBlockType(
		int32 WorldSeed,
		FIntVector WorldPos)
	{
		// Use seeded noise to deterministically place gems
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
			
			UE_LOG(LogTemp, Warning, TEXT("CavernGem: Placed %d at (%d, %d, %d), GemRoll=%.3f, GemType=%.3f"), 
				static_cast<int32>(GemBlock), WorldPos.X, WorldPos.Y, WorldPos.Z, GemRoll, GemType);
			return GemBlock;
		}

		return EBlockType::Stone;
	}
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
//  GenerateBlocks / GenerateChunk
// ---------------------------------------------------------------------------

void FCrystalCavesLevelGenerator::GenerateBlocks(
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ,
	TArray<EBlockType>& OutBlocks)
{

	// ---- Solid stone ceiling and floor ----------------------------------------
	if (LocalChunkZ == CEILING_SOLID_Z || LocalChunkZ == FLOOR_SOLID_Z)
	{
		OutBlocks.Init(EBlockType::Stone, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		return;
	}

	// ---- Pure open air with cavern carving (LocalChunkZ 2..5) ------------------
	if (LocalChunkZ > CEILING_FRINGE_Z && LocalChunkZ < FLOOR_FRINGE_Z)
	{
		OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

		// Layer configs for cavern carving
		FCavernLayerConfig UpperConfig;
		UpperConfig.MinZ = 64;
		UpperConfig.MaxZ = 128;
		UpperConfig.LayoutThreshold = 0.4f;

		FCavernLayerConfig LowerConfig;
		LowerConfig.MinZ = -64;
		LowerConfig.MaxZ = 0;
		LowerConfig.LayoutThreshold = 0.4f;

		uint32 WorldSeed = 12345; // TODO: Get from world/game mode

		for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
		{
			for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
			{
				for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
				{
					const int32 WorldX = GlobalChunkX * CHUNK_SIZE_X + X;
					const int32 WorldY = GlobalChunkY * CHUNK_SIZE_Y + Y;
					const int32 WorldZ = LocalChunkZ * CHUNK_SIZE_Z + Z;

					const bool bIsCavern = CavernShape::IsCavernVoxel(
						WorldSeed,
						FIntVector(WorldX, WorldY, WorldZ),
						UpperConfig,
						LowerConfig);

				// Use gems for stone blocks in caverns, plain stone elsewhere
				const EBlockType BlockType = bIsCavern ? EBlockType::Air : CavernShape::GetCavernBlockType(WorldSeed, FIntVector(WorldX, WorldY, WorldZ));
				OutBlocks[BlockIdx(X, Y, Z)] = BlockType;
				}
			}
		}

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

			// How many blocks of stone this column contributes (0..32).
			const int32 FringeBlocks = FMath::RoundToInt(SampleCaveNoise(WX, WY) * CHUNK_SIZE_Z);

			for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
			{
				EBlockType Type;

				if (bCeiling)
				{
					// Z=31 is the shallowest face, adjacent to solid ceiling.
					// Stone hangs DOWN: fill the top FringeBlocks of the chunk.
					Type = (Z >= CHUNK_SIZE_Z - FringeBlocks) ? EBlockType::Stone : EBlockType::Air;
				}
				else
				{
					// Z=0 is the deepest face, adjacent to solid floor.
					// Stone rises UP: fill the bottom FringeBlocks of the chunk.
					Type = (Z < FringeBlocks) ? EBlockType::Stone : EBlockType::Air;
				}

				OutBlocks[BlockIdx(X, Y, Z)] = Type;
			}
		}
	}
}

void FCrystalCavesLevelGenerator::GenerateChunk(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ)
{
	TArray<EBlockType> Blocks;
	GenerateBlocks(GlobalChunkX, GlobalChunkY, LocalChunkZ, Blocks);
	Chunk.Initialize(Blocks);
}
