#include "Inventory/InventoryComponent.h"
#include "Crafting/CraftingSystem.h"

// ---------------------------------------------------------------------------
//  Construction / Lifecycle
// ---------------------------------------------------------------------------

UInventoryComponent::UInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    // Pre-allocate all slots so every index is always valid.
    MainGrid.SetNum(GridSize);
    Hotbar.SetNum(HotbarSize);
    CraftingInputSlots.SetNum(CraftingInputSize);

    // Equipment slots will be populated on-demand when items are equipped.
    // Starting with an empty TMap avoids reflection system issues during destruction.
}

void UInventoryComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!ItemDataTable)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UInventoryComponent on '%s': No ItemDataTable assigned – item lookups will fail."),
            *GetOwner()->GetName());
    }

    // Initialize crafting system
    if (!CraftingSystem)
    {
        CraftingSystem = NewObject<UCraftingSystem>(this);
        if (CraftingSystem)
        {
            CraftingSystem->Initialize(this);
        }
    }
}

// ---------------------------------------------------------------------------
//  Core API
// ---------------------------------------------------------------------------

int32 UInventoryComponent::AddItem(FName ItemID, int32 Count)
{
    if (ItemID.IsNone() || Count <= 0) return Count;

    UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent::AddItem] START: ItemID=%s, Count=%d"), *ItemID.ToString(), Count);

    // Determine max stack size from the DataTable (default 64 if not found).
    int32 MaxStack = 64;
    if (FItemData* Data = GetItemData(ItemID))
    {
        MaxStack = FMath::Max(1, Data->MaxStackSize);
        UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent::AddItem] Found item data, MaxStack=%d"), MaxStack);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent::AddItem] ItemID not found in DataTable!"));
    }

    int32 Remaining = Count;

    // Pass 1: Search BOTH hotbar and grid for existing partial stacks.
    // Check hotbar first (maintains priority), then grid.
    for (FInventorySlot& Slot : Hotbar)
    {
        if (Remaining <= 0) break;
        if (Slot.ItemID == ItemID && Slot.StackCount > 0 && Slot.StackCount < MaxStack)
        {
            const int32 Room = MaxStack - Slot.StackCount;
            const int32 Transfer = FMath::Min(Room, Remaining);
            Slot.StackCount += Transfer;
            Remaining -= Transfer;
        }
    }

    for (FInventorySlot& Slot : MainGrid)
    {
        if (Remaining <= 0) break;
        if (Slot.ItemID == ItemID && Slot.StackCount > 0 && Slot.StackCount < MaxStack)
        {
            const int32 Room = MaxStack - Slot.StackCount;
            const int32 Transfer = FMath::Min(Room, Remaining);
            Slot.StackCount += Transfer;
            Remaining -= Transfer;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent::AddItem] After Pass 1 (partial stacks): Remaining=%d"), Remaining);

    // Pass 2: Fill empty hotbar slots (prioritize hotbar for new stacks).
    Remaining = AddToSlotArray(Hotbar, ItemID, Remaining, MaxStack);

    UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent::AddItem] After Pass 2 (hotbar empties): Remaining=%d"), Remaining);

    // Pass 3: Fill empty main grid slots.
    Remaining = AddToSlotArray(MainGrid, ItemID, Remaining, MaxStack);

    UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent::AddItem] After Pass 3 (grid empties): Remaining=%d"), Remaining);

    if (Remaining < Count)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent::AddItem] SUCCESS - NotifyInventoryChanged called. Items added: %d"), Count - Remaining);
        NotifyInventoryChanged();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent::AddItem] FAILED - No items were added!"));
    }

    return Remaining;
}

bool UInventoryComponent::RemoveItem(FName ItemID, int32 Count)
{
    if (ItemID.IsNone() || Count <= 0) return true;

    // Dry-run: make sure we actually have enough before mutating anything.
    if (GetItemCount(ItemID) < Count) return false;

    int32 Remaining = Count;
    Remaining = RemoveFromSlotArray(MainGrid, ItemID, Remaining);
    Remaining = RemoveFromSlotArray(Hotbar,   ItemID, Remaining);

    NotifyInventoryChanged();
    return (Remaining == 0);
}

int32 UInventoryComponent::GetItemCount(FName ItemID) const
{
    if (ItemID.IsNone()) return 0;

    int32 Total = 0;
    for (const FInventorySlot& Slot : MainGrid)
    {
        if (Slot.ItemID == ItemID) Total += Slot.StackCount;
    }
    for (const FInventorySlot& Slot : Hotbar)
    {
        if (Slot.ItemID == ItemID) Total += Slot.StackCount;
    }
    return Total;
}

