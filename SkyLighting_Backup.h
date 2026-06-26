// ============================================================================
// BACKUP: Sky Lighting Functions for CryptCraft
// ============================================================================
// 
// This file contains the lighting system code extracted from:
//   - CryptCraftGameMode.h/cpp
//   - VoxelWorld.h/cpp
//
// Saved: 2026-06-20
// Reason: Switching to brother's codebase; will re-integrate these after
//
// ============================================================================

// ============================================================================
// SECTION 1: Declarations (goes in CryptCraftGameMode.h)
// ============================================================================

/*
In ACryptCraftGameMode class:

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (ClampMin = "0.0"))
float SunIntensity = 10.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
FRotator SunRotation = FRotator(-45.f, 45.f, 0.f);

// Private methods:
void EnsureDirectionalLight();
void EnsureSkyAtmosphere();
*/

// ============================================================================
// SECTION 2: BeginPlay() modification (in CryptCraftGameMode.cpp)
// ============================================================================

/*
void ACryptCraftGameMode::BeginPlay()
{
	Super::BeginPlay();

	AVoxelWorld* World = EnsureVoxelWorld();
	EnsureDirectionalLight();
	EnsureSkyAtmosphere();  // Spawn SkyAtmosphere for blue sky

	// ... rest of BeginPlay code
}
*/

// ============================================================================
// SECTION 3: Include headers (add to CryptCraftGameMode.cpp)
// ============================================================================

/*
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
*/

// ============================================================================
// SECTION 4: EnsureDirectionalLight() implementation
// ============================================================================

/*
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
*/

// ============================================================================
// SECTION 5: EnsureSkyAtmosphere() implementation
// ============================================================================

/*
void ACryptCraftGameMode::EnsureSkyAtmosphere()
{
	// Spawn SkyAtmosphere for blue sky appearance
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (*It && (*It)->GetClass()->GetName() == TEXT("SkyAtmosphere"))
		{
			UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: SkyAtmosphere already exists, skipping spawn."));
			return;
		}
	}

	UClass* SkyAtmoClass = LoadClass<AActor>(nullptr, TEXT("/Script/Engine.SkyAtmosphere"));
	if (SkyAtmoClass)
	{
		AActor* SkyAtmo = GetWorld()->SpawnActor(SkyAtmoClass);
		if (SkyAtmo)
		{
			UE_LOG(LogTemp, Log, TEXT("CryptCraftGameMode: Spawned SkyAtmosphere."));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: Failed to spawn SkyAtmosphere actor."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("CryptCraftGameMode: Could not load SkyAtmosphere class."));
	}
}
*/

// ============================================================================
// SECTION 6: VoxelWorld.cpp - GetSunDirection() and declarations
// ============================================================================

/*
In VoxelWorld.h, add these properties and methods:

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
float MinimumBrightness = 0.30f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Lighting")
float BrightnessDebugMultiplier = 1.0f;

UFUNCTION(BlueprintPure, Category = "Voxel")
FVector GetSunDirection() const;

In VoxelWorld.cpp, add:

FVector AVoxelWorld::GetSunDirection() const
{
	// Get the directional light (sun) for per-face brightness computation
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		ADirectionalLight* Light = *It;
		if (Light && Light->IsValidLowLevel())
		{
			FVector Dir = Light->GetActorForwardVector();
			static bool bLoggedOnce = false;
			if (!bLoggedOnce)
			{
				UE_LOG(LogTemp, Warning, TEXT("GetSunDirection: Using DirectionalLight ForwardVector=(%.3f,%.3f,%.3f)"), Dir.X, Dir.Y, Dir.Z);
				bLoggedOnce = true;
			}
			return Dir;
		}
	}
	
	// No directional light found – return default downward
	return FVector(0.f, 0.f, -1.f);
}
*/

// ============================================================================
// SECTION 7: Chunk.cpp - Brightness computation in BuildGreedyMesh()
// ============================================================================

/*
In Chunk.cpp, the per-face brightness computation should look like this
(typically around the vertex color encoding section):

// Brightness Computation for per-face lighting
float FaceBrightness = 0.5f;
if (VoxelWorld)
{
	const FVector SunDir = VoxelWorld->GetSunDirection();
	const float DotProduct = FVector::DotProduct(Normal, SunDir);
	FaceBrightness = FMath::Max(0.f, DotProduct);
	FaceBrightness = FMath::Max(FaceBrightness, VoxelWorld->MinimumBrightness);  // Clamp to 0.30
	FaceBrightness *= VoxelWorld->BrightnessDebugMultiplier;
	FaceBrightness = FMath::Min(FaceBrightness, 1.0f);
}

// CRITICAL: Encode brightness in vertex color ALPHA channel (not hardcoded to 1.0)
const FLinearColor TileColor(AU, AV, TileU, FaceBrightness);
OutColors.Add(TileColor);  // x4 for quad vertices

// Logging (every 1000 faces):
static int32 FaceCounter = 0;
if (++FaceCounter % 1000 == 0)
{
	UE_LOG(LogTemp, Warning, 
		TEXT("Chunk::BuildGreedyMesh: FaceCounter=%d, DotProd=%.3f, MinBright=%.3f, DebugMult=%.3f, FinalBright=%.3f"),
		FaceCounter, DotProduct, VoxelWorld->MinimumBrightness, VoxelWorld->BrightnessDebugMultiplier, FaceBrightness);
}
*/

// ============================================================================
// SECTION 8: Material Graph Change (M_VoxelChunk)
// ============================================================================

/*
In the Material Editor for M_VoxelChunk:

1. Ensure vertex color is being used to control UV coordinates (existing)
2. ADD NEW NODE: Vertex Color → Mask(A) node
   - This extracts the Alpha channel (our brightness value)
3. ADD NEW NODE: Multiply node
   - Connect "Vertex Color Mask(A)" to the second input
   - Connect the "Base Color" (from atlas texture) to the first input
4. Connect Multiply output → Material Output Base Color

This ensures the vertex-encoded brightness darkens all faces appropriately.
*/

// ============================================================================
// NOTES FOR RE-INTEGRATION
// ============================================================================

/*
When adding these functions back to the brother's codebase:

1. Watch for naming conflicts (brother's version may have similar functions)
2. Check if brother's code already has:
   - A DirectionalLight spawn mechanism
   - A GetSunDirection() or sun direction query
   - Brightness computation in mesh building
3. If brother's code has these, merge carefully rather than just copying
4. Test with cache clear (delete Binaries, Intermediate, Saved folders) before testing
5. If lights don't spawn, add diagnostic logging to BeginPlay() and EnsureDirectionalLight()

Key Success Indicator:
- Blocks in dark caves should show 30% brightness minimum (never completely black)
- Faces pointing toward sun should be bright (~90-100%)
- Faces pointing away should be dim but still visible (~30-40%)
*/
