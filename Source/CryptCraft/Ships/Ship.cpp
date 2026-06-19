// Ship.cpp

#include "Ships/Ship.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "GameFramework/Character.h"
#include "CryptWorldCharacter.h"

AShip::AShip()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f; // Tick every frame

	// Create ProceduralMeshComponent for greedy-meshed geometry
	ShipMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ShipMeshComponent"));
	ShipMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);  // Will be enabled after build
	RootComponent = ShipMeshComponent;

	ControlBlockWorldPosition = FIntVector::ZeroValue;

	// Pawn settings
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CurrentYawRate = 0.0f;
}

void AShip::BeginPlay()
{
	Super::BeginPlay();
	// Nothing needed here yet
}

void AShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GetController())
	{
		return;
	}

	// Handle raw keyboard input directly
	if (GetWorld() && GetWorld()->GetFirstPlayerController())
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		
		// W/S for forward/backward
		if (PC->IsInputKeyDown(EKeys::W))
		{
			CurrentForwardInput = 1.0f;
		}
		else if (PC->IsInputKeyDown(EKeys::S))
		{
			CurrentForwardInput = -1.0f;
		}
		else
		{
			CurrentForwardInput = 0.0f;
		}
		
		// A/D for yaw rotation
		if (PC->IsInputKeyDown(EKeys::A))
		{
			CurrentYawRate = -MaxYawRate;
		}
		else if (PC->IsInputKeyDown(EKeys::D))
		{
			CurrentYawRate = MaxYawRate;
		}
		else
		{
			CurrentYawRate = 0.0f;
		}
		
		// Space/Shift for vertical movement
		if (PC->IsInputKeyDown(EKeys::SpaceBar))
		{
			CurrentVerticalInput = 1.0f;
		}
		else if (PC->IsInputKeyDown(EKeys::LeftShift))
		{
			CurrentVerticalInput = -1.0f;
		}
		else
		{
			CurrentVerticalInput = 0.0f;
		}
	}

	// Apply rotation and movement if piloted
	ApplyShipRotation(DeltaTime);
	ApplyShipMovement(DeltaTime);
}

void AShip::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!PlayerInputComponent)
	{
		return;
	}

	// Bind turn input (A/D or gamepad left stick)
	PlayerInputComponent->BindAxis("Turn", this, &AShip::OnTurn);

	// Bind forward/backward movement (W/S)
	PlayerInputComponent->BindAxis("MoveForward", this, &AShip::OnMoveForward);

	// Bind right-click to depossess the ship
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AShip::Depossess);
}

void AShip::OnTurn(float Value)
{
	// Value ranges from -1 (full left) to +1 (full right)
	CurrentYawRate = Value * MaxYawRate;
}

void AShip::OnMoveForward(float Value)
{
	// Value ranges from -1 (full backward) to +1 (full forward)
	CurrentForwardInput = Value;
}

void AShip::OnMoveVertical(float Value)
{
	// TODO: Implement vertical movement (Space/Shift)
	CurrentVerticalInput = Value;
}

void AShip::ApplyShipRotation(float DeltaTime)
{
	if (FMath::IsNearlyZero(CurrentYawRate))
	{
		return;
	}

	// Get current rotation as quaternion
	FQuat CurrentQuat = GetActorQuat();

	// Create a rotation around Z axis (yaw)
	FQuat YawRotation = FQuat(FVector::UpVector, FMath::DegreesToRadians(CurrentYawRate * DeltaTime));

	// Apply the yaw rotation
	FQuat NewQuat = YawRotation * CurrentQuat;

	// Check collision with the new rotation before applying it
	FVector CurrentLocation = GetActorLocation();
	if (!CanMoveToPosition(CurrentLocation, NewQuat))
	{
		// Collision detected - don't rotate
		return;
	}

	// Set new rotation
	SetActorRotation(NewQuat);

	// Update controller's view rotation to match ship rotation
	if (GetController())
	{
		FRotator NewRotator = NewQuat.Rotator();
		GetController()->SetControlRotation(NewRotator);
	}
}

