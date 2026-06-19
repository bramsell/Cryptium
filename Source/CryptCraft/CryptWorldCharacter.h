// CryptWorldCharacter.h
// Concrete (non-abstract) first-person character for CryptWorld.
// Loads the project's default Input Mapping Context and Input Actions from
// Content so no Blueprint wrapper is required.

#pragma once

#include "CoreMinimal.h"
#include "CryptCraftCharacter.h"
#include "Voxel/VoxelTypes.h"
#include "CryptWorldCharacter.generated.h"

class UInputMappingContext;
class AVoxelWorld;
class UInventoryComponent;
class UHotbarWidget;
class UInventoryWidget;
class AItemPickup;

UCLASS()
class CRYPTCRAFT_API ACryptWorldCharacter : public ACryptCraftCharacter
{
	GENERATED_BODY()

public:
	ACryptWorldCharacter();

	/** Max reach distance in UE units (500 = 5 blocks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Interaction")
	float TraceRange = 500.f;

	/** Block type placed on right-click. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Interaction")
	EBlockType SelectedBlockType = EBlockType::Stone;

	/**
	 * Actor class spawned when a block is mined.
	 * Defaults to AItemPickup and can be overridden in Blueprint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Drops")
	TSubclassOf<AItemPickup> ItemPickupClass;

	/** Data-driven block-drop mapping (BlockType -> item row name, e.g. Stone -> "stone"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Drops")
	TMap<EBlockType, FName> BlockTypeToItemID;

	/** Upward impulse used when a pickup is spawned from mining. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Drops", meta = (ClampMin = "0.0"))
	float DropSpawnImpulseZ = 250.f;

	// -----------------------------------------------------------------------
	//  Inventory
	// -----------------------------------------------------------------------

	/** Main inventory + equipment component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInventoryComponent> InventoryComponent;

	/**
	 * Widget class for the persistent hotbar (WBP_Hotbar).
	 * Assign in the Blueprint child class details panel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|UI")
	TSubclassOf<UHotbarWidget> HotbarWidgetClass;

	/**
	 * Widget class for the full inventory screen (WBP_Inventory).
	 * Assign in the Blueprint child class details panel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|UI")
	TSubclassOf<UInventoryWidget> InventoryWidgetClass;

	/** Toggle the inventory screen open/closed (default key: E). */
	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void ToggleInventory();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	// Loaded at construction from /Game/Input/IMC_Default
	UPROPERTY()
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	// Loaded at construction from /Game/Input/IMC_MouseLook
	UPROPERTY()
	TObjectPtr<UInputMappingContext> MouseLookMappingContext;

	/** Cached reference to the active AVoxelWorld. */
	UPROPERTY()
	TObjectPtr<AVoxelWorld> VoxelWorld;

	/** Persistent hotbar widget instance (lives for the lifetime of the character). */
	UPROPERTY()
	TObjectPtr<UHotbarWidget> HotbarWidgetInstance;

	/** Inventory screen instance (created/destroyed on toggle). */
	UPROPERTY()
	TObjectPtr<UInventoryWidget> InventoryWidgetInstance;

	/** True while the inventory screen is visible. */
	bool bInventoryOpen = false;

	/** Fire a ray from the camera. Returns true and fills OutHit on success. */
	bool TraceBlock(FHitResult& OutHit) const;

	/** Get the block type from the active hotbar slot's ItemID. Returns Air if slot is empty or item is not a block. */
	EBlockType GetBlockTypeFromHotbar();

	/** Left-click: remove the targeted block. */
	UFUNCTION() void BreakBlock();

	/** Right-click: place a block on the targeted face. */
	UFUNCTION() void PlaceBlock();

	/** Hotbar slot selection handlers (keys 1-0). */
	UFUNCTION() void Hotbar_SelectSlot_0();
	UFUNCTION() void Hotbar_SelectSlot_1();
	UFUNCTION() void Hotbar_SelectSlot_2();
	UFUNCTION() void Hotbar_SelectSlot_3();
	UFUNCTION() void Hotbar_SelectSlot_4();
	UFUNCTION() void Hotbar_SelectSlot_5();
	UFUNCTION() void Hotbar_SelectSlot_6();
	UFUNCTION() void Hotbar_SelectSlot_7();
	UFUNCTION() void Hotbar_SelectSlot_8();
	UFUNCTION() void Hotbar_SelectSlot_9();

	/** Hotbar scroll wheel handlers. */
	UFUNCTION() void Hotbar_ScrollUp();
	UFUNCTION() void Hotbar_ScrollDown();

	/** Internal helper to change hotbar slot. */
	void SelectHotbarSlot(int32 SlotIndex);

	/** Trigger ship detection from a control block position. */
	void TriggerShipDetection(FIntVector ControlBlockCoord);
};
