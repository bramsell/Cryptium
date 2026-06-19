// ShipDetectionComponent.h
// Handles the flood-fill algorithm to detect all blocks connected to a control block.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Voxel/VoxelTypes.h"
#include "ShipDetectionComponent.generated.h"

class AVoxelWorld;

/**
 * Detects connected solid blocks from a given starting position using BFS.
 * Stops at air/water boundaries and enforces a maximum block count.
 */
UCLASS(ClassGroup = (CryptCraft), meta = (BlueprintSpawnableComponent))
class CRYPTCRAFT_API UShipDetectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShipDetectionComponent();

	/**
	 * Run a flood-fill (BFS) from the given block coordinate.
	 * Collects all solid blocks reachable from the start without crossing air/water.
	 *
	 * @param VoxelWorld         The world grid to query blocks from
	 * @param StartBlockCoord    The block coordinate to start the BFS from
	 * @param MaxBlockCount      Maximum blocks to detect before stopping
	 * @param OutDetectedBlocks  Result map of detected block positions and types
	 * @return                   Number of blocks detected
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship|Detection")
	static int32 DetectConnectedBlocks(
		AVoxelWorld* VoxelWorld,
		const FIntVector& StartBlockCoord,
		int32 MaxBlockCount,
		TMap<FIntVector, EBlockType>& OutDetectedBlocks);

	/**
	 * Debug draw the detected ship blocks with colored boxes.
	 *
	 * @param World          The world to draw in
	 * @param DetectedBlocks The blocks to visualize
	 * @param Duration       How long to display the boxes (seconds)
	 * @param Color          Color for the debug boxes
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship|Debug")
	static void DebugDrawDetectedBlocks(
		UWorld* World,
		const TMap<FIntVector, EBlockType>& DetectedBlocks,
		float Duration = 5.0f,
		FColor Color = FColor::Green);

private:
	/**
	 * Check if a block type is solid and should be included in ship detection.
	 */
	UFUNCTION()
	static bool IsSolidBlockForShip(EBlockType BlockType);

	/**
	 * Check if a block type acts as a boundary (stops ship detection).
	 */
	UFUNCTION()
	static bool IsBoundaryBlock(EBlockType BlockType);
};
