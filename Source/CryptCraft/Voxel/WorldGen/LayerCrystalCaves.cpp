#include "LayerCrystalCaves.h"
#include "LayerBase.h"
#include "Voxel/Chunk.h"

// ===========================================================================
//  CONFIG SECTION - Crystal Cavern Layer Structure
//  Edit these to change cavern layout, density, and appearance
// ===========================================================================

// Top Cavern Layer (0-2) - Cavern noise determines air or stone regions
struct FTopCavernZone
{
	static constexpr int32 START_CHUNK = 0;
	static constexpr int32 END_CHUNK   = 2;
	static constexpr const TCHAR* LAYER_TYPE = TEXT("cavern_noise");
	static constexpr EBlockType BLOCK_TYPE = EBlockType::Stone;
	static constexpr int32 CEILING_CHUNK = 0;  // Top surface at chunk 0
	static constexpr int32 FLOOR_CHUNK = 2;    // Bottom surface at chunk 2
};

// Top Floor Layer (3) - Solid Stone Boundary
struct FTopFloorZone
{
	static constexpr int32 START_CHUNK = 3;
	static constexpr int32 END_CHUNK   = 3;
	static constexpr const TCHAR* LAYER_TYPE = TEXT("solid");
	static constexpr EBlockType BLOCK_TYPE = EBlockType::Stone;
};

// Bottom Cavern Layer (4-6) - Cavern noise determines air or stone regions
struct FBottomCavernZone
{
	static constexpr int32 START_CHUNK = 4;
	static constexpr int32 END_CHUNK   = 6;
	static constexpr const TCHAR* LAYER_TYPE = TEXT("cavern_noise");
	static constexpr EBlockType BLOCK_TYPE = EBlockType::Stone;
	static constexpr int32 CEILING_CHUNK = 4;  // Top surface at chunk 4
	static constexpr int32 FLOOR_CHUNK = 6;    // Bottom surface at chunk 6
};

// Bottom Floor Layer (7) - Solid Stone Boundary
struct FBottomFloorZone
{
	static constexpr int32 START_CHUNK = 7;
	static constexpr int32 END_CHUNK   = 7;
	static constexpr const TCHAR* LAYER_TYPE = TEXT("solid");
	static constexpr EBlockType BLOCK_TYPE = EBlockType::Stone;
};

// Taper zone depth: applies to top and bottom cavern boundaries
static constexpr int32 TAPER_DEPTH = CHUNK_SIZE_Z;  // 32 blocks per chunk

// ---------------------------------------------------------------------------
//  BRIDGE/TUNNEL CONFIG - Unified bridge-in-air / tunnel-in-rock system
//  Set BRIDGES_ENABLED to false to disable entirely with no other changes needed
// ---------------------------------------------------------------------------
static constexpr bool BRIDGES_ENABLED = true;

static constexpr float BRIDGE_NOISE_FREQ_XY   = 0.002f;  // horizontal frequency — independent from air/stone region freq
static constexpr float BRIDGE_NOISE_OFFSET     = 5231.0f; // offset to decorrelate bridge noise from cavern walls
static constexpr float BRIDGE_THRESHOLD_BASE  = 0.006f;  // base thickness of bridge/tunnel ribbon in XY
static constexpr float BRIDGE_THRESHOLD_WALL  = 0.030f;  // thickness at wall contact (wider at walls)
static constexpr float BRIDGE_SLANT_FREQ      = 0.005f;  // frequency of height warping — controls slope rate
static constexpr float BRIDGE_SLANT_AMPLITUDE = 15.0f;   // max height variation in blocks (±15 blocks)
static constexpr float BRIDGE_BASE_MID_Z      = 0.5f;    // fraction of layer height for bridge floor (0.5 = middle)
static constexpr float BRIDGE_WALL_FALLOFF    = 0.04f;   // how quickly bridge narrows away from wall (higher = faster)
static constexpr float BRIDGE_ARCH_DEPTH      = 3.0f;   // vertical extent of bridge arch below MidZ
static constexpr float BRIDGE_ARCH_WALL_MULT  = 3.0f;    // arch extends 2x deeper at walls (drooping effect)
static constexpr float BRIDGE_TUNNEL_HEIGHT   = 8.0f;    // vertical extent of tunnel vault above MidZ

