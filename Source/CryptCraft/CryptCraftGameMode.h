// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Voxel/VoxelWorld.h"
#include "Engine/DirectionalLight.h"
#include "CryptCraftGameMode.generated.h"

/**
 *  Simple GameMode for a first person game
 */
UCLASS(abstract)
class ACryptCraftGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACryptCraftGameMode();

	/**
	 * The AVoxelWorld subclass to spawn automatically.
	 * Override this in a child Blueprint GameMode to swap in a custom class.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	TSubclassOf<AVoxelWorld> VoxelWorldClass;

	/**
	 * Generation mode passed to the auto-spawned VoxelWorld.
	 * Change to Flat for a testing platform, Terrain for hilly procedural world.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	EWorldGenType WorldGenType = EWorldGenType::Terrain;

	/**
	 * Intensity (lux) of the auto-spawned directional light.
	 * Has no effect if a DirectionalLight already exists in the level.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (ClampMin = "0.0"))
	float SunIntensity = 10.0f;

	/**
	 * Rotation of the auto-spawned sun (Pitch drives the elevation angle).
	 * Default: 45° down from the horizon, facing slightly south-east.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
	FRotator SunRotation = FRotator(-45.f, 45.f, 0.f);

protected:
	/** Spawns an AVoxelWorld and/or a DirectionalLight if not already in the level. */
	virtual void BeginPlay() override;

private:
	AVoxelWorld* EnsureVoxelWorld();
	void EnsureDirectionalLight();
	void EnsureSkyAtmosphere();
	void EnsureSkyLight();

	/**
	 * Teleports all player pawns to the voxel surface on the next tick.
	 * Deferred so the pawn (spawned by PostLogin before BeginPlay) has
	 * finished initialising before we move it.
	 */
	UFUNCTION()
	void TeleportPlayersToSurface();

	/** Cached spawn location set during BeginPlay for use in the deferred call. */
	FVector PendingSpawnLocation = FVector::ZeroVector;
};