void AShip::ApplyShipMovement(float DeltaTime)
{
	if (FMath::IsNearlyZero(CurrentForwardInput) && FMath::IsNearlyZero(CurrentVerticalInput))
	{
		return;
	}

	// Get current rotation as quaternion
	FQuat CurrentQuat = GetActorQuat();

	// Calculate movement in ship's local space
	FVector LocalMovement = FVector::ZeroVector;

	if (!FMath::IsNearlyZero(CurrentForwardInput))
	{
		// Forward/backward in ship's local X direction (forward)
		LocalMovement.X = CurrentForwardInput * MaxForwardSpeed * DeltaTime;
	}

	if (!FMath::IsNearlyZero(CurrentVerticalInput))
	{
		// Up/down in world Z direction (not rotated by ship orientation)
		LocalMovement.Z = CurrentVerticalInput * MaxVerticalSpeed * DeltaTime;
	}

	// Rotate the local movement vector into world space using the ship's rotation
	FVector WorldMovement = CurrentQuat.RotateVector(LocalMovement);

	// Calculate proposed new position
	FVector CurrentLocation = GetActorLocation();
	FVector ProposedNewLocation = CurrentLocation + WorldMovement;

	// Check collision before moving
	if (!CanMoveToPosition(ProposedNewLocation, CurrentQuat))
	{
		// Collision detected - don't move
		return;
	}

	// Apply movement
	SetActorLocation(ProposedNewLocation);
}

bool AShip::InitializeFromDetectedBlocks(
	const TMap<FIntVector, EBlockType>& DetectedBlocks,
	FIntVector ControlBlockWorldPos,
	AVoxelWorld* InVoxelWorld)
{
	if (DetectedBlocks.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AShip::Initialize] No blocks to transfer"));
		return false;
	}

	if (!InVoxelWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AShip::Initialize] No VoxelWorld provided"));
		return false;
	}

	VoxelWorld = InVoxelWorld;
	ControlBlockWorldPosition = ControlBlockWorldPos;

	// Convert world coordinates to ship-local coordinates and store
	for (const auto& Pair : DetectedBlocks)
	{
		const FIntVector WorldCoord = Pair.Key;
		const EBlockType BlockType = Pair.Value;
		const FIntVector LocalCoord = WorldToShipCoord(WorldCoord);

		ShipBlocks.Add(LocalCoord, BlockType);
	}

	UE_LOG(LogTemp, Log, TEXT("[AShip::Initialize] Step 1: Stored %d blocks in ship grid"), ShipBlocks.Num());

	// Log physics state before atomic transfer
	if (UWorld* World = GetWorld())
	{
		if (ACharacter* PlayerCharacter = World->GetFirstPlayerController()->GetCharacter())
		{
			FVector PlayerPos = PlayerCharacter->GetActorLocation();
			UE_LOG(LogTemp, Warning, TEXT("[AShip::Initialize] *** PHYSICS LOCK START *** Player at Z=%.1f, Ship at Z=%.1f"),
				PlayerPos.Z, (float)(ControlBlockWorldPosition.Z * BLOCK_SIZE));
		}
	}

	// Step 1: Build mesh on the ship via greedy meshing
	if (!BuildShipMesh())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AShip::Initialize] Failed to build ship mesh"));
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		if (ACharacter* PlayerCharacter = World->GetFirstPlayerController()->GetCharacter())
		{
			FVector PlayerPos = PlayerCharacter->GetActorLocation();
			UE_LOG(LogTemp, Warning, TEXT("[AShip::Initialize]   After mesh build: Player at Z=%.1f (collision ENABLED)"), PlayerPos.Z);
		}
	}

	// Step 2: Disable collision on world blocks (keep them in world grid temporarily)
	TSet<FIntVector> BlockKeysSet;
	for (const auto& Pair : DetectedBlocks)
	{
		BlockKeysSet.Add(Pair.Key);
	}
	DisableWorldBlockCollision(BlockKeysSet);
	UE_LOG(LogTemp, Log, TEXT("[AShip::Initialize] Step 3: Disabled collision on %d world blocks"), DetectedBlocks.Num());

	if (UWorld* World = GetWorld())
	{
		if (ACharacter* PlayerCharacter = World->GetFirstPlayerController()->GetCharacter())
		{
			FVector PlayerPos = PlayerCharacter->GetActorLocation();
			UE_LOG(LogTemp, Warning, TEXT("[AShip::Initialize]   After collision disable: Player at Z=%.1f"), PlayerPos.Z);
		}
	}

	// Step 3: Schedule removal of world blocks after one physics tick
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			this,
			&AShip::RemoveBlocksFromWorld);
		UE_LOG(LogTemp, Log, TEXT("[AShip::Initialize] Step 4: Scheduled world block removal for next tick"));
	}

	return true;
}

