// CryptWorldCharacter.cpp

#include "CryptWorldCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "EngineUtils.h"
#include "Voxel/VoxelWorld.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemPickup.h"
#include "Items/ItemData.h"
#include "UI/HotbarWidget.h"
#include "UI/InventoryWidget.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/DataTable.h"
#include "GameFramework/PlayerController.h"

ACryptWorldCharacter::ACryptWorldCharacter()
{
	// Create inventory component
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));

	// Load the default Input Mapping Context
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMC(
		TEXT("/Game/Input/IMC_Default"));
	if (IMC.Succeeded())
		DefaultMappingContext = IMC.Object;

	// Load the mouse-look Mapping Context
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMC_Mouse(
		TEXT("/Game/Input/IMC_MouseLook"));
	if (IMC_Mouse.Succeeded())
		MouseLookMappingContext = IMC_Mouse.Object;

	// Load Input Actions and assign to the parent's protected UPROPERTY fields
	static ConstructorHelpers::FObjectFinder<UInputAction> JumpAsset(
		TEXT("/Game/Input/Actions/IA_Jump"));
	if (JumpAsset.Succeeded())
		JumpAction = JumpAsset.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> MoveAsset(
		TEXT("/Game/Input/Actions/IA_Move"));
	if (MoveAsset.Succeeded())
		MoveAction = MoveAsset.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> LookAsset(
		TEXT("/Game/Input/Actions/IA_Look"));
	if (LookAsset.Succeeded())
		LookAction = LookAsset.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> MouseLookAsset(
		TEXT("/Game/Input/Actions/IA_MouseLook"));
	if (MouseLookAsset.Succeeded())
		MouseLookAction = MouseLookAsset.Object;

	// Default pickup class and block-drop mapping (editable in Blueprint details).
	ItemPickupClass = AItemPickup::StaticClass();
	BlockTypeToItemID.Add(EBlockType::Grass,   FName(TEXT("grass")));
	BlockTypeToItemID.Add(EBlockType::Dirt,    FName(TEXT("dirt")));
	BlockTypeToItemID.Add(EBlockType::Stone,   FName(TEXT("stone")));
	BlockTypeToItemID.Add(EBlockType::Sand,    FName(TEXT("sand")));
	BlockTypeToItemID.Add(EBlockType::Gravel,  FName(TEXT("gravel")));
	BlockTypeToItemID.Add(EBlockType::OakLog,  FName(TEXT("oak_log")));
	BlockTypeToItemID.Add(EBlockType::OakLeaves, FName(TEXT("oak_leaves")));
	BlockTypeToItemID.Add(EBlockType::OakPlanks, FName(TEXT("oak_planks")));
	BlockTypeToItemID.Add(EBlockType::Cobblestone, FName(TEXT("cobblestone")));
	BlockTypeToItemID.Add(EBlockType::CoalOre, FName(TEXT("coal_ore")));
	BlockTypeToItemID.Add(EBlockType::CopperOre, FName(TEXT("copper_ore")));
	BlockTypeToItemID.Add(EBlockType::IronOre, FName(TEXT("iron_ore")));
}

void ACryptWorldCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Bind mouse buttons directly (no Enhanced Input asset needed).
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton,  IE_Pressed, this, &ACryptWorldCharacter::BreakBlock);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ACryptWorldCharacter::PlaceBlock);
	PlayerInputComponent->BindKey(EKeys::E,                IE_Pressed, this, &ACryptWorldCharacter::ToggleInventory);

	// Hotbar slot selection: keys 1-0 map to slots 0-9
	PlayerInputComponent->BindKey(EKeys::One,   IE_Pressed, this, &ACryptWorldCharacter::Hotbar_SelectSlot_0);
	PlayerInputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &ACryptWorldCharacter::Hotbar_SelectSlot_1);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ACryptWorldCharacter::Hotbar_SelectSlot_2);
	PlayerInputComponent->BindKey(EKeys::Four,  IE_Pressed, this, &ACryptWorldCharacter::Hotbar_SelectSlot_3);
	PlayerInputComponent->BindKey(EKeys::Five,  IE_Pressed, this, &ACryptWorldCharacter::Hotbar_SelectSlot_4);
	PlayerInputComponent->BindKey(EKeys::Six,   IE_Pressed, this, &ACryptWorldCharacter::Hotbar_SelectSlot_5);
	PlayerInputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &ACryptWorldCharacter::Hotbar_SelectSlot_6);
	PlayerInputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &ACryptWorldCharacter::Hotbar_SelectSlot_7);
	PlayerInputComponent->BindKey(EKeys::Nine,  IE_Pressed, this, &ACryptWorldCharacter::Hotbar_SelectSlot_8);
	PlayerInputComponent->BindKey(EKeys::Zero,  IE_Pressed, this, &ACryptWorldCharacter::Hotbar_SelectSlot_9);

	// Hotbar scroll wheel
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp,   IE_Pressed, this, &ACryptWorldCharacter::Hotbar_ScrollUp);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ACryptWorldCharacter::Hotbar_ScrollDown);
}

void ACryptWorldCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Yellow, TEXT("[CryptWorldCharacter::BeginPlay] *** CHARACTER BEGINPLAY CALLED ***"));
	}
	UE_LOG(LogTemp, Warning, TEXT("[CryptWorldCharacter::BeginPlay] *** CHARACTER BEGINPLAY CALLED ***"));

	// Cache the VoxelWorld so interaction methods don't iterate every call.
	for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
	{
		VoxelWorld = *It;
		break;
	}

	// The parent attaches the camera to FirstPersonMesh at socket "head".
	// Without a skeletal mesh asset assigned there is no "head" socket, so
	// the camera ends up at the mesh origin (feet level).  Re-attach it to
	// the capsule at eye height so the player can actually see.
	if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
	{
		Cam->AttachToComponent(
			GetCapsuleComponent(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Cam->SetRelativeLocation(FVector(0.f, 0.f, 60.f));   // ~eye height
		Cam->SetRelativeRotation(FRotator::ZeroRotator);
		Cam->bUsePawnControlRotation = true;
	}

	// Load ItemDataTable dynamically for the inventory system
	UE_LOG(LogTemp, Log, TEXT("[CryptWorldCharacter::BeginPlay] Starting ItemDataTable setup..."));
	
	if (InventoryComponent)
	{
		UE_LOG(LogTemp, Log, TEXT("[CryptWorldCharacter::BeginPlay] InventoryComponent found"));
		
		UDataTable* ItemTable = InventoryComponent->ItemDataTable;
		
		// Load if not already assigned
		if (!ItemTable)
		{
			UE_LOG(LogTemp, Log, TEXT("[CryptWorldCharacter::BeginPlay] ItemDataTable is null, attempting to load..."));
			const FString ItemTablePath = TEXT("/Game/Data/DT_Items");
			ItemTable = LoadObject<UDataTable>(nullptr, *ItemTablePath);
			if (ItemTable)
			{
				InventoryComponent->ItemDataTable = ItemTable;
				UE_LOG(LogTemp, Log, TEXT("[CryptWorldCharacter::BeginPlay] ✓ ItemDataTable loaded: %s"), *ItemTablePath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[CryptWorldCharacter::BeginPlay] ✗ FAILED to load ItemDataTable from: %s"), *ItemTablePath);
				ItemTable = nullptr;
			}
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[CryptWorldCharacter::BeginPlay] ItemDataTable already assigned"));
		}

		// Populate rows if table exists
		if (ItemTable)
		{
			UE_LOG(LogTemp, Log, TEXT("[CryptWorldCharacter::BeginPlay] Populating item definitions..."));

			// Define all required block item entries with texture paths
			TMap<FName, TPair<FText, FString>> ItemDefinitions;
			ItemDefinitions.Add(TEXT("grass"), TPair<FText, FString>(FText::FromString(TEXT("Grass Block")), TEXT("/Game/Textures/Blocks/grass_top")));
			ItemDefinitions.Add(TEXT("dirt"), TPair<FText, FString>(FText::FromString(TEXT("Dirt")), TEXT("/Game/Textures/Blocks/dirt")));
			ItemDefinitions.Add(TEXT("stone"), TPair<FText, FString>(FText::FromString(TEXT("Stone")), TEXT("/Game/Textures/Blocks/stone")));
			ItemDefinitions.Add(TEXT("sand"), TPair<FText, FString>(FText::FromString(TEXT("Sand")), TEXT("/Game/Textures/Blocks/sand")));
			ItemDefinitions.Add(TEXT("gravel"), TPair<FText, FString>(FText::FromString(TEXT("Gravel")), TEXT("/Game/Textures/Blocks/gravel")));
			ItemDefinitions.Add(TEXT("oak_log"), TPair<FText, FString>(FText::FromString(TEXT("Oak Log")), TEXT("/Game/Textures/Blocks/oaklog_side")));
			ItemDefinitions.Add(TEXT("oak_leaves"), TPair<FText, FString>(FText::FromString(TEXT("Oak Leaves")), TEXT("/Game/Textures/Blocks/grass_top")));
			ItemDefinitions.Add(TEXT("oak_planks"), TPair<FText, FString>(FText::FromString(TEXT("Oak Planks")), TEXT("/Game/Textures/Blocks/oakplanks")));
			ItemDefinitions.Add(TEXT("cobblestone"), TPair<FText, FString>(FText::FromString(TEXT("Cobblestone")), TEXT("/Game/Textures/Blocks/cobblestone")));
			ItemDefinitions.Add(TEXT("coal_ore"), TPair<FText, FString>(FText::FromString(TEXT("Coal Ore")), TEXT("/Game/Textures/Blocks/coalore")));
			ItemDefinitions.Add(TEXT("copper_ore"), TPair<FText, FString>(FText::FromString(TEXT("Copper Ore")), TEXT("/Game/Textures/Blocks/silverore")));
			ItemDefinitions.Add(TEXT("iron_ore"), TPair<FText, FString>(FText::FromString(TEXT("Iron Ore")), TEXT("/Game/Textures/Blocks/stone")));

			int32 CreatedCount = 0;
			for (const auto& Pair : ItemDefinitions)
			{
				const FName& ItemID = Pair.Key;
				const FText& DisplayName = Pair.Value.Key;
				const FString& TexturePath = Pair.Value.Value;

				if (ItemTable->FindRow<FItemData>(ItemID, TEXT("Startup")))
				{
					UE_LOG(LogTemp, Log, TEXT("  ✓ Row exists: %s"), *ItemID.ToString());
				}
				else
				{
					FItemData NewItem;
					NewItem.ItemID = ItemID;
					NewItem.DisplayName = DisplayName;
					NewItem.Description = FText::FromString(FString::Printf(TEXT("A block of %s"), *DisplayName.ToString()));
					NewItem.MaxStackSize = 64;
					NewItem.ItemType = EItemType::Block;
					NewItem.bDroppable = true;
					NewItem.Rarity = 0;
					
					// Load block texture as soft reference (won't load until needed)
					NewItem.Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(*FString::Printf(TEXT("%s.%s"), *TexturePath, *FPaths::GetBaseFilename(TexturePath))));

					ItemTable->AddRow(ItemID, NewItem);
					++CreatedCount;
					UE_LOG(LogTemp, Log, TEXT("  + Added: %s with texture: %s"), *ItemID.ToString(), *TexturePath);
				}
			}

			UE_LOG(LogTemp, Warning, TEXT("[CryptWorldCharacter::BeginPlay] ✓✓✓ ItemDataTable ready. Created %d new rows."), CreatedCount);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[CryptWorldCharacter::BeginPlay] ✗ ItemDataTable unavailable after load attempt"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[CryptWorldCharacter::BeginPlay] ✗ InventoryComponent is null!"));
	}

	// Create and show the persistent hotbar
	if (HotbarWidgetClass)
	{
		HotbarWidgetInstance = CreateWidget<UHotbarWidget>(GetWorld(), HotbarWidgetClass);
		if (HotbarWidgetInstance)
		{
			HotbarWidgetInstance->AddToViewport(0);  // z-order 0 (behind inventory)
			HotbarWidgetInstance->Init(InventoryComponent);
		}
	}

	// Register the Enhanced Input Mapping Context with the local player
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
			if (MouseLookMappingContext)
			{
				Subsystem->AddMappingContext(MouseLookMappingContext, 1);
			}
		}
	}
}