// ===========================================================================
//  End CONFIG Section
// ===========================================================================

// ===========================================================================
//  NOISE FUNCTIONS - Modular components for cavern generation
// ===========================================================================

// ---------------------------------------------------------------------------
//  Helper: 2D Noise Sampling
//  Returns base 2D Perlin noise for determining air vs. stone regions
// ---------------------------------------------------------------------------

static float Sample2DNoise(int32 X, int32 Z)
{
	const float Freq = 1.5f / 256.0f;  // 50% higher frequency for smaller cavern regions
	const float NoiseX = static_cast<float>(X) * Freq;
	const float NoiseZ = static_cast<float>(Z) * Freq;
	return CavePerlin2D(NoiseX, NoiseZ);  // Returns ~[-1, 1]
}

// ===========================================================================
//  DETERMINISTIC FEATURE PLACEMENT - Cell-based hole/spike/pillar generation
// ===========================================================================

struct FCaveFeature
{
	enum class EType { None, Hole, Spike, Pillar };
	FVector2D Center;
	float Radius;
	EType Type;
};

// Simple hash function for deterministic cell seeding
static uint32 HashCellSeed(uint32 WorldSeed, int32 CellX, int32 CellY)
{
	uint32 Hash = WorldSeed;
	Hash ^= (CellX * 73856093U);
	Hash ^= (CellY * 19349663U);
	Hash = (Hash >> 13) ^ Hash;
	Hash = Hash * (Hash * Hash * 15731 + 789221) + 1376312589;
	return Hash;
}

// Get the primary feature for a given cell (if one exists)
static FCaveFeature GetFeatureAtCell(uint32 WorldSeed, int32 CellX, int32 CellY, float CellSize)
{
	FCaveFeature Result;
	Result.Type = FCaveFeature::EType::None;
	Result.Radius = 0.0f;

	const uint32 CellSeed = HashCellSeed(WorldSeed, CellX, CellY);
	const float Roll = (CellSeed % 1000) / 1000.0f;

	// Feature distribution: 80% of cells have a feature (tuned for frequent spikes)
	if (Roll > 0.80f)
	{
		return Result;  // No feature in this cell
	}

	// Position: cell center + deterministic offset
	const float OffsetX = ((CellSeed ^ 0xABCD) % 100) / 100.0f - 0.5f;
	const float OffsetY = ((CellSeed ^ 0x1234) % 100) / 100.0f - 0.5f;
	Result.Center = FVector2D(
		CellX * CellSize + CellSize * 0.5f + OffsetX * CellSize * 0.4f,
		CellY * CellSize + CellSize * 0.5f + OffsetY * CellSize * 0.4f
	);

	// Feature type and radius from roll range
	// Spikes are very common, holes less so, pillars less frequent
	if (Roll < 0.08f)
	{
		Result.Type = FCaveFeature::EType::Hole;
		Result.Radius = 10.0f + ((CellSeed ^ 0x5678) % 100) / 100.0f * 10.0f;  // 10-20 blocks wide
	}
	else if (Roll < 0.70f)
	{
		Result.Type = FCaveFeature::EType::Spike;
		Result.Radius = 5.0f + ((CellSeed ^ 0x9ABC) % 100) / 100.0f * 5.0f;  // 5-10 blocks wide
	}
	else
	{
		Result.Type = FCaveFeature::EType::Pillar;
		Result.Radius = 5.0f + ((CellSeed ^ 0xDEF0) % 100) / 100.0f * 10.0f;  // 5-15 blocks wide (thinner)
	}

	return Result;
}

// ---------------------------------------------------------------------------
//  Fractional Brownian Motion (fBm) - Multi-octave Perlin noise blending
//  Combines multiple noise octaves for smoother, more varied terrain
//  Parameters: X, Z coordinates; InitialFrequency (e.g., 1/64); NumOctaves;
//             Persistence (amplitude scale per octave, ~0.5); Lacunarity (freq scale, ~2.0)
// ---------------------------------------------------------------------------