bool AShip::BuildShipMesh()
{
	if (!ShipMeshComponent || ShipBlocks.Num() == 0)
	{
		return false;
	}

	// Use greedy meshing to generate efficient geometry
	RebuildMesh();

	// Enable collision on the mesh
	ShipMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ShipMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	ShipMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);

	UE_LOG(LogTemp, Log, TEXT("[AShip::BuildShipMesh] Built greedy mesh for %d blocks"), ShipBlocks.Num());

	return true;
}

void AShip::RebuildMesh()
{
	if (!ShipMeshComponent)
	{
		return;
	}

	// Clear existing mesh
	ShipMeshComponent->ClearAllMeshSections();

	// Generate greedy mesh
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> LinearColors;

	BuildGreedyMesh(Vertices, Triangles, Normals, UVs, LinearColors);

	// Create mesh section
	if (Vertices.Num() > 0 && Triangles.Num() > 0)
	{
		// Convert to newer API signature with LinearColor for full float precision
		TArray<FVector2D> UV0 = UVs;  // First UV channel
		TArray<FProcMeshTangent> Tangents;
		
		ShipMeshComponent->CreateMeshSection_LinearColor(
			0,                          // SectionIndex
			Vertices,
			Triangles,
			Normals,
			UV0,                        // UVs
			LinearColors,               // Vertex colors (stores texture atlas info)
			Tangents,                   // Tangents
			true);                      // bCreateCollision - build collision for this section

		// Apply the voxel world material if available
		if (VoxelWorld)
		{
			UMaterialInterface* Mat = VoxelWorld->GetChunkMaterial();
			if (Mat)
			{
				ShipMeshComponent->SetMaterial(0, Mat);
			}
		}
	}
}

