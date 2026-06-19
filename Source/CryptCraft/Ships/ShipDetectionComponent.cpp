// ShipDetectionComponent.cpp

#include "Ships/ShipDetectionComponent.h"
#include "Voxel/VoxelWorld.h"
#include "Containers/Queue.h"

UShipDetectionComponent::UShipDetectionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 UShipDetectionComponent::DetectConnectedBlocks(
	AVoxelWorld* VoxelWorld,
	const FIntVector& StartBlockCoord,
	int32 MaxBlockCount,
	TMap<FIntVector, EBlockType>& OutDetectedBlocks)
{
	OutDetectedBlocks.Reset();

	if (!VoxelWorld || MaxBlockCount <= 0)
	{
		return 0;
	}

	// BFS (Breadth-First Search) to collect all connected solid blocks
	TQueue<FIntVector> Queue;
	TSet<FIntVector> Visited;

	// Start from neighbors of the control block (since the control block itself is not a voxel block)
	const FIntVector Neighbors[6] =
	{
		StartBlockCoord + FIntVector(1, 0, 0),   // +X
		StartBlockCoord + FIntVector(-1, 0, 0),  // -X
		StartBlockCoord + FIntVector(0, 1, 0),   // +Y
		StartBlockCoord + FIntVector(0, -1, 0),  // -Y
		StartBlockCoord + FIntVector(0, 0, 1),   // +Z
		StartBlockCoord + FIntVector(0, 0, -1)   // -Z
	};

	for (const FIntVector& Neighbor : Neighbors)
	{
		Queue.Enqueue(Neighbor);
		Visited.Add(Neighbor);
	}

	int32 DetectedCount = 0;

	while (!Queue.IsEmpty() && DetectedCount < MaxBlockCount)
	{
		FIntVector CurrentCoord;
		Queue.Dequeue(CurrentCoord);

		// Get the block at this position
		EBlockType BlockType = VoxelWorld->GetBlockAt(CurrentCoord);

		// Skip boundary blocks (air, water)
		if (IsBoundaryBlock(BlockType))
		{
			continue;
		}

		// Skip non-solid blocks
		if (!IsSolidBlockForShip(BlockType))
		{
			continue;
		}

		// This block is part of the ship
		OutDetectedBlocks.Add(CurrentCoord, BlockType);
		DetectedCount++;

		// Enqueue the 6 neighbors (±X, ±Y, ±Z)
		const FIntVector NeighborCoords[6] =
		{
			CurrentCoord + FIntVector(1, 0, 0),   // +X
			CurrentCoord + FIntVector(-1, 0, 0),  // -X
			CurrentCoord + FIntVector(0, 1, 0),   // +Y
			CurrentCoord + FIntVector(0, -1, 0),  // -Y
			CurrentCoord + FIntVector(0, 0, 1),   // +Z
			CurrentCoord + FIntVector(0, 0, -1)   // -Z
		};

		for (const FIntVector& Neighbor : NeighborCoords)
		{
			if (!Visited.Contains(Neighbor))
			{
				Visited.Add(Neighbor);
				Queue.Enqueue(Neighbor);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[UShipDetectionComponent::DetectConnectedBlocks] Detected %d connected blocks from %s"),
		DetectedCount, *StartBlockCoord.ToString());

	return DetectedCount;
}

void UShipDetectionComponent::DebugDrawDetectedBlocks(
	UWorld* World,
	const TMap<FIntVector, EBlockType>& DetectedBlocks,
	float Duration,
	FColor Color)
{
	if (!World || DetectedBlocks.Num() == 0)
	{
		return;
	}

	for (const auto& Pair : DetectedBlocks)
	{
		const FIntVector& BlockCoord = Pair.Key;
		const FVector WorldCenter(
			(BlockCoord.X + 0.5f) * BLOCK_SIZE,
			(BlockCoord.Y + 0.5f) * BLOCK_SIZE,
			(BlockCoord.Z + 0.5f) * BLOCK_SIZE);

		// Draw a box around each detected block
		DrawDebugBox(
			World,
			WorldCenter,
			FVector(50.0f, 50.0f, 50.0f),  // Half-extent = 50 for 100-unit blocks
			Color,
			false,
			Duration,
			0,
			1.0f);
	}
}

bool UShipDetectionComponent::IsSolidBlockForShip(EBlockType BlockType)
{
	// Include any non-air, non-water, non-lava block
	switch (BlockType)
	{
		case EBlockType::Air:
		case EBlockType::Water:
		case EBlockType::Lava:
			return false;
		default:
			return true;
	}
}

bool UShipDetectionComponent::IsBoundaryBlock(EBlockType BlockType)
{
	// Boundary blocks stop the flood fill
	switch (BlockType)
	{
		case EBlockType::Air:
		case EBlockType::Water:
		case EBlockType::Lava:
			return true;
		default:
			return false;
	}
}