// ---------------------------------------------------------------------------
//  Block interaction
// ---------------------------------------------------------------------------

bool ACryptWorldCharacter::TraceBlock(FHitResult& OutHit) const
{
	UCameraComponent* Cam = GetFirstPersonCameraComponent();
	if (!Cam) return false;

	const FVector Start = Cam->GetComponentLocation();
	const FVector End   = Start + Cam->GetForwardVector() * TraceRange;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);  // don't hit ourselves

	return GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params);
}

void ACryptWorldCharacter::BreakBlock()
{
	if (!VoxelWorld) return;

	FHitResult Hit;
	if (!TraceBlock(Hit)) return;

	// Step slightly INWARD along the negative normal to land inside the hit block.
	const FVector InsideBlock = Hit.ImpactPoint - Hit.ImpactNormal * (BLOCK_SIZE * 0.5f);

	const FIntVector WorldVoxel(
		FMath::FloorToInt(InsideBlock.X / BLOCK_SIZE),
		FMath::FloorToInt(InsideBlock.Y / BLOCK_SIZE),
		FMath::FloorToInt(InsideBlock.Z / BLOCK_SIZE)
	);

	const EBlockType BrokenType = VoxelWorld->GetBlockAt(WorldVoxel);
	if (BrokenType == EBlockType::Air)
	{
		return;
	}

	if (const FName* DropItemID = BlockTypeToItemID.Find(BrokenType))
	{
		if (!DropItemID->IsNone())
		{
			UWorld* World = GetWorld();
			if (World)
			{
				const FVector BlockCenter(
					(WorldVoxel.X + 0.5f) * BLOCK_SIZE,
					(WorldVoxel.Y + 0.5f) * BLOCK_SIZE,
					(WorldVoxel.Z + 0.5f) * BLOCK_SIZE);

				// Spawn 50 units above the block to clear chunk geometry before collision is enabled
				const FVector SpawnLocation = BlockCenter + FVector(0.f, 0.f, 50.f);

				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

				TSubclassOf<AItemPickup> SpawnClass = ItemPickupClass;
				if (!SpawnClass)
				{
					SpawnClass = AItemPickup::StaticClass();
				}
				if (AItemPickup* Pickup = AItemPickup::SpawnManagedPickup(
					World,
					SpawnClass,
					*DropItemID,
					1,
					SpawnLocation,
					FRotator::ZeroRotator,
					SpawnParams))
				{
					const FVector RandomizedLinearImpulse(
						FMath::FRandRange(-70.f, 70.f),
						FMath::FRandRange(-70.f, 70.f),
						DropSpawnImpulseZ + FMath::FRandRange(-30.f, 30.f));

					const FVector RandomizedAngularImpulse(
						FMath::FRandRange(-90.f, 90.f),
						FMath::FRandRange(-90.f, 90.f),
						FMath::FRandRange(-40.f, 40.f));

					Pickup->ApplySpawnImpulse(RandomizedLinearImpulse);
					Pickup->ApplySpawnAngularImpulse(RandomizedAngularImpulse);
				}
			}
		}
	}

	VoxelWorld->SetBlockAt(WorldVoxel, EBlockType::Air);
}