void AShip::BuildGreedyMesh(
	TArray<FVector>&       OutVertices,
	TArray<int32>&         OutTriangles,
	TArray<FVector>&       OutNormals,
	TArray<FVector2D>&     OutUVs,
	TArray<FLinearColor>&  OutColors)
{
	// Get atlas info from VoxelWorld if available
	const bool bHasWorld = (VoxelWorld != nullptr);
	const int32 AtlasCols = bHasWorld ? FMath::Max(1, VoxelWorld->GetAtlasCols()) : 1;
	const int32 AtlasRows = AtlasCols; // square atlas
	const float TileU = 1.f / static_cast<float>(AtlasCols);
	const float TileV = 1.f / static_cast<float>(AtlasRows);

	// Helper: Check if a block at local coordinate is air (solid → false)
	auto IsAir = [this](const FIntVector& LocalCoord) -> bool
	{
		const EBlockType* Found = ShipBlocks.Find(LocalCoord);
		// Missing key or Air type → treat as air (face visible)
		return !Found || *Found == EBlockType::Air;
	};

	// Helper: Get texture atlas position for a face
	auto FaceTileOrigin = [&](EBlockType Type, int32 FaceAxis, int32 FaceSign,
	                           float& OutU, float& OutV)
	{
		FString TexName;
		if (bHasWorld)
		{
			const FBlockDefinition& Def = VoxelWorld->GetBlockDefinition(Type);
			if      (FaceAxis == 2 && FaceSign > 0) TexName = Def.TextureTop;
			else if (FaceAxis == 2 && FaceSign < 0) TexName = Def.TextureBottom;
			else                                    TexName = Def.TextureSide;
			if (TexName.IsEmpty()) TexName = Def.TextureSide;
		}
		const int32 TileIdx = bHasWorld ? VoxelWorld->GetTileIndex(TexName) : 0;
		OutU = static_cast<float>(TileIdx % AtlasCols) * TileU;
		OutV = static_cast<float>(TileIdx / AtlasCols) * TileV;
	};

	// Bounding box of all blocks
	FIntVector MinBound(INT32_MAX);
	FIntVector MaxBound(INT32_MIN);
	for (const auto& Pair : ShipBlocks)
	{
		MinBound = MinBound.ComponentMin(Pair.Key);
		MaxBound = MaxBound.ComponentMax(Pair.Key);
	}

	if (ShipBlocks.Num() == 0)
	{
		return; // No blocks to mesh
	}

	// Greedy meshing: 6 directional passes
	for (int32 d = 0; d < 3; ++d)
	{
		const int32 u = (d + 1) % 3;
		const int32 v = (d + 2) % 3;

		// Build 2D face mask for each slice perpendicular to axis d
		const int32 MaskW = (MaxBound[u] - MinBound[u] + 1) + 2; // +2 for boundary
		const int32 MaskH = (MaxBound[v] - MinBound[v] + 1) + 2;
		TArray<int32> Mask;
		Mask.SetNumZeroed(MaskW * MaskH);

		// Slice range includes boundary (one beyond min/max)
		for (int32 s = MinBound[d] - 1; s <= MaxBound[d]; ++s)
		{
			// Build mask for this slice
			for (int32 j = 0; j < MaskH; ++j)
			{
				for (int32 i = 0; i < MaskW; ++i)
				{
					const int32 ui = MinBound[u] - 1 + i;
					const int32 vi = MinBound[v] - 1 + j;

					FIntVector PosA(0), PosB(0);
					PosA[d] = s;     PosA[u] = ui; PosA[v] = vi;
					PosB[d] = s + 1; PosB[u] = ui; PosB[v] = vi;

					const bool bAOpaque = !IsAir(PosA);
					const bool bBOpaque = !IsAir(PosB);

					if (bAOpaque == bBOpaque)
					{
						Mask[i + j * MaskW] = 0; // No face
					}
					else if (bAOpaque)
					{
						// Face with +d normal
						const EBlockType* BlockA = ShipBlocks.Find(PosA);
						Mask[i + j * MaskW] = BlockA ? static_cast<int32>(*BlockA) : 1;
					}
					else
					{
						// Face with −d normal
						const EBlockType* BlockB = ShipBlocks.Find(PosB);
						Mask[i + j * MaskW] = BlockB ? -static_cast<int32>(*BlockB) : -1;
					}
				}
			}

			// Greedy merge: find rectangles
			for (int32 j = 0; j < MaskH; ++j)
			{
				for (int32 i = 0; i < MaskW; )
				{
					const int32 FaceVal = Mask[i + j * MaskW];
					if (FaceVal == 0)
					{
						++i;
						continue;
					}

					// Expand width
					int32 w = 1;
					while (i + w < MaskW && Mask[(i + w) + j * MaskW] == FaceVal)
					{
						++w;
					}

					// Expand height
					int32 h = 1;
					bool bDone = false;
					while (!bDone && j + h < MaskH)
					{
						for (int32 k = 0; k < w; ++k)
						{
							if (Mask[(i + k) + (j + h) * MaskW] != FaceVal)
							{
								bDone = true;
								break;
							}
						}
						if (!bDone) ++h;
					}

					// Emit quad
					const int32 BaseIdx = OutVertices.Num();
					const float FaceD = static_cast<float>(s + 1) * BLOCK_SIZE;

					auto MakeVtx = [&](int32 uCoord, int32 vCoord) -> FVector
					{
						FVector P(0.f);
						P[d] = FaceD;
						P[u] = static_cast<float>(MinBound[u] - 1 + uCoord) * BLOCK_SIZE;
						P[v] = static_cast<float>(MinBound[v] - 1 + vCoord) * BLOCK_SIZE;
						return P;
					};

					OutVertices.Add(MakeVtx(i,     j    ));
					OutVertices.Add(MakeVtx(i + w, j    ));
					OutVertices.Add(MakeVtx(i + w, j + h));
					OutVertices.Add(MakeVtx(i,     j + h));

					// Normals
					FVector Normal(0.f);
					Normal[d] = (FaceVal > 0) ? 1.f : -1.f;
					OutNormals.Add(Normal);
					OutNormals.Add(Normal);
					OutNormals.Add(Normal);
					OutNormals.Add(Normal);

					// UVs and colors (texture atlas)
					{
						EBlockType BType = static_cast<EBlockType>(FMath::Abs(FaceVal));
						float AU, AV;
						FaceTileOrigin(BType, d, FaceVal, AU, AV);

						if (d == 1)
						{
							OutUVs.Add(FVector2D(0.f,       (float)w ));
							OutUVs.Add(FVector2D(0.f,       0.f      ));
							OutUVs.Add(FVector2D((float)h,  0.f      ));
							OutUVs.Add(FVector2D((float)h,  (float)w ));
						}
						else if (d == 2 && FaceVal > 0)
						{
							OutUVs.Add(FVector2D(0.f,      (float)h ));
							OutUVs.Add(FVector2D((float)w, (float)h ));
							OutUVs.Add(FVector2D((float)w, 0.f      ));
							OutUVs.Add(FVector2D(0.f,      0.f      ));
						}
						else
						{
							OutUVs.Add(FVector2D(0.f,      (float)h ));
							OutUVs.Add(FVector2D((float)w, (float)h ));
							OutUVs.Add(FVector2D((float)w, 0.f      ));
							OutUVs.Add(FVector2D(0.f,      0.f      ));
						}

						const FLinearColor TileColor(AU, AV, TileU, 1.0f);
						OutColors.Add(TileColor);
						OutColors.Add(TileColor);
						OutColors.Add(TileColor);
						OutColors.Add(TileColor);
					}

					// Triangles
					if (FaceVal > 0)
					{
						OutTriangles.Add(BaseIdx + 0);
						OutTriangles.Add(BaseIdx + 3);
						OutTriangles.Add(BaseIdx + 1);
						OutTriangles.Add(BaseIdx + 3);
						OutTriangles.Add(BaseIdx + 2);
						OutTriangles.Add(BaseIdx + 1);
					}
					else
					{
						OutTriangles.Add(BaseIdx + 0);
						OutTriangles.Add(BaseIdx + 1);
						OutTriangles.Add(BaseIdx + 3);
						OutTriangles.Add(BaseIdx + 1);
						OutTriangles.Add(BaseIdx + 2);
						OutTriangles.Add(BaseIdx + 3);
					}

					// Mark cells as used
					for (int32 jj = j; jj < j + h; ++jj)
					{
						for (int32 ii = i; ii < i + w; ++ii)
						{
							Mask[ii + jj * MaskW] = 0;
						}
					}

					i += w;
				}
			}
		}
	}
}

