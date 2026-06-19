// Ship.h
// Represents a single ship with its own block grid and greedy-meshed geometry.
// Ships are created when a control block detects connected blocks.
// Ships are Pawns so they can be possessed by the player for piloting.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Voxel/VoxelTypes.h"
#include "Ship.generated.h"

class UProceduralMeshComponent;
class AVoxelWorld;

/**
 * A ship is a bounded collection of blocks that can be piloted.
 * - Blocks are stored in local coordinates relative to control block origin (0,0,0)
 * - Uses greedy meshing + ProceduralMeshComponent for efficient rendering
 * - Handles atomic transfer from world grid to ship grid
 * - Inherits from Pawn so it can be possessed by a player controller for piloting
 */
UCLASS()
class CRYPTCRAFT_API AShip : public APawn
{
	GENERATED_BODY()

public:
	AShip();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/**
	 * Initialize ship from detected blocks at a world position.
	 * Handles the atomic swap: disable world collision → build ship mesh → remove world blocks.
	 *
	 * @param DetectedBlocks Map of world-space block coordinates and types
	 * @param ControlBlockWorldPos The world position of the control block (becomes ship origin)
	 * @param InVoxelWorld Reference to the world grid for removal
	 * @return True if successful, false if transfer failed
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship")
	bool InitializeFromDetectedBlocks(
		const TMap<FIntVector, EBlockType>& DetectedBlocks,
		FIntVector ControlBlockWorldPos,
		AVoxelWorld* InVoxelWorld);

	/**
	 * Get the block type at a local ship coordinate.
	 * Returns Air if coordinate is out of bounds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship")
	EBlockType GetBlockAt(FIntVector LocalBlockCoord) const;

	/**
	 * Set a block in the ship grid and update ISM.
	 * (Used for future damage/modification systems)
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship")
	void SetBlockAt(FIntVector LocalBlockCoord, EBlockType BlockType);

	/**
	 * Add a block to the ship grid at a local coordinate.
	 * Rebuilds mesh automatically.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship")
	void AddBlockToShip(FIntVector LocalBlockCoord, EBlockType BlockType)
	{
		if (BlockType != EBlockType::Air)
		{
			SetBlockAt(LocalBlockCoord, BlockType);
		}
	}

	/**
	 * Remove a block from the ship grid at a local coordinate.
	 * Rebuilds mesh automatically.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship")
	void RemoveBlockFromShip(FIntVector LocalBlockCoord)
	{
		SetBlockAt(LocalBlockCoord, EBlockType::Air);
	}

	/**
	 * Get total number of blocks in this ship.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship")
	int32 GetBlockCount() const { return ShipBlocks.Num(); }

	/**
	 * Get the world position of the control block origin.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship")
	FIntVector GetControlBlockWorldPos() const { return ControlBlockWorldPosition; }

	/**
	 * Setup input binding for ship piloting (called when possessed).
	 */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:
	// -----------------------------------------------------------------------
	//  Piloting / Input
	// -----------------------------------------------------------------------

	/** Current yaw rotation rate (degrees per second) from input. */
	float CurrentYawRate = 0.0f;

	/** Max rotation speed (degrees per second). */
	UPROPERTY(EditAnywhere, Category = "Ship|Piloting")
	float MaxYawRate = 180.0f;

	/** Current forward movement input (-1 to +1). */
	float CurrentForwardInput = 0.0f;

	/** Max forward/backward speed (units per second). */
	UPROPERTY(EditAnywhere, Category = "Ship|Piloting")
	float MaxForwardSpeed = 500.0f;

	/** Current vertical movement input (-1 to +1). */
	float CurrentVerticalInput = 0.0f;

	/** Max up/down speed (units per second). */
	UPROPERTY(EditAnywhere, Category = "Ship|Piloting")
	float MaxVerticalSpeed = 300.0f;

	/** Input callback: turn left/right (from A/D or gamepad). */
	void OnTurn(float Value);

	/** Input callback: move forward/backward (from W/S). */
	void OnMoveForward(float Value);

	/** Input callback: move up/down (from Space/Shift). */
	void OnMoveVertical(float Value);

	/** Apply current rotation to ship each tick. */
	void ApplyShipRotation(float DeltaTime);

	/** Apply current movement to ship each tick. */
	void ApplyShipMovement(float DeltaTime);

	/** Check if ship can move to a new world position/rotation without colliding with world blocks. */
	bool CanMoveToPosition(FVector NewWorldPos, FQuat NewRotation) const;

	// -----------------------------------------------------------------------
	//  Storage
	// -----------------------------------------------------------------------

	/** All blocks in this ship, keyed by local coordinate (relative to control block). */
	TMap<FIntVector, EBlockType> ShipBlocks;

	/** Cached reference to VoxelWorld for removal operations. */
	UPROPERTY()
	TObjectPtr<AVoxelWorld> VoxelWorld;

	/** World position where the control block was placed (ship origin in world space). */
	FIntVector ControlBlockWorldPosition;

	// -----------------------------------------------------------------------
	//  Rendering
	// -----------------------------------------------------------------------

	/** ProceduralMeshComponent for greedy-meshed ship geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Rendering", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProceduralMeshComponent> ShipMeshComponent;

	// -----------------------------------------------------------------------
	//  Atomic Transfer Sequence
	// -----------------------------------------------------------------------

	/** Blocks whose collision was disabled (waiting for physics tick before removal). */
	TSet<FIntVector> PendingRemovalBlocks;

	/** Handle for delayed world block removal. */
	FTimerHandle RemovalTimerHandle;

	/**
	 * Rebuild the ship mesh using greedy meshing.
	 * Generates a ProceduralMesh from the ship's block grid, merging adjacent
	 * same-type blocks into larger rectangles for performance.
	 */
	void RebuildMesh();

	/**
	 * Internal: Greedy mesh generation for sparse TMap block storage.
	 * Outputs vertices, triangles, normals, UVs, and colors for ProceduralMesh.
	 * Handles sparse layout where missing blocks are treated as air.
	 */
	void BuildGreedyMesh(
		TArray<FVector>&       OutVertices,
		TArray<int32>&         OutTriangles,
		TArray<FVector>&       OutNormals,
		TArray<FVector2D>&     OutUVs,
		TArray<FLinearColor>&  OutColors);

	/**
	 * Step 1: Build mesh from detected blocks.
	 * Returns true if successful.
	 */
	bool BuildShipMesh();

	/**
	 * Step 2: Disable collision on world blocks (they stay in world grid temporarily).
	 */
	void DisableWorldBlockCollision(const TSet<FIntVector>& BlocksToTransfer);

	/**
	 * Step 3: After one physics tick, remove blocks from world grid.
	 * Called via timer to ensure physics has updated.
	 */
	void RemoveBlocksFromWorld();

	/**
	 * Depossess the ship and return control to the character.
	 */
	void Depossess();

	/**
	 * Helper: Convert world block coordinates to ship-local coordinates.
	 */
	FIntVector WorldToShipCoord(FIntVector WorldCoord) const
	{
		return WorldCoord - ControlBlockWorldPosition;
	}

	/**
	 * Helper: Convert ship-local coordinates to world block coordinates.
	 */
	FIntVector ShipToWorldCoord(FIntVector ShipCoord) const
	{
		return ShipCoord + ControlBlockWorldPosition;
	}
};