static float fBm(float X, float Z, float InitialFrequency, int32 NumOctaves, float Persistence, float Lacunarity)
{
	float Result = 0.0f;
	float Amplitude = 1.0f;
	float MaxAmplitude = 0.0f;
	float Frequency = InitialFrequency;

	for (int32 i = 0; i < NumOctaves; ++i)
	{
		Result += CavePerlin2D(X * Frequency, Z * Frequency) * Amplitude;
		MaxAmplitude += Amplitude;
		
		Amplitude *= Persistence;
		Frequency *= Lacunarity;
	}

	return Result / MaxAmplitude;  // Normalize to [-1, 1]
}

// Sample unified height with cell-based features (holes, spikes, pillars) and gentle hills
float FCrystalCavesLevelGenerator::SampleUnifiedHeight(int32 X, int32 Z, uint32 FeatureSeedVariant) const
{
	const float CellSize = 64.0f;
	const float BaseHeight = 10.0f;

	// Use fBm for smoother, multi-octave hill variation
	float HillNoise = (fBm(static_cast<float>(X), static_cast<float>(Z), 1.0f / 64.0f, 3, 0.5f, 2.0f) + 1.0f) * 0.5f;
	float AdjustedNoise = (HillNoise - 0.5f) * 2.0f + 0.5f;  // Double sensitivity around midpoint
	float HillHeight = BaseHeight + AdjustedNoise * 12.0f;

	int32 CellX = FMath::FloorToInt(static_cast<float>(X) / CellSize);
	int32 CellY = FMath::FloorToInt(static_cast<float>(Z) / CellSize);

	// First pass: Check for pillars using BASE seed (consistent floor/ceiling alignment)
	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			FCaveFeature PillarCheck = GetFeatureAtCell(WorldSeed, CellX + DX, CellY + DY, CellSize);
			if (PillarCheck.Type != FCaveFeature::EType::Pillar) continue;

			float Dist = FMath::Sqrt(
				FMath::Square(static_cast<float>(X) - PillarCheck.Center.X) +
				FMath::Square(static_cast<float>(Z) - PillarCheck.Center.Y)
			);

			if (Dist > PillarCheck.Radius) continue;

			const float T = 1.0f - (Dist / PillarCheck.Radius);
			const float Smooth = (1.0f - FMath::Cos(T * PI)) * 0.5f;

			uint32 PillarSeed = HashCellSeed(WorldSeed, CellX + DX, CellY + DY);
			float PillarHeight = 50.0f + ((PillarSeed >> 16) % 1000) / 1000.0f * 10.0f;
			return HillHeight + PillarHeight * FMath::Pow(Smooth, 1.5f);
		}
	}

	// Second pass: Check for spikes/holes using VARIANT seed (floor/ceiling can differ)
	uint32 VariantSeed = WorldSeed + FeatureSeedVariant;
	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			FCaveFeature Feature = GetFeatureAtCell(VariantSeed, CellX + DX, CellY + DY, CellSize);
			if (Feature.Type == FCaveFeature::EType::None || Feature.Type == FCaveFeature::EType::Pillar) continue;

			float Dist = FMath::Sqrt(
				FMath::Square(static_cast<float>(X) - Feature.Center.X) +
				FMath::Square(static_cast<float>(Z) - Feature.Center.Y)
			);

			if (Dist > Feature.Radius) continue;

			const float T = 1.0f - (Dist / Feature.Radius);
			const float Smooth = (1.0f - FMath::Cos(T * PI)) * 0.5f;

			if (Feature.Type == FCaveFeature::EType::Hole)
			{
				return FMath::Lerp(HillHeight, 0.0f, Smooth);
			}
			else if (Feature.Type == FCaveFeature::EType::Spike)
			{
				uint32 CellSeed = HashCellSeed(VariantSeed, CellX + DX, CellY + DY);
				float HeightMult = 0.6f + ((CellSeed >> 16) % 1000) / 1000.0f * 0.8f;  // 0.6-1.4 uniform range
				float SpikeHeightAddition = 30.0f * HeightMult;
				return HillHeight + SpikeHeightAddition * FMath::Pow(Smooth, 2.0f);
			}
		}
	}

	return HillHeight;
}

