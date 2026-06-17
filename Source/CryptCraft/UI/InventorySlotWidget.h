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

protected:
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
};
