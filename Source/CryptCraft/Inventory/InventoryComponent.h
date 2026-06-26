#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/ItemData.h"
#include "InventoryComponent.generated.h"

class UCraftingSystem;

// ---------------------------------------------------------------------------
//  A single inventory slot — an item ID + how many are stacked
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct CRYPTCRAFT_API FInventorySlot
{
    GENERATED_BODY()

    /** Row name in the ItemDataTable. FName::None means the slot is empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
    FName ItemID;

    /** How many items are stacked in this slot (0 when empty). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot", meta = (ClampMin = "0"))
    int32 StackCount = 0;

    bool IsEmpty() const { return ItemID.IsNone() || StackCount <= 0; }
    void Clear()         { ItemID = FName(); StackCount = 0; }
};

// ---------------------------------------------------------------------------
//  Inventory container types
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EInventoryContainer : uint8
{
    MainGrid       UMETA(DisplayName = "Main Grid"),
    Hotbar         UMETA(DisplayName = "Hotbar"),
    CraftingInput  UMETA(DisplayName = "Crafting Input"),
    CraftingOutput UMETA(DisplayName = "Crafting Output"),
};

// ---------------------------------------------------------------------------
//  UInventoryComponent
//
//  Attach to the player character (or any Actor that carries items).
//  Layout:
//    - MainGrid  : 10 × 5 = 50 slots  (general storage)
//    - Hotbar    : 10 slots             (quick-access bar)
//    - Equipment : one slot per EEquipmentSlot value
//    - CraftingInput : 2 × 2 = 4 slots (recipe inputs)
//    - CraftingOutput : 1 slot         (recipe result)
//
//  Item definitions are looked up at runtime via a UDataTable whose row
//  type is FItemData.  Assign it in the component's details panel or via BP.
// ---------------------------------------------------------------------------

UCLASS(ClassGroup = (CryptCraft), meta = (BlueprintSpawnableComponent))
class CRYPTCRAFT_API UInventoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    // -----------------------------------------------------------------------
    //  Layout constants
    // -----------------------------------------------------------------------

    static constexpr int32 GridWidth  = 10;
    static constexpr int32 GridHeight = 5;
    static constexpr int32 GridSize   = GridWidth * GridHeight;  // 50
    static constexpr int32 HotbarSize = 10;

    static constexpr int32 CraftingInputWidth  = 2;
    static constexpr int32 CraftingInputHeight = 2;
    static constexpr int32 CraftingInputSize   = CraftingInputWidth * CraftingInputHeight;  // 4
    static constexpr int32 CraftingOutputSize  = 1;

    // -----------------------------------------------------------------------
    //  Construction
    // -----------------------------------------------------------------------

    UInventoryComponent();

    // -----------------------------------------------------------------------
    //  Data
    // -----------------------------------------------------------------------

    /**
     * DataTable (row type: FItemData) that holds all item definitions.
     * Assign the asset in the editor or at runtime before calling any API.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
    TObjectPtr<UDataTable> ItemDataTable;

    /** 50-slot main grid (10 columns × 5 rows). Index = row*GridWidth + col. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Grid")
    TArray<FInventorySlot> MainGrid;

    /** 10-slot hotbar. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Hotbar")
    TArray<FInventorySlot> Hotbar;

    /** Equipment slots keyed by their logical slot type. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Equipment")
    TMap<EEquipmentSlot, FInventorySlot> EquipmentSlots;

    /** 4-slot crafting input grid (2×2). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Crafting")
    TArray<FInventorySlot> CraftingInputSlots;

    /** Single slot for crafting output. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Crafting")
    FInventorySlot CraftingOutputSlot;

    /** Currently selected hotbar index (0-based). */
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Inventory|Hotbar")
    int32 ActiveHotbarIndex = 0;

    // -----------------------------------------------------------------------
    //  Events
    // -----------------------------------------------------------------------

    /** Fired whenever any slot in the inventory changes. */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChanged);
    UPROPERTY(BlueprintAssignable, Category = "Inventory")
    FOnInventoryChanged OnInventoryChanged;

    /** Fired when the equipped item in a slot changes. */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEquipmentChanged, EEquipmentSlot, Slot);
    UPROPERTY(BlueprintAssignable, Category = "Inventory")
    FOnEquipmentChanged OnEquipmentChanged;

    /** Fired when the active hotbar index changes. */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHotbarSelectionChanged, int32, NewIndex);
    UPROPERTY(BlueprintAssignable, Category = "Inventory")
    FOnHotbarSelectionChanged OnHotbarSelectionChanged;

    // -----------------------------------------------------------------------
    //  Core API
    // -----------------------------------------------------------------------

    /**
     * Try to add Count of ItemID into the inventory.
     * Stacks onto existing partial stacks first, then fills empty grid slots,
     * then empty hotbar slots.
     * @return Number of items that did NOT fit (0 = fully added).
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory")
    int32 AddItem(FName ItemID, int32 Count = 1);

    /**
     * Remove Count of ItemID from anywhere in the inventory (grid + hotbar).
     * Removes from partial stacks first.
     * @return true if the full count was removed; false if not enough present.
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory")
    bool RemoveItem(FName ItemID, int32 Count = 1);

    /**
     * Returns how many of ItemID exist across grid + hotbar.
     * Does NOT count equipped items.
     */
    UFUNCTION(BlueprintPure, Category = "Inventory")
    int32 GetItemCount(FName ItemID) const;

    // -----------------------------------------------------------------------
    //  Slot interaction
    // -----------------------------------------------------------------------

    /**
     * Swap (or merge stacks in) two slots across any inventory containers.
     * Container types: MainGrid, Hotbar, CraftingInput, CraftingOutput.
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory")
    void SwapSlots(EInventoryContainer FromContainer, int32 FromIndex,
                   EInventoryContainer ToContainer, int32 ToIndex);

    /**
     * Split a stack – move half (round-down) from the source slot into the
     * first available empty slot.  No-op if the stack is size 1.
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory")
    void SplitStack(bool bFromHotbar, int32 SlotIndex);

    // -----------------------------------------------------------------------
    //  Hotbar
    // -----------------------------------------------------------------------

    /** Set the active hotbar slot (clamped to 0..HotbarSize-1). */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Hotbar")
    void SetActiveHotbarIndex(int32 Index);

    /** Returns the item currently held in the active hotbar slot. */
    UFUNCTION(BlueprintPure, Category = "Inventory|Hotbar")
    FInventorySlot GetActiveHotbarSlot() const;

    // -----------------------------------------------------------------------
    //  Equipment
    // -----------------------------------------------------------------------

    /**
     * Equip the item in a grid or hotbar slot into its natural equipment slot.
     * If something is already equipped there, the two slots are swapped.
     * @return false if the item is not equippable (wrong type or slot mismatch).
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Equipment")
    bool EquipFromSlot(bool bFromHotbar, int32 SlotIndex);

    /**
     * Move the item in an equipment slot back into the first free grid/hotbar slot.
     * @return false if no room was found.
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Equipment")
    bool UnequipToInventory(EEquipmentSlot Slot);

    // -----------------------------------------------------------------------
    //  Data lookup
    // -----------------------------------------------------------------------

    /**
     * Look up an item's definition in the DataTable.
     * @return Pointer to the row, or nullptr if ItemID is not found.
     * Caller must not cache this pointer across GC frames if the table is
     * reloaded.
     * Note: not exposed to Blueprint – raw struct pointers cannot be used
     * as UFUNCTION return values.
     */
    FItemData* GetItemData(FName ItemID) const;

    // -----------------------------------------------------------------------
    //  Utility
    // -----------------------------------------------------------------------

    /** Clear all slots (grid, hotbar, equipment). */
    UFUNCTION(BlueprintCallable, Category = "Inventory")
    void ClearAll();

    /** Broadcast OnInventoryChanged. */
    void NotifyInventoryChanged();

protected:
    virtual void BeginPlay() override;

    /** Crafting system instance (auto-created during initialization). */
    UPROPERTY()
    TObjectPtr<UCraftingSystem> CraftingSystem;

private:
    /** Internal helper: add as many items as possible from Count into Slots. */
    int32 AddToSlotArray(TArray<FInventorySlot>& Slots, FName ItemID,
                         int32 Count, int32 MaxStack);

    /** Internal helper: remove up to Count items from Slots. Returns remainder. */
    int32 RemoveFromSlotArray(TArray<FInventorySlot>& Slots, FName ItemID, int32 Count);
};