// ---------------------------------------------------------------------------
//  GenerateBridgeTunnel - Unified bridge-in-air / tunnel-in-rock system
//
//  Returns a block override if this voxel is part of a bridge/tunnel,
//  or EBlockType::MAX as a sentinel meaning "no override, use default logic."
//
//  How it works:
//  - 2D path-finding (XY noise) determines WHERE the bridge/tunnel exists horizontally
//  - Cosine profile (pure math) determines SHAPE: rounded arch below MidZ, rounded vault above
//  - In AIR regions:   solid arch below MidZ, tapers to zero at MidZ - BRIDGE_ARCH_DEPTH
//  - In ROCK regions:  air void above MidZ, tapers to zero at MidZ + BRIDGE_TUNNEL_HEIGHT
//  - MidZ is warped by low-frequency slanting noise → bridges rise and fall across landscape
//  - Path thickness is modulated by distance from air/rock boundary → wide at walls, thin in open space
// ---------------------------------------------------------------------------

static bool GenerateBridgeTunnel(
	float RegionNoise,
	float VoxelHeightFromFloor,
	int32 TotalCavernHeight,
	int32 WorldX,
	int32 WorldY,
	EBlockType SolidBlockType,
	EBlockType& OutBlockType)
{
	if (!BRIDGES_ENABLED)
	{
		return false; // bridges disabled, no override
	}

	// --- Slanting floor height ---
	// Low-frequency 2D noise makes bridges rise and fall across the landscape
	float SlantNoise = CavePerlin2D(WorldX * BRIDGE_SLANT_FREQ, WorldY * BRIDGE_SLANT_FREQ);
	float MidZ = TotalCavernHeight * BRIDGE_BASE_MID_Z + SlantNoise * BRIDGE_SLANT_AMPLITUDE;

	// --- XY path detection (2D noise only) ---
	// This determines WHERE the bridge/tunnel path exists in the horizontal plane
	// Use offset to decorrelate from cavern wall noise
	float PathNoise = CavePerlin2D(
		WorldX * BRIDGE_NOISE_FREQ_XY + BRIDGE_NOISE_OFFSET,
		WorldY * BRIDGE_NOISE_FREQ_XY + BRIDGE_NOISE_OFFSET);

	// --- Width modulation: wide at air/rock boundary, thin toward cavern center ---
	// RegionNoise is near 0 at boundary, larger positive deep in air, larger negative deep in rock
	float BoundaryDist = FMath::Abs(RegionNoise) * 100.0f; // 0 at boundary, increases away from it
	float WallProximity = FMath::Exp(-BoundaryDist * BRIDGE_WALL_FALLOFF); // 1.0 at wall, 0 toward center

	// Blend threshold between base (thin, open space) and wall (wide, at contact)
	float Threshold = FMath::Lerp(BRIDGE_THRESHOLD_BASE, BRIDGE_THRESHOLD_WALL, WallProximity);

	// --- Is this XY position on a bridge/tunnel path? ---
	bool bOnPath = FMath::Abs(PathNoise) < Threshold;
	if (!bOnPath)
	{
		return false; // not on any bridge/tunnel path, no override
	}

	// --- Vertical profile: deterministic falloff from MidZ ---
	float DistFromMid = VoxelHeightFromFloor - MidZ;
	bool bIsAir = RegionNoise > 0.0f;

	if (bIsAir)
	{
		// Bridge deck in air region: solid arch below MidZ
		if (DistFromMid >= 0.0f)
		{
			return false; // above MidZ in air region, no bridge here
		}
		
		// Below MidZ: cosine arch profile tapers from MidZ down to MidZ - LocalArchDepth
		// Arch extends deeper at walls (WallProximity=1) than in open space (WallProximity=0)
		float ArchDepthAtWall = BRIDGE_ARCH_DEPTH * BRIDGE_ARCH_WALL_MULT;
		float LocalArchDepth = BRIDGE_ARCH_DEPTH + (ArchDepthAtWall - BRIDGE_ARCH_DEPTH) * WallProximity;
		float ArchProfile = FMath::Cos((FMath::Abs(DistFromMid) / LocalArchDepth) * PI * 0.5f);
		float LocalThreshold = Threshold * ArchProfile;
		
		if (FMath::Abs(PathNoise) < LocalThreshold)
		{
			OutBlockType = SolidBlockType;
			return true;
		}
	}
	else
	{
		// Tunnel void in rock region: hollow above MidZ
		if (DistFromMid <= 0.0f)
		{
			return false; // below MidZ in rock region, no tunnel here
		}
		
		// Above MidZ: cosine profile tapers from MidZ up to MidZ + BRIDGE_TUNNEL_HEIGHT
		float TunnelProfile = FMath::Cos((DistFromMid / BRIDGE_TUNNEL_HEIGHT) * PI * 0.5f);
		float LocalThreshold = Threshold * TunnelProfile;
		
		if (FMath::Abs(PathNoise) < LocalThreshold)
		{
			OutBlockType = EBlockType::Air;
			return true;
		}
	}

	return false; // on path but outside active vertical zones
}

