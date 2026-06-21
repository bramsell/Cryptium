// WorldGenerationManager.cpp

#include "WorldGenerationManager.h"
#include "Voxel/Chunk.h"
#include "CryptCraft.h"

// ---------------------------------------------------------------------------
//  Registration
// ---------------------------------------------------------------------------

void FWorldGenerationManager::RegisterLevel(int32 LevelIndex, TSharedPtr<ILevelGenerator> Generator)
{
	check(LevelIndex >= 0);
	check(Generator.IsValid());

	// Grow the array with nulls if the index is beyond the current size
	while (Levels.Num() <= LevelIndex)
		Levels.Add(nullptr);

	Levels[LevelIndex] = MoveTemp(Generator);

	UE_LOG(LogCryptCraft, Log,
		TEXT("WorldGenerationManager: Registered '%s' at Level %d"),
		*Levels[LevelIndex]->GetLevelName(), LevelIndex);
}

// ---------------------------------------------------------------------------
//  Routing math
// ---------------------------------------------------------------------------

bool FWorldGenerationManager::ResolveChunkZ(
	int32 GlobalChunkZ,
	int32& OutLevelIndex,
	int32& OutLocalChunkZ) const
{
	// Surface: Level 0, LocalChunkZ == GlobalChunkZ (no transform)
	if (GlobalChunkZ >= 0)
	{
		OutLevelIndex  = 0;
		OutLocalChunkZ = GlobalChunkZ;
		return Levels.IsValidIndex(0) && Levels[0].IsValid();
	}

	// Underground: walk the level stack, consuming each level's declared depth.
	// TopZ is the shallowest (least-negative) ChunkZ belonging to the current level.
	// It starts at -1 (immediately below the surface) and steps down by each
	// level's GetDepthInChunks() as we move deeper.
	int32 TopZ = -1;
	for (int32 i = 1; i < Levels.Num(); ++i)
	{
		if (!Levels[i].IsValid())
		{
			// Gap in the registration — treat as zero-depth and skip
			continue;
		}

		const int32 Depth  = Levels[i]->GetDepthInChunks();
		const int32 BottomZ = TopZ - (Depth - 1); // Most-negative ChunkZ in this level

		if (GlobalChunkZ >= BottomZ && GlobalChunkZ <= TopZ)
		{
			OutLevelIndex  = i;
			OutLocalChunkZ = TopZ - GlobalChunkZ; // 0 = top chunk, Depth-1 = bottom chunk
			return true;
		}

		TopZ = BottomZ - 1; // Advance to the top of the next level
	}

	// Below all registered levels
	OutLevelIndex  = Levels.Num();
	OutLocalChunkZ = 0;
	return false;
}

int32 FWorldGenerationManager::GetLevelIndex(int32 GlobalChunkZ) const
{
	int32 LevelIndex, LocalChunkZ;
	ResolveChunkZ(GlobalChunkZ, LevelIndex, LocalChunkZ);
	return LevelIndex;
}

int32 FWorldGenerationManager::GetLocalChunkZ(int32 GlobalChunkZ) const
{
	int32 LevelIndex, LocalChunkZ;
	ResolveChunkZ(GlobalChunkZ, LevelIndex, LocalChunkZ);
	return LocalChunkZ;
}

// ---------------------------------------------------------------------------
//  Routing
// ---------------------------------------------------------------------------

void FWorldGenerationManager::RouteBlockGeneration(
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 GlobalChunkZ,
	TArray<EBlockType>& OutBlocks) const
{
	int32 LevelIndex, LocalChunkZ;
	const bool bFound = ResolveChunkZ(GlobalChunkZ, LevelIndex, LocalChunkZ);

	if (bFound)
	{
		Levels[LevelIndex]->GenerateBlocks(GlobalChunkX, GlobalChunkY, LocalChunkZ, OutBlocks);
	}
	else
	{
		// Below all registered levels — solid bedrock as a safe fallback
		OutBlocks.Init(EBlockType::Bedrock, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
	}
}

void FWorldGenerationManager::RouteChunkGeneration(
	AChunk& Chunk,
	int32 GlobalChunkX,
	int32 GlobalChunkY,
	int32 GlobalChunkZ) const
{
	int32 LevelIndex, LocalChunkZ;
	const bool bFound = ResolveChunkZ(GlobalChunkZ, LevelIndex, LocalChunkZ);

	if (bFound)
	{
		UE_LOG(LogCryptCraft, VeryVerbose,
			TEXT("WorldGenerationManager: ChunkZ=%d → Level %d ('%s'), LocalZ=%d"),
			GlobalChunkZ, LevelIndex, *Levels[LevelIndex]->GetLevelName(), LocalChunkZ);

		Levels[LevelIndex]->GenerateChunk(Chunk, GlobalChunkX, GlobalChunkY, LocalChunkZ);
	}
	else
	{
		// Below all registered levels — solid bedrock as a safe fallback
		UE_LOG(LogCryptCraft, VeryVerbose,
			TEXT("WorldGenerationManager: No generator for Level %d (ChunkZ=%d) — filling with Bedrock"),
			LevelIndex, GlobalChunkZ);

		TArray<EBlockType> BedrockBlocks;
		BedrockBlocks.Init(EBlockType::Bedrock, CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);
		Chunk.Initialize(BedrockBlocks);
	}
}