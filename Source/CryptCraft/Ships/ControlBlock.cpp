// ControlBlock.cpp

#include "Ships/ControlBlock.h"
#include "Ships/ShipDetectionComponent.h"
#include "Voxel/VoxelWorld.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "EngineUtils.h"

AControlBlock::AControlBlock()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.TickInterval = 0.0f;

	// Create collision component as root
	CollisionComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionComponent"));
	CollisionComponent->SetBoxExtent(FVector(50.0f, 50.0f, 50.0f));  // Half-size: 100x100x100 (1 block)
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionComponent->SetCollisionObjectType(ECC_WorldStatic);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	RootComponent = CollisionComponent;

	// Create visual mesh
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(CollisionComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Load a simple cube mesh for visualization
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube"));
	if (CubeMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeMesh.Object);
	}

	// Load or create a distinct material for the control block
	static ConstructorHelpers::FObjectFinder<UMaterial> ControlBlockMat(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
	if (ControlBlockMat.Succeeded())
	{
		UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(
			ControlBlockMat.Object, MeshComponent);
		MatInstance->SetVectorParameterValue(FName(TEXT("Color")), FLinearColor(1.0f, 0.5f, 0.0f, 1.0f));  // Orange
		MeshComponent->SetMaterial(0, MatInstance);
	}

	// Create detection component
	DetectionComponent = CreateDefaultSubobject<UShipDetectionComponent>(TEXT("DetectionComponent"));
}

void AControlBlock::BeginPlay()
{
	Super::BeginPlay();

	// Cache reference to the VoxelWorld
	for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
	{
		VoxelWorld = *It;
		break;
	}

	if (!VoxelWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("[AControlBlock] No AVoxelWorld found in the level!"));
	}
}

void AControlBlock::Interact()
{
	if (!VoxelWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AControlBlock::Interact] VoxelWorld not available"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[AControlBlock::Interact] ========== SHIP DETECTION START =========="));
	UE_LOG(LogTemp, Log, TEXT("[AControlBlock::Interact] Control block at: %s"), *GetBlockCoordinate().ToString());

	// Run ship detection
	TMap<FIntVector, EBlockType> DetectedBlocks;
	int32 BlockCount = DetectShipBlocks(DetectedBlocks);

	if (BlockCount <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AControlBlock::Interact] ✗ FLOOD FILL FAILED - No connected blocks detected!"));
		UE_LOG(LogTemp, Log, TEXT("[AControlBlock::Interact] ========== SHIP DETECTION END =========="));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[AControlBlock::Interact] ✓ FLOOD FILL SUCCESSFUL - Detected %d connected blocks"), BlockCount);

	// Cache the results
	CachedDetectedBlocks = DetectedBlocks;

	// Debug visualization with colored boxes
	if (bDebugDrawDetectedBlocks)
	{
		UShipDetectionComponent::DebugDrawDetectedBlocks(
			GetWorld(),
			DetectedBlocks,
			DebugDrawDuration,
			FColor::Green);

		// Draw the control block in red
		DrawDebugBox(
			GetWorld(),
			GetActorLocation(),
			FVector(50.0f, 50.0f, 50.0f),
			FColor::Red,
			false,
			DebugDrawDuration,
			0,
			2.0f);

		UE_LOG(LogTemp, Log, TEXT("[AControlBlock::Interact] Debug boxes drawn for %f seconds"), DebugDrawDuration);
	}

	UE_LOG(LogTemp, Log, TEXT("[AControlBlock::Interact] ========== SHIP DETECTION END =========="));
}

FIntVector AControlBlock::GetBlockCoordinate() const
{
	const FVector WorldPos = GetActorLocation();
	return FIntVector(
		FMath::FloorToInt(WorldPos.X / BLOCK_SIZE),
		FMath::FloorToInt(WorldPos.Y / BLOCK_SIZE),
		FMath::FloorToInt(WorldPos.Z / BLOCK_SIZE));
}

int32 AControlBlock::DetectShipBlocks(TMap<FIntVector, EBlockType>& OutDetectedBlocks)
{
	if (!VoxelWorld || !DetectionComponent)
	{
		return 0;
	}

	FIntVector BlockCoord = GetBlockCoordinate();
	return DetectionComponent->DetectConnectedBlocks(
		VoxelWorld,
		BlockCoord,
		MaxShipBlockCount,
		OutDetectedBlocks);
}