// ---------------------------------------------------------------------------
//  Composite: Cavern Noise Generation
//  Combines 2D noise (air/stone regions) + height noise (surface slopes)
//  Parameters: Ceiling and floor chunk indices for surface detection
// ---------------------------------------------------------------------------

EBlockType FCrystalCavesLevelGenerator::GenerateNoise2DCavern(
	int32 X,
	int32 Z,
	int32 LocalChunkZ,
	int32 VoxelZ,
	int32 CeilingChunk,
	int32 FloorChunk,
	EBlockType SolidBlockType) const
{
	float BaseNoise = Sample2DNoise(X, Z);

	// Calculate global voxel Z across entire cavern layer (0 at ceiling, increases downward)
	int32 GlobalVoxelZ = (LocalChunkZ - CeilingChunk) * CHUNK_SIZE_Z + VoxelZ;
	int32 TotalCavernHeight = (FloorChunk - CeilingChunk + 1) * CHUNK_SIZE_Z;

	// Apply Z-variation taper only at boundaries in top (0-15) and bottom (height-16 to height) zones
	float ZVariation = 0.0f;
	if (FMath::Abs(BaseNoise * 100.0f) < 15.0f)
	{
		float TaperStrength = 0.0f;
		
		// Top zone: maximum taper at ceiling, fade toward middle
		if (GlobalVoxelZ < TAPER_DEPTH)
		{
			float DistanceFromTop = static_cast<float>(GlobalVoxelZ);
			float TaperLerp = DistanceFromTop / static_cast<float>(TAPER_DEPTH);  // 0.0 at top (max strength), 1.0 at zone edge (min strength)
			TaperStrength = (-FMath::Cos(TaperLerp * PI) + 1.0f) * 0.5f;  // Negative cosine curve
		}
		// Bottom zone: maximum taper at floor, fade toward middle
		else if (GlobalVoxelZ >= (TotalCavernHeight - TAPER_DEPTH))
		{
			float DistanceFromBottom = TotalCavernHeight - GlobalVoxelZ;
			float TaperLerp = DistanceFromBottom / static_cast<float>(TAPER_DEPTH);  // 0.0 at bottom (max strength), 1.0 at zone edge (min strength)
			TaperStrength = (-FMath::Cos(TaperLerp * PI) + 1.0f) * 0.5f;  // Negative cosine curve
		}
		
		if (TaperStrength > 0.0f)
		{
			// Only apply tapering if we're actually in a taper zone
			float TaperingNoise = fBm(static_cast<float>(X), static_cast<float>(GlobalVoxelZ), 1.0f / 128.0f, 3, 0.5f, 2.0f);
			float NormalizedNoise = (TaperingNoise + 1.0f) * 0.5f;  // [0, 1]
			float BaseTaper = 10.0f + NormalizedNoise * 10.0f;  // [10, 20] base range, negated to [-20, -10]
			ZVariation = -BaseTaper * TaperStrength;  // Scale by taper strength curve
		}
	}

	// --- Bridge/tunnel pass (highest priority, runs before floor/ceiling features) ---
	const int32 VoxelHeightFromFloor = (FloorChunk - LocalChunkZ) * CHUNK_SIZE_Z + VoxelZ;
	EBlockType BridgeBlockType = EBlockType::Air;
	if (GenerateBridgeTunnel(
		BaseNoise,
		static_cast<float>(VoxelHeightFromFloor),
		TotalCavernHeight,
		X, Z,
		SolidBlockType,
		BridgeBlockType))
	{
		return BridgeBlockType; // bridge/tunnel overrides everything else
	}

	// Check if this voxel is in the air region (with height-varying boundary at edges)
	if (BaseNoise * 100.0f + ZVariation <= 0.0f)
	{
		return SolidBlockType;
	}

	float FloorHeight = SampleUnifiedHeight(X, Z, 0);  // Floor uses base seed (no variant)
	if (VoxelHeightFromFloor <= (int32)FloorHeight)
	{
		return SolidBlockType;
	}

	// Height from ceiling in blocks, regardless of which chunk we're in
	const int32 VoxelHeightFromCeiling = (LocalChunkZ - CeilingChunk) * CHUNK_SIZE_Z + (CHUNK_SIZE_Z - 1 - VoxelZ);

	float CeilingHeight = SampleUnifiedHeight(X, Z, 7919);  // Ceiling uses variant seed for spike variation
	if (VoxelHeightFromCeiling <= (int32)CeilingHeight)
	{
		return SolidBlockType;
	}

	return EBlockType::Air;
}