void AShip::DisableWorldBlockCollision(const TSet<FIntVector>& BlocksToTransfer)
{
	if (!VoxelWorld)
	{
		return;
	}

	// Store blocks for later removal
	PendingRemovalBlocks = BlocksToTransfer;

	// For now, just log which blocks we're tracking
	// In a full implementation, you'd mark these blocks as "collision disabled" in the world
	UE_LOG(LogTemp, Log, TEXT("[AShip::DisableWorldBlockCollision] Marked %d blocks for deferred removal"), PendingRemovalBlocks.Num());
}

void AShip::RemoveBlocksFromWorld()
{
	if (!VoxelWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AShip::RemoveBlocksFromWorld] VoxelWorld not available"));
		return;
	}

	// Log player position before removal
	if (UWorld* World = GetWorld())
	{
		if (ACharacter* PlayerCharacter = World->GetFirstPlayerController()->GetCharacter())
		{
			FVector PlayerPos = PlayerCharacter->GetActorLocation();
			UE_LOG(LogTemp, Warning, TEXT("[AShip::RemoveBlocksFromWorld] *** BEFORE REMOVAL *** Player at Z=%.1f"), PlayerPos.Z);
		}
	}

	// Remove all pending blocks from world grid
	for (const FIntVector& WorldCoord : PendingRemovalBlocks)
	{
		VoxelWorld->SetBlockAt(WorldCoord, EBlockType::Air);
	}

	UE_LOG(LogTemp, Log, TEXT("[AShip::RemoveBlocksFromWorld] Removed %d blocks from world grid"), PendingRemovalBlocks.Num());

	// Log player position after removal
	if (UWorld* World = GetWorld())
	{
		if (ACharacter* PlayerCharacter = World->GetFirstPlayerController()->GetCharacter())
		{
			FVector PlayerPos = PlayerCharacter->GetActorLocation();
			UE_LOG(LogTemp, Warning, TEXT("[AShip::RemoveBlocksFromWorld] *** AFTER REMOVAL *** Player at Z=%.1f, *** PHYSICS LOCK END ***"), PlayerPos.Z);
		}
	}

	PendingRemovalBlocks.Empty();
}

void AShip::Depossess()
{
	APlayerController* PC = GetController<APlayerController>();
	if (!PC) return;

	// Find the original character to re-possess
	for (TActorIterator<ACryptWorldCharacter> It(GetWorld()); It; ++It)
	{
		ACryptWorldCharacter* Character = *It;
		if (Character && Character->GetController() == nullptr)
		{
			// Found an unpossessed character, possess it
			UE_LOG(LogTemp, Log, TEXT("[AShip::Depossess] Depossessing ship, returning to character"));
			PC->UnPossess();
			PC->Possess(Character);
			return;
		}
	}

	// If no unpossessed character found, just unpossess the ship
	UE_LOG(LogTemp, Warning, TEXT("[AShip::Depossess] No character found to return to"));
	PC->UnPossess();
}

EBlockType AShip::GetBlockAt(FIntVector LocalBlockCoord) const
{
	const EBlockType* Found = ShipBlocks.Find(LocalBlockCoord);
	return Found ? *Found : EBlockType::Air;
}