// ---------------------------------------------------------------------------
//  Slot interaction
// ---------------------------------------------------------------------------

void UInventoryComponent::SwapSlots(EInventoryContainer FromContainer, int32 FromIndex,
                                     EInventoryContainer ToContainer, int32 ToIndex)
{
    // Helper to get the slot array for a container
    auto GetSlotArray = [this](EInventoryContainer Container) -> TArray<FInventorySlot>*
    {
        switch (Container)
        {
            case EInventoryContainer::MainGrid:       return &MainGrid;
            case EInventoryContainer::Hotbar:         return &Hotbar;
            case EInventoryContainer::CraftingInput:  return &CraftingInputSlots;
            case EInventoryContainer::CraftingOutput: return nullptr;  // Single slot, handled specially
            default:                                  return nullptr;
        }
    };

    // Helper to get a slot from any container (handles single-slot CraftingOutput)
    auto GetSlot = [this, &GetSlotArray](EInventoryContainer Container, int32 Index) -> FInventorySlot*
    {
        if (Container == EInventoryContainer::CraftingOutput)
        {
            return Index == 0 ? &CraftingOutputSlot : nullptr;
        }
        TArray<FInventorySlot>* Arr = GetSlotArray(Container);
        return (Arr && Arr->IsValidIndex(Index)) ? &(*Arr)[Index] : nullptr;
    };

    FInventorySlot* From = GetSlot(FromContainer, FromIndex);
    FInventorySlot* To   = GetSlot(ToContainer, ToIndex);

    if (!From || !To) return;

    // Merge stacks if same item and destination has room.
    if (!From->IsEmpty() && !To->IsEmpty() && From->ItemID == To->ItemID)
    {
        int32 MaxStack = 64;
        if (FItemData* Data = GetItemData(From->ItemID))
        {
            MaxStack = FMath::Max(1, Data->MaxStackSize);
        }

        const int32 Room = MaxStack - To->StackCount;
        if (Room > 0)
        {
            const int32 Transfer = FMath::Min(Room, From->StackCount);
            To->StackCount   += Transfer;
            From->StackCount -= Transfer;
            if (From->StackCount <= 0) From->Clear();
            NotifyInventoryChanged();
            return;
        }
    }

    // Otherwise: simple swap.
    Swap(*From, *To);
    NotifyInventoryChanged();
}

void UInventoryComponent::SplitStack(bool bFromHotbar, int32 SlotIndex)
{
    TArray<FInventorySlot>& Arr = bFromHotbar ? Hotbar : MainGrid;
    if (!Arr.IsValidIndex(SlotIndex)) return;

    FInventorySlot& Src = Arr[SlotIndex];
    if (Src.IsEmpty() || Src.StackCount <= 1) return;

    const int32 SplitAmount = Src.StackCount / 2;

    // Find the first empty slot (hotbar first, then grid).
    auto TryPlace = [&](TArray<FInventorySlot>& Slots) -> bool
    {
        for (FInventorySlot& Slot : Slots)
        {
            if (Slot.IsEmpty())
            {
                Slot.ItemID     = Src.ItemID;
                Slot.StackCount = SplitAmount;
                Src.StackCount -= SplitAmount;
                NotifyInventoryChanged();
                return true;
            }
        }
        return false;
    };

    if (!TryPlace(Hotbar)) TryPlace(MainGrid);
}

// ---------------------------------------------------------------------------
//  Hotbar
// ---------------------------------------------------------------------------

void UInventoryComponent::SetActiveHotbarIndex(int32 Index)
{
    const int32 Clamped = FMath::Clamp(Index, 0, HotbarSize - 1);
    if (Clamped != ActiveHotbarIndex)
    {
        ActiveHotbarIndex = Clamped;
        OnHotbarSelectionChanged.Broadcast(ActiveHotbarIndex);
    }
}

FInventorySlot UInventoryComponent::GetActiveHotbarSlot() const
{
    if (Hotbar.IsValidIndex(ActiveHotbarIndex))
    {
        return Hotbar[ActiveHotbarIndex];
    }
    return FInventorySlot();
}

// ---------------------------------------------------------------------------
//  Equipment
// ---------------------------------------------------------------------------