// ===========================================================================
//  END NOISE FUNCTIONS
// ===========================================================================

// ---------------------------------------------------------------------------
//  GenerateBlocks - Fill a chunk with appropriate block types based on zone
// ---------------------------------------------------------------------------

void FCrystalCavesLevelGenerator::GenerateBlocks(
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 LocalChunkZ,
	TArray<EBlockType>& OutBlocks)
{
	OutBlocks.Init(EBlockType::Stone, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

	// Determine which zone this chunk belongs to and handle accordingly
	if (LocalChunkZ >= FTopCavernZone::START_CHUNK && LocalChunkZ <= FTopCavernZone::END_CHUNK)
	{
		// Top cavern: check layer type
		if (FCString::Strcmp(FTopCavernZone::LAYER_TYPE, TEXT("cavern_noise")) == 0)
		{
			const int32 WorldX = GlobalChunkX * CHUNK_SIZE_X;
			const int32 WorldY = GlobalChunkY * CHUNK_SIZE_Y;

			for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
			{
				for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
				{
					const int32 WorldPosX = WorldX + X;
					const int32 WorldPosZ = WorldY + Y;

					for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
					{
						EBlockType BlockType = GenerateNoise2DCavern(
							WorldPosX, WorldPosZ, LocalChunkZ, Z,
							FTopCavernZone::CEILING_CHUNK, FTopCavernZone::FLOOR_CHUNK,
							FTopCavernZone::BLOCK_TYPE);
						OutBlocks[BlockIdx(X, Y, Z)] = BlockType;
					}
				}
			}
		}
		else
		{
			OutBlocks.Init(FTopCavernZone::BLOCK_TYPE, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		}
	}
	else if (LocalChunkZ >= FTopFloorZone::START_CHUNK && LocalChunkZ <= FTopFloorZone::END_CHUNK)
	{
		OutBlocks.Init(FTopFloorZone::BLOCK_TYPE, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
	}
	else if (LocalChunkZ >= FBottomCavernZone::START_CHUNK && LocalChunkZ <= FBottomCavernZone::END_CHUNK)
	{
		// Bottom cavern: check layer type
		if (FCString::Strcmp(FBottomCavernZone::LAYER_TYPE, TEXT("cavern_noise")) == 0)
		{
			const int32 WorldX = GlobalChunkX * CHUNK_SIZE_X;
			const int32 WorldY = GlobalChunkY * CHUNK_SIZE_Y;

			for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
			{
				for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
				{
					const int32 WorldPosX = WorldX + X;
					const int32 WorldPosZ = WorldY + Y;

					for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
					{
						EBlockType BlockType = GenerateNoise2DCavern(
							WorldPosX, WorldPosZ, LocalChunkZ, Z,
							FBottomCavernZone::CEILING_CHUNK, FBottomCavernZone::FLOOR_CHUNK,
							FBottomCavernZone::BLOCK_TYPE);
						OutBlocks[BlockIdx(X, Y, Z)] = BlockType;
					}
				}
			}
		}
		else
		{
			OutBlocks.Init(FBottomCavernZone::BLOCK_TYPE, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		}
	}
	else if (LocalChunkZ >= FBottomFloorZone::START_CHUNK && LocalChunkZ <= FBottomFloorZone::END_CHUNK)
	{
		OutBlocks.Init(FBottomFloorZone::BLOCK_TYPE, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
	}
}

// ---------------------------------------------------------------------------
//  GenerateChunk - Wrapper that calls GenerateBlocks then initializes chunk
// ---------------------------------------------------------------------------

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