void ACryptWorldCharacter::PlaceBlock()
{
	if (!VoxelWorld) return;

	// Get the active hotbar slot
	if (!InventoryComponent) return;
	FInventorySlot ActiveSlot = InventoryComponent->GetActiveHotbarSlot();
	if (ActiveSlot.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlaceBlock] Active hotbar slot is empty"));
		return;
	}

	// Find the block type that corresponds to the ItemID
	EBlockType BlockToPlace = EBlockType::Air;
	for (const auto& Pair : BlockTypeToItemID)
	{
		if (Pair.Value == ActiveSlot.ItemID)
		{
			BlockToPlace = Pair.Key;
			break;
		}
	}

	if (BlockToPlace == EBlockType::Air)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlaceBlock] ItemID '%s' is not a placeable block"), *ActiveSlot.ItemID.ToString());
		return;
	}

	FHitResult Hit;
	if (!TraceBlock(Hit)) return;

	// Step slightly OUTWARD along the normal to land inside the adjacent empty block.
	const FVector AdjacentBlock = Hit.ImpactPoint + Hit.ImpactNormal * (BLOCK_SIZE * 0.5f);

	const FIntVector WorldVoxel(
		FMath::FloorToInt(AdjacentBlock.X / BLOCK_SIZE),
		FMath::FloorToInt(AdjacentBlock.Y / BLOCK_SIZE),
		FMath::FloorToInt(AdjacentBlock.Z / BLOCK_SIZE)
	);

	// Don't place a block inside the player's own capsule.
	const FVector BlockWorldCenter(
		(WorldVoxel.X + 0.5f) * BLOCK_SIZE,
		(WorldVoxel.Y + 0.5f) * BLOCK_SIZE,
		(WorldVoxel.Z + 0.5f) * BLOCK_SIZE
	);

	const FVector PlayerOrigin = GetActorLocation();
	const float   CapsuleHalfH = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float   CapsuleR     = GetCapsuleComponent()->GetScaledCapsuleRadius();

	const bool bInsideCapsule =
		FMath::Abs(BlockWorldCenter.Z - PlayerOrigin.Z) < (CapsuleHalfH + BLOCK_SIZE * 0.5f) &&
		FVector2D(BlockWorldCenter.X - PlayerOrigin.X, BlockWorldCenter.Y - PlayerOrigin.Y).Size()
			< (CapsuleR + BLOCK_SIZE * 0.5f);

	if (bInsideCapsule) return;

	// Place the block
	VoxelWorld->SetBlockAt(WorldVoxel, BlockToPlace);

	// Remove one from inventory
	if (InventoryComponent->RemoveItem(ActiveSlot.ItemID, 1))
	{
		UE_LOG(LogTemp, Log, TEXT("[PlaceBlock] Placed %s, removed from inventory"), *ActiveSlot.ItemID.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlaceBlock] Failed to remove item from inventory: %s"), *ActiveSlot.ItemID.ToString());
	}
}

