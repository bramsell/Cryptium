// Copyright Epic Games, Inc. All Rights Reserved.

#include "CryptCraftGameMode.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SkyLight.h"

ACryptCraftGameMode::ACryptCraftGameMode()
{
	// Default to the base AVoxelWorld class; set in the constructor body
	// rather than as an inline default to avoid a VTable helper crash during
	// UHT class registration.
	VoxelWorldClass = AVoxelWorld::StaticClass();
}

void ACryptCraftGameMode::BeginPlay()
{
	Super::BeginPlay();

	AVoxelWorld* World = EnsureVoxelWorld();
	EnsureDirectionalLight();
	EnsureSkyLight();

	// Calculate and cache where we want the player to stand.
	if (World)
	{
		PendingSpawnLocation = World->GetPlayerSpawnLocation();
		UE_LOG(LogTemp, Log, TEXT("CryptCraftGameMode: Player surface position = %s"), *PendingSpawnLocation.ToString());
	}

	// Defer the teleport by one tick so the pawn spawned by PostLogin →
	// RestartPlayer has fully initialised before we move it.
	// Use CreateUObject so the timer manager holds a GC-safe weak reference.
	FTimerDelegate Delegate;
	Delegate.BindUObject(this, &ACryptCraftGameMode::TeleportPlayersToSurface);
	GetWorldTimerManager().SetTimerForNextTick(Delegate);
}

// ---------------------------------------------------------------------------

AVoxelWorld* ACryptCraftGameMode::EnsureVoxelWorld()
{
	// Already present in the level – apply our WorldGenType and return it.
	// NOTE: GameMode::BeginPlay fires before level actors' BeginPlay in UE5, so
	// the VoxelWorld's generation hasn't run yet when we reach this point.
	for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
	{
		(*It)->WorldGenType = WorldGenType;
		return *It;
	}

	if (!VoxelWorldClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: VoxelWorldClass is null, cannot auto-spawn AVoxelWorld."));
		return nullptr;
	}

	// Use SpawnActorDeferred so we can configure WorldGenType BEFORE BeginPlay
	// fires on the VoxelWorld (BeginPlay is where terrain generation happens).
	AVoxelWorld* SpawnedWorld = GetWorld()->SpawnActorDeferred<AVoxelWorld>(
		VoxelWorldClass,
		FTransform::Identity
	);

	if (!SpawnedWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("CryptCraftGameMode: Failed to spawn AVoxelWorld."));
		return nullptr;
	}

	// Apply generation mode before BeginPlay is called
	SpawnedWorld->WorldGenType = WorldGenType;

	// Finish spawning – this calls BeginPlay with the correct WorldGenType set
	SpawnedWorld->FinishSpawning(FTransform::Identity);

	UE_LOG(LogTemp, Log, TEXT("CryptCraftGameMode: Auto-spawned AVoxelWorld (mode: %s)."),
		*UEnum::GetValueAsString(WorldGenType));

	return SpawnedWorld;
}

// ---------------------------------------------------------------------------

void ACryptCraftGameMode::TeleportPlayersToSurface()
{
	if (PendingSpawnLocation.IsZero()) return;

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC) continue;

		APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			// No pawn yet (e.g. abstract character with no BP child set).
			// RestartPlayer will at least log a sensible error.
			RestartPlayer(PC);
			Pawn = PC->GetPawn();
		}

		if (Pawn)
		{
			Pawn->TeleportTo(PendingSpawnLocation, FRotator::ZeroRotator,
			                 /*bIsATest=*/false, /*bNoCheck=*/false);
			UE_LOG(LogTemp, Log, TEXT("CryptCraftGameMode: Teleported '%s' to %s."),
				*Pawn->GetName(), *PendingSpawnLocation.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("CryptCraftGameMode: Could not find/spawn a pawn for '%s'. "
				     "Make sure the GameMode's Default Pawn Class is set to a non-abstract Blueprint."),
				*PC->GetName());
		}
	}
}

// ---------------------------------------------------------------------------