bool UInventoryComponent::EquipFromSlot(bool bFromHotbar, int32 SlotIndex)
{
    TArray<FInventorySlot>& Arr = bFromHotbar ? Hotbar : MainGrid;
    if (!Arr.IsValidIndex(SlotIndex)) return false;

    FInventorySlot& InvSlot = Arr[SlotIndex];
    if (InvSlot.IsEmpty()) return false;

    FItemData* Data = GetItemData(InvSlot.ItemID);
    if (!Data) return false;

    // Only Armor, Charm, and Ring items are equippable via this path.
    const bool bEquippable = (Data->ItemType == EItemType::Armor  ||
                              Data->ItemType == EItemType::Charm  ||
                              Data->ItemType == EItemType::Ring);
    if (!bEquippable) return false;

    const EEquipmentSlot TargetSlot = Data->EquipSlot;
    FInventorySlot& EquipSlot = EquipmentSlots[TargetSlot];

    // Swap inventory slot ↔ equipment slot.
    Swap(InvSlot, EquipSlot);

    NotifyInventoryChanged();
    OnEquipmentChanged.Broadcast(TargetSlot);
    return true;
}

bool UInventoryComponent::UnequipToInventory(EEquipmentSlot Slot)
{
    FInventorySlot& EquipSlot = EquipmentSlots[Slot];
    if (EquipSlot.IsEmpty()) return false;

    // Try hotbar first, then grid.
    auto TryUnequip = [&](TArray<FInventorySlot>& Slots) -> bool
    {
        for (FInventorySlot& S : Slots)
        {
            if (S.IsEmpty())
            {
                S = EquipSlot;
                EquipSlot.Clear();
                NotifyInventoryChanged();
                OnEquipmentChanged.Broadcast(Slot);
                return true;
            }
        }
        return false;
    };

    return TryUnequip(Hotbar) || TryUnequip(MainGrid);
}

// ---------------------------------------------------------------------------
//  Data lookup
// ---------------------------------------------------------------------------

FItemData* UInventoryComponent::GetItemData(FName ItemID) const
{
    if (!ItemDataTable || ItemID.IsNone()) return nullptr;
    return ItemDataTable->FindRow<FItemData>(ItemID, TEXT("UInventoryComponent::GetItemData"));
}

// ---------------------------------------------------------------------------
//  Utility
// ---------------------------------------------------------------------------

void UInventoryComponent::ClearAll()
{
    for (FInventorySlot& S : MainGrid)  S.Clear();
    for (FInventorySlot& S : Hotbar)    S.Clear();
    for (auto& Pair : EquipmentSlots)   Pair.Value.Clear();
    NotifyInventoryChanged();
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------

int32 UInventoryComponent::AddToSlotArray(TArray<FInventorySlot>& Slots,
                                           FName ItemID, int32 Count,
                                           int32 MaxStack)
{
    if (Count <= 0) return 0;

    // Pass 1: top up existing partial stacks.
    for (FInventorySlot& Slot : Slots)
    {
        if (Count <= 0) break;
        if (Slot.ItemID == ItemID && Slot.StackCount < MaxStack)
        {
            const int32 Room     = MaxStack - Slot.StackCount;
            const int32 Transfer = FMath::Min(Room, Count);
            Slot.StackCount += Transfer;
            Count           -= Transfer;
        }
    }

    // Pass 2: fill empty slots.
    for (FInventorySlot& Slot : Slots)
    {
        if (Count <= 0) break;
        if (Slot.IsEmpty())
        {
            const int32 Transfer = FMath::Min(MaxStack, Count);
            Slot.ItemID     = ItemID;
            Slot.StackCount = Transfer;
            Count           -= Transfer;
        }
    }

    return Count;  // Leftover that didn't fit.
}

int32 UInventoryComponent::RemoveFromSlotArray(TArray<FInventorySlot>& Slots,
                                                FName ItemID, int32 Count)
{
    if (Count <= 0) return 0;

    // Remove from smallest stacks first to minimise fragmentation.
    // Simple approach: drain from the end of the array so hotbar items go last.
    for (int32 i = Slots.Num() - 1; i >= 0 && Count > 0; --i)
    {
        FInventorySlot& Slot = Slots[i];
        if (Slot.ItemID == ItemID && Slot.StackCount > 0)
        {
            const int32 Take = FMath::Min(Slot.StackCount, Count);
            Slot.StackCount -= Take;
            Count           -= Take;
            if (Slot.StackCount <= 0) Slot.Clear();
        }
    }

    return Count;
}

void UInventoryComponent::NotifyInventoryChanged()
{
    OnInventoryChanged.Broadcast();
}
