// ControlBlock.h
// A placeable block that serves as the control point for a ship.
// When the player interacts with it, it triggers flood-fill detection
// to identify all blocks that will form the ship.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Voxel/VoxelTypes.h"
#include "ControlBlock.generated.h"

class AVoxelWorld;
class UShipDetectionComponent;
class UBoxComponent;
class UStaticMeshComponent;
class UWidgetComponent;

/**
 * A placeable actor that marks the "brain" of a potential ship.
 * When the player interacts with it (e.g., right-click or E key),
 * it runs a flood-fill detection to find all connected solid blocks
 * that will form the ship.
 */
UCLASS()
class CRYPTCRAFT_API AControlBlock : public AActor
{
	GENERATED_BODY()

public:
	AControlBlock();

	// -----------------------------------------------------------------------
	//  Components
	// -----------------------------------------------------------------------

	/** Root collision for the control block. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Components")
	TObjectPtr<UBoxComponent> CollisionComponent;

	/** Visual mesh representing the control block. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Ship detection logic (handles flood fill). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Components")
	TObjectPtr<UShipDetectionComponent> DetectionComponent;

	// -----------------------------------------------------------------------
	//  Ship Detection Properties
	// -----------------------------------------------------------------------

	/** Maximum number of blocks that can be detected in a single ship. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Detection", meta = (ClampMin = "10", ClampMax = "10000"))
	int32 MaxShipBlockCount = 2000;

	/** Debug draw the detected ship blocks (shows colored boxes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Debug")
	bool bDebugDrawDetectedBlocks = true;

	/** Debug draw duration in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Debug", meta = (ClampMin = "0.1"))
	float DebugDrawDuration = 5.0f;

	// -----------------------------------------------------------------------
	//  Interaction
	// -----------------------------------------------------------------------

	/**
	 * Called when the player interacts with this control block.
	 * Triggers flood-fill detection and displays the build UI.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship|Interaction")
	void Interact();

	/**
	 * Get the block coordinate of this control block in world space.
	 */
	UFUNCTION(BlueprintPure, Category = "Ship")
	FIntVector GetBlockCoordinate() const;

	/**
	 * Get the VoxelWorld reference.
	 */
	UFUNCTION(BlueprintPure, Category = "Ship")
	AVoxelWorld* GetVoxelWorld() const { return VoxelWorld; }

	/**
	 * Run the ship detection without displaying UI.
	 * Useful for testing or programmatic detection.
	 * Returns the number of blocks detected.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship|Detection")
	int32 DetectShipBlocks(TMap<FIntVector, EBlockType>& OutDetectedBlocks);

protected:
	virtual void BeginPlay() override;

private:
	/** Reference to the owning VoxelWorld for block queries. */
	UPROPERTY()
	TObjectPtr<AVoxelWorld> VoxelWorld;

	/** Most recently detected ship blocks (cached from last detection). */
	UPROPERTY()
	TMap<FIntVector, EBlockType> CachedDetectedBlocks;
};