void ACryptCraftGameMode::EnsureSkyLight()
{
	// Re-purpose this as a fill/ambient light using a second directional light.
	// A SkyLight would require a sky capture (which comes out black with no skybox).
	// A shadowless directional light reliably reaches every surface including
	// underground cave walls without needing any scene capture.

	// Skip if a fill light was already spawned.
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		// Only skip if we find more than one – the first is the sun.
		ADirectionalLight* L = *It;
		if (L && L->GetName().Contains(TEXT("AutoFill")))
			return;
	}

	FActorSpawnParameters Params;
	Params.Name = TEXT("AutoFillLight");

	// Come from the opposite hemisphere to the sun so every face gets
	// at least some contribution. Pitch > 0 means pointing upward-ish,
	// which lights ceilings and the underside of terrain.
	const FRotator FillRotation(-160.f, 225.f, 0.f);

	ADirectionalLight* Fill = GetWorld()->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(),
		FVector::ZeroVector,
		FillRotation,
		Params
	);

	if (!Fill)
	{
		UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: Failed to spawn fill light."));
		return;
	}

	if (UDirectionalLightComponent* LC = Cast<UDirectionalLightComponent>(Fill->GetLightComponent()))
	{
		// Low intensity – just enough to see in a cave, not enough to wash
		// out outdoor surfaces.
		LC->SetIntensity(0.15f);
		LC->SetLightColor(FLinearColor(0.55f, 0.65f, 0.9f));  // cool blue-grey tint

		// NO shadows – this must penetrate underground and into caves.
		LC->SetCastShadows(false);

		// Don't let this light interact with the atmosphere.
		LC->SetAtmosphereSunLight(false);

		// Explicitly yield to the sun so there is no ambiguity about which
		// directional light drives forward shading / translucency / fog.
		LC->ForwardShadingPriority = 0;
		LC->MarkRenderStateDirty();
	}

	UE_LOG(LogTemp, Log, TEXT("CryptCraftGameMode: Auto-spawned fill light."));

	// =========================================================================
	// SKYATMOSPHERE: Required for SkyLight real-time capture to work
	// =========================================================================

	// TODO: SkyAtmosphere spawn temporarily disabled until correct header is identified
	// For now, rely on environment's existing sky or create via Blueprint
	//
	// ASkyAtmosphere should be present in the level for SkyLight real-time capture
	// to work properly. If you see "SkyLight with real-time capture requires SkyAtmosphere"
	// warning, add a SkyAtmosphere actor to the level or implement spawn here.

	// =========================================================================
	// SKYLIGHT: Add a SkyLight for better ambient cave illumination
	// =========================================================================

	UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: === Starting SkyLight spawn process ==="));

	// Check if SkyLight already exists
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		if (*It && (*It)->GetName().Contains(TEXT("AutoSky")))
		{
			UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: SkyLight already exists, skipping spawn."));
			return;
		}
	}

	FActorSpawnParameters SkyParams;
	SkyParams.Name = TEXT("AutoSkyLight");

	ASkyLight* SkyLight = GetWorld()->SpawnActor<ASkyLight>(
		ASkyLight::StaticClass(),
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SkyParams
	);

	if (!SkyLight)
	{
		UE_LOG(LogTemp, Error, TEXT("CryptCraftGameMode: Failed to spawn ASkyLight actor!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: ASkyLight spawned: %s"), *SkyLight->GetName());

	USkyLightComponent* SkyLC = Cast<USkyLightComponent>(SkyLight->GetLightComponent());
	if (!SkyLC)
	{
		UE_LOG(LogTemp, Error, TEXT("CryptCraftGameMode: SkyLight actor has no USkyLightComponent!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: SkyLightComponent found"));

	// Moderate intensity for ambient cave lighting
	SkyLC->SetIntensity(0.3f);
	UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: Set SkyLight intensity to 0.3"));

	// Cool blue color for cave atmosphere
	SkyLC->SetLightColor(FLinearColor(0.50f, 0.60f, 0.85f));
	UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: Set SkyLight color to blue-grey"));

	// Use real-time scene capture so it adapts to the environment
	SkyLC->bRealTimeCapture = true;
	UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: Enabled real-time capture"));

	// Mark for update and rendering
	SkyLC->MarkRenderStateDirty();
	SkyLight->UpdateComponentTransforms();
	UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: Marked render state dirty and updated transforms"));

	UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: Auto-spawned SkyLight for cave ambient lighting."));
}

void ACryptCraftGameMode::EnsureDirectionalLight()
{
	// Already present in the level – leave it alone.
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.Name = TEXT("AutoSun");

	ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(),
		FVector::ZeroVector,
		SunRotation,
		Params
	);

	if (!Sun)
	{
		UE_LOG(LogTemp, Error, TEXT("CryptCraftGameMode: Failed to spawn DirectionalLight."));
		return;
	}

	// Configure intensity and enable atmosphere / sky atmosphere casting
	if (UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		LightComp->SetIntensity(SunIntensity);
		LightComp->SetAtmosphereSunLight(true);
		LightComp->SetCastShadows(true);

		// Give the sun the highest priority so it is unambiguously chosen
		// as the primary directional light for forward shading / fog / translucency.
		LightComp->ForwardShadingPriority = 1;
		LightComp->MarkRenderStateDirty();
	}

	UE_LOG(LogTemp, Log, TEXT("CryptCraftGameMode: Auto-spawned DirectionalLight."));
}
