// VoxelGenLayers.cpp – Implementation of layer-based world generation

#include "VoxelGenLayers.h"
#include "VoxelWorld.h"
#include "Math/RandomStream.h"

// ---------------------------------------------------------------------------
//  Layer definition setup
// ---------------------------------------------------------------------------

void FVoxelGenLayers::InitializeLayerDefinitions(TArray<FLayerDefinition>& OutLayers)
{
	OutLayers.Empty();

	// Surface layer: grass and dirt with high Perlin variation
	{
		FLayerDefinition Surface;
		Surface.Name = TEXT("Surface");
		Surface.Height = CHUNK_SIZE_Z;
		Surface.PrimaryBlock = EBlockType::Grass;
		Surface.SecondaryBlock = EBlockType::Dirt;
		Surface.SecondaryPercent = 0.f;  // Mostly grass (dirt added by layer boundary)
		Surface.bHasCaverns = false;
		Surface.bHasOres = false;
		OutLayers.Add(Surface);
	}

	// Layer 1: Upper stone with coal (will add ore generation later)
	{
		FLayerDefinition L1;
		L1.Name = TEXT("Shallow Stone");
		L1.Height = CHUNK_SIZE_Z;
		L1.PrimaryBlock = EBlockType::Stone;
		L1.SecondaryBlock = EBlockType::Gravel;
		L1.SecondaryPercent = 15.f;
		L1.bHasCaverns = false;
		L1.bHasOres = true;  // Coal, Iron will go here (future)
		OutLayers.Add(L1);
	}

	// Layer 2: Deep stone with varied ore
	{
		FLayerDefinition L2;
		L2.Name = TEXT("Deep Stone");
		L2.Height = CHUNK_SIZE_Z;
		L2.PrimaryBlock = EBlockType::Stone;
		L2.SecondaryBlock = EBlockType::Gravel;
		L2.SecondaryPercent = 25.f;
		L2.bHasCaverns = false;
		L2.bHasOres = true;  // Gold, Silver, etc.
		OutLayers.Add(L2);
	}

	// Layer 3: Gem layer with high ore concentration
	{
		FLayerDefinition L3;
		L3.Name = TEXT("Gem Layer");
		L3.Height = CHUNK_SIZE_Z;
		L3.PrimaryBlock = EBlockType::Stone;
		L3.SecondaryBlock = EBlockType::Sand;  // Different visual character
		L3.SecondaryPercent = 20.f;
		L3.bHasCaverns = true;  // Caverns start appearing here
		L3.bHasOres = true;     // Diamond, Emerald, Ruby, etc.
		OutLayers.Add(L3);
	}

	// Layer 4: Bedrock / Void layer (bottom of world)
	{
		FLayerDefinition Bedrock;
		Bedrock.Name = TEXT("Bedrock");
		Bedrock.Height = CHUNK_SIZE_Z;
		Bedrock.PrimaryBlock = EBlockType::Bedrock;
		Bedrock.SecondaryBlock = EBlockType::Bedrock;
		Bedrock.SecondaryPercent = 0.f;  // 100% bedrock
		Bedrock.bHasCaverns = false;
		Bedrock.bHasOres = false;
		OutLayers.Add(Bedrock);
	}
}

// ---------------------------------------------------------------------------
//  Main generation
// ---------------------------------------------------------------------------