void AShip::SetBlockAt(FIntVector LocalBlockCoord, EBlockType BlockType)
{
	if (BlockType == EBlockType::Air)
	{
		ShipBlocks.Remove(LocalBlockCoord);
	}
	else
	{
		ShipBlocks.Add(LocalBlockCoord, BlockType);
	}

	// Rebuild the mesh to reflect the change
	RebuildMesh();

	UE_LOG(LogTemp, Log, TEXT("[AShip::SetBlockAt] Block added/removed at local %d,%d,%d - Ship now has %d blocks"),
		LocalBlockCoord.X, LocalBlockCoord.Y, LocalBlockCoord.Z, ShipBlocks.Num());
}

bool AShip::CanMoveToPosition(FVector NewWorldPos, FQuat NewRotation) const
{
	if (!VoxelWorld)
	{
		// Can't check collision without access to world grid
		return true;
	}

	// Check only boundary blocks (blocks with at least one exposed face) for collision
	for (const auto& Pair : ShipBlocks)
	{
		FIntVector LocalBlockCoord = Pair.Key;
		EBlockType ShipBlockType = Pair.Value;

		// Skip air blocks
		if (ShipBlockType == EBlockType::Air)
		{
			continue;
		}

		// Check if this block is a boundary block (has at least one exposed face)
		bool bIsBoundaryBlock = false;
		for (int32 d = 0; d < 6; ++d)
		{
			FIntVector NeighborCoord;
			if (d == 0) NeighborCoord = LocalBlockCoord + FIntVector(1, 0, 0);
			else if (d == 1) NeighborCoord = LocalBlockCoord + FIntVector(-1, 0, 0);
			else if (d == 2) NeighborCoord = LocalBlockCoord + FIntVector(0, 1, 0);
			else if (d == 3) NeighborCoord = LocalBlockCoord + FIntVector(0, -1, 0);
			else if (d == 4) NeighborCoord = LocalBlockCoord + FIntVector(0, 0, 1);
			else NeighborCoord = LocalBlockCoord + FIntVector(0, 0, -1);

			// If neighbor doesn't exist or is air, this block has an exposed face
			if (!ShipBlocks.Contains(NeighborCoord) || ShipBlocks[NeighborCoord] == EBlockType::Air)
			{
				bIsBoundaryBlock = true;
				break;
			}
		}

		// Skip interior blocks (not on the boundary)
		if (!bIsBoundaryBlock)
		{
			continue;
		}

		// Transform local block coordinate to world coordinate at the proposed position/rotation
		FVector LocalBlockWorldPos = FVector(LocalBlockCoord) * BLOCK_SIZE;  // Convert to world units
		FVector RotatedLocalPos = NewRotation.RotateVector(LocalBlockWorldPos);
		FVector TransformedWorldPos = NewWorldPos + RotatedLocalPos;

		// Convert to block grid coordinates (center of the block)
		FIntVector CenterBlockCoord = FIntVector(
			FMath::FloorToInt(TransformedWorldPos.X / BLOCK_SIZE),
			FMath::FloorToInt(TransformedWorldPos.Y / BLOCK_SIZE),
			FMath::FloorToInt(TransformedWorldPos.Z / BLOCK_SIZE)
		);

		// Check a 3x3x3 neighborhood of block coordinates around the center
		for (int32 dx = -1; dx <= 1; ++dx)
		{
			for (int32 dy = -1; dy <= 1; ++dy)
			{
				for (int32 dz = -1; dz <= 1; ++dz)
				{
					FIntVector CheckCoord = CenterBlockCoord + FIntVector(dx, dy, dz);
					EBlockType WorldBlockAtPos = VoxelWorld->GetBlockAt(CheckCoord);

					// If there's a solid block, we have a collision
					if (WorldBlockAtPos != EBlockType::Air && WorldBlockAtPos != EBlockType::Water)
					{
						UE_LOG(LogTemp, Warning, TEXT("[AShip::CanMoveToPosition] Collision detected! Ship block at local %d,%d,%d would collide with world block type %d at world %d,%d,%d"),
							LocalBlockCoord.X, LocalBlockCoord.Y, LocalBlockCoord.Z,
							(int32)WorldBlockAtPos,
							CheckCoord.X, CheckCoord.Y, CheckCoord.Z);
						return false;
					}
				}
			}
		}
	}

	// No collisions found
	return true;
}