// ---------------------------------------------------------------------------
//  Hotbar
// ---------------------------------------------------------------------------

void ACryptWorldCharacter::Hotbar_SelectSlot_0() { SelectHotbarSlot(0); }
void ACryptWorldCharacter::Hotbar_SelectSlot_1() { SelectHotbarSlot(1); }
void ACryptWorldCharacter::Hotbar_SelectSlot_2() { SelectHotbarSlot(2); }
void ACryptWorldCharacter::Hotbar_SelectSlot_3() { SelectHotbarSlot(3); }
void ACryptWorldCharacter::Hotbar_SelectSlot_4() { SelectHotbarSlot(4); }
void ACryptWorldCharacter::Hotbar_SelectSlot_5() { SelectHotbarSlot(5); }
void ACryptWorldCharacter::Hotbar_SelectSlot_6() { SelectHotbarSlot(6); }
void ACryptWorldCharacter::Hotbar_SelectSlot_7() { SelectHotbarSlot(7); }
void ACryptWorldCharacter::Hotbar_SelectSlot_8() { SelectHotbarSlot(8); }
void ACryptWorldCharacter::Hotbar_SelectSlot_9() { SelectHotbarSlot(9); }

void ACryptWorldCharacter::Hotbar_ScrollUp()
{
	if (!InventoryComponent) return;
	int32 CurrentIndex = InventoryComponent->ActiveHotbarIndex;
	int32 NextIndex = (CurrentIndex + 1) % UInventoryComponent::HotbarSize;
	SelectHotbarSlot(NextIndex);
}

void ACryptWorldCharacter::Hotbar_ScrollDown()
{
	if (!InventoryComponent) return;
	int32 CurrentIndex = InventoryComponent->ActiveHotbarIndex;
	int32 NextIndex = (CurrentIndex - 1 + UInventoryComponent::HotbarSize) % UInventoryComponent::HotbarSize;
	SelectHotbarSlot(NextIndex);
}

void ACryptWorldCharacter::SelectHotbarSlot(int32 SlotIndex)
{
	if (!InventoryComponent) return;

	InventoryComponent->SetActiveHotbarIndex(SlotIndex);
	UE_LOG(LogTemp, Log, TEXT("[CryptWorldCharacter::SelectHotbarSlot] Switched to hotbar slot %d"), SlotIndex);
}

// ---------------------------------------------------------------------------
//  Inventory UI
// ---------------------------------------------------------------------------

void ACryptWorldCharacter::ToggleInventory()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	if (!bInventoryOpen)
	{
		// ---- Open ----
		if (!InventoryWidgetClass) return;

		InventoryWidgetInstance = CreateWidget<UInventoryWidget>(PC, InventoryWidgetClass);
		if (!InventoryWidgetInstance) return;

		InventoryWidgetInstance->Init(InventoryComponent);
		InventoryWidgetInstance->AddToViewport(1);  // z-order 1 (above hotbar)

		int32 PCViewportX = 0;
		int32 PCViewportY = 0;
		PC->GetViewportSize(PCViewportX, PCViewportY);

		if (GEngine && GEngine->GameViewport)
		{
			FVector2D ViewportSize;
			GEngine->GameViewport->GetViewportSize(ViewportSize);

			UE_LOG(LogTemp, Log,
				TEXT("[InventoryUI] ViewportSize(GameViewport)=%.0f x %.0f | ViewportSize(PlayerController)=%d x %d"),
				ViewportSize.X, ViewportSize.Y, PCViewportX, PCViewportY);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[InventoryUI] GameViewport unavailable | ViewportSize(PlayerController)=%d x %d"),
				PCViewportX, PCViewportY);
		}

		// Alternative centering approach: alignment only, no explicit viewport position.
		InventoryWidgetInstance->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));

		// Show cursor, keep WASD movement fully active
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(InventoryWidgetInstance->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);

		bInventoryOpen = true;
	}
	else
	{
		// ---- Close ----
		if (InventoryWidgetInstance)
		{
			InventoryWidgetInstance->RemoveFromParent();
			InventoryWidgetInstance = nullptr;
		}

		// Return to pure game input (hides cursor, locks mouse)
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);

		bInventoryOpen = false;
	}
}