void FVoxelGenLayers::GenerateLayeredChunkData(
	FIntVector ChunkCoord,
	TArray<EBlockType>& OutBlocks,
	const TArray<FLayerDefinition>& Layers)
{
	OutBlocks.SetNum(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

	// Map chunk Z coordinate to layer index
	// Chunk Z < 0: empty/sky
	// Chunk Z = 0: Layer 0 (Surface)
	// Chunk Z = 1: Layer 1 (Shallow Stone)
	// etc.
	int32 LayerIndex = ChunkCoord.Z;
	
	UE_LOG(LogTemp, Warning, TEXT("GenerateLayeredChunkData: ChunkCoord=(%d,%d,%d), LayerIndex=%d, NumLayers=%d"), 
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, LayerIndex, Layers.Num());
	
	// Handle chunks above/below the layer system
	if (LayerIndex < 0)
	{
		// Above-ground chunks: all air
		for (int32 i = 0; i < OutBlocks.Num(); ++i)
		{
			OutBlocks[i] = EBlockType::Air;
		}
		UE_LOG(LogTemp, Warning, TEXT("  → Generated as AIR (above layers)"));
		return;
	}

	if (LayerIndex >= Layers.Num())
	{
		// Below all layers: solid bedrock
		for (int32 i = 0; i < OutBlocks.Num(); ++i)
		{
			OutBlocks[i] = EBlockType::Bedrock;
		}
		UE_LOG(LogTemp, Warning, TEXT("  → Generated as BEDROCK (below layers)"));
		return;
	}

	// Generate the appropriate layer
	for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
	{
		for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
		{
			for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
			{
				const float GlobalX = static_cast<float>(ChunkCoord.X * CHUNK_SIZE_X + X);
				const float GlobalY = static_cast<float>(ChunkCoord.Y * CHUNK_SIZE_Y + Y);

				EBlockType Block = GetBlockForLayerVoxel(X, Y, Z, GlobalX, GlobalY, LayerIndex, Layers);
				OutBlocks[X + CHUNK_SIZE_X * (Y + CHUNK_SIZE_Y * Z)] = Block;
			}
		}
	}

	// Second pass: place objects (boulders, ore veins, etc.)
	PlaceLayerObjects(ChunkCoord, LayerIndex, OutBlocks, Layers);
	UE_LOG(LogTemp, Warning, TEXT("  → Generated layer %d: %s"), LayerIndex, *Layers[LayerIndex].Name);
}

// ---------------------------------------------------------------------------
//  Per-voxel block determination
// ---------------------------------------------------------------------------

EBlockType FVoxelGenLayers::GetBlockForLayerVoxel(
	int32 LocalX, int32 LocalY, int32 LocalZ,
	float GlobalX, float GlobalY,
	int32 LayerIndex,
	const TArray<FLayerDefinition>& Layers)
{
	if (!Layers.IsValidIndex(LayerIndex))
	{
		return EBlockType::Air;  // Out of bounds
	}

	const FLayerDefinition& Layer = Layers[LayerIndex];

	// For now: simple deterministic layer composition (no Perlin noise yet)
	// Future: add Perlin variation within each layer

	// Layer 0 is special: surface with grass top, dirt below
	if (LayerIndex == 0)
	{
		// Top few blocks of surface layer
		if (LocalZ >= CHUNK_SIZE_Z - 5)
		{
			return (LocalZ == CHUNK_SIZE_Z - 1) ? EBlockType::Grass : EBlockType::Dirt;
		}
		return Layer.PrimaryBlock;  // Rest is stone
	}

	// Other layers: primary with secondary mixed in
	// Use deterministic hash for placement
	uint32 Hash = (uint32)(LocalX * 73856093u) 
	            ^ (uint32)(LocalY * 19349663u) 
	            ^ (uint32)(LocalZ * 83492791u)
	            ^ (uint32)(LayerIndex * 2654435761u);
	uint8 HashByte = (Hash >> 8) & 0xFF;
	float HashPercent = HashByte / 255.f;

	if (HashPercent < (Layer.SecondaryPercent / 100.f))
	{
		return Layer.SecondaryBlock;
	}
	return Layer.PrimaryBlock;
}

// ---------------------------------------------------------------------------
//  Layer boundary calculation (for smooth transitions)
// ---------------------------------------------------------------------------

int32 FVoxelGenLayers::GetLayerBoundaryZ(
	int32 LayerIndex,
	float WorldX, float WorldY,
	const TArray<FLayerDefinition>& Layers)
{
	// Calculate the starting Z of this layer
	// Layer 0 starts at some surface height, Layer 1 below it, etc.

	if (LayerIndex < 0 || LayerIndex >= Layers.Num())
	{
		return 0;
	}

	int32 BoundaryZ = 0;

	// Sum up all layer heights up to this layer
	for (int32 i = 0; i < LayerIndex; ++i)
	{
		BoundaryZ -= Layers[i].Height;  // Negative because we go down
	}

	// For surface layer (0), add terrain noise variation
	if (LayerIndex == 0)
	{
		// Would use SampleTerrainHeight() from VoxelWorld, but keep this simple for now
		BoundaryZ = 50;  // Base surface height
	}

	return BoundaryZ;
}

// ---------------------------------------------------------------------------
//  Layer index lookup (which layer contains a voxel Z)
// ---------------------------------------------------------------------------

int32 FVoxelGenLayers::GetLayerIndexForZ(int32 WorldZ, const TArray<FLayerDefinition>& Layers)
{
	// Start from surface and work downward
	int32 CurrentZ = 0;

	for (int32 LayerIdx = 0; LayerIdx < Layers.Num(); ++LayerIdx)
	{
		int32 LayerTop = CurrentZ;
		int32 LayerBottom = CurrentZ - Layers[LayerIdx].Height;

		if (WorldZ <= LayerTop && WorldZ > LayerBottom)
		{
			return LayerIdx;
		}

		CurrentZ = LayerBottom;
	}

	return -1;  // Above all layers or in void
}

// ---------------------------------------------------------------------------
//  Object placement (boulders, veins, caverns)
// ---------------------------------------------------------------------------

void FVoxelGenLayers::PlaceLayerObjects(
	FIntVector ChunkCoord,
	int32 LayerIndex,
	TArray<EBlockType>& InOutBlocks,
	const TArray<FLayerDefinition>& Layers)
{
	if (!Layers.IsValidIndex(LayerIndex))
	{
		return;
	}

	const FLayerDefinition& Layer = Layers[LayerIndex];

	// Layer 0: place boulders on surface
	if (LayerIndex == 0)
	{
		// Boulder placement (similar to surface generation)
		// This would call GenerateSurfaceObjects equivalent, but layer-aware
		// TODO: implement when needed
	}

	// Layers with ore: place ore veins
	if (Layer.bHasOres)
	{
		// TODO: implement ore vein generation
		// Use FOreVeinDef to determine which ores and how much
	}

	// Layers with caverns: carve out cave systems
	if (Layer.bHasCaverns)
	{
		// TODO: implement cavern carving
		// Use simplex noise or cellular automata for natural-looking caves
	}
}
