// InventorySlotWidget.h
// Base C++ class for a single inventory/hotbar slot widget.
// Create a Blueprint child (WBP_InventorySlot, WBP_HotbarSlot) and add the
// UImage and UTextBlock widgets listed below with the exact same names.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Items/ItemData.h"
#include "Inventory/InventoryComponent.h"
#include "InventorySlotWidget.generated.h"

class UImage;
class UTextBlock;
class UBorder;
class UInventoryDragDropOperation;

UCLASS()
class CRYPTCRAFT_API UInventorySlotWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // -----------------------------------------------------------------------
    //  API called by parent widgets
    // -----------------------------------------------------------------------

    /**
     * Refresh visuals from a slot + its item definition.
     * Pass nullptr for Data to render the slot as empty.
     * Note: not a UFUNCTION — raw USTRUCT pointers cannot be exposed to Blueprint.
     */
    void RefreshSlot(const FInventorySlot& InSlot, const FItemData* Data);

    /** Highlight this slot (used by the hotbar to show the active slot). */
    UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
    void SetSelected(bool bSelected);

    /** Index of this slot inside its parent container. */
    UPROPERTY(BlueprintReadWrite, Category = "Inventory|UI")
    int32 SlotIndex = 0;

    /** Whether this slot is in the hotbar (true) or main grid (false). */
    UPROPERTY(BlueprintReadWrite, Category = "Inventory|UI")
    bool bIsHotbarSlot = false;

    /** Reference to the inventory component for drag-drop operations. */
    UPROPERTY(BlueprintReadWrite, Category = "Inventory|UI")
    TObjectPtr<UInventoryComponent> InventoryComponent;

    /** Restore item visibility (used after drag ends). */
    UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
    void RestoreItemVisibility();

protected:
    // -----------------------------------------------------------------------
    //  UUserWidget overrides for drag-drop
    // -----------------------------------------------------------------------

    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
    virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
    virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
    virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

    // -----------------------------------------------------------------------
    //  Bound widgets  (BindWidgetOptional → no compile error if absent in BP,
    //  but the feature simply won't render)
    // -----------------------------------------------------------------------

    /** Item icon image. Name in Blueprint must be exactly "ItemIcon". */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> ItemIcon;

    /** Stack-count label. Name in Blueprint must be exactly "StackCountText". */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> StackCountText;

    /**
     * Border whose brush colour is swapped to show selection.
     * Name in Blueprint must be exactly "SelectionBorder".
     */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UBorder> SelectionBorder;

private:
    /** Cache of the current slot data for drag operations. */
    FInventorySlot CachedSlotData;
    FItemData* CachedItemData = nullptr;
};
