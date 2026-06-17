// HotbarWidget.h
// Persistent hotbar always visible at the bottom of the screen.
// Blueprint child: WBP_Hotbar
// Required bound widget: UHorizontalBox named "HotbarBox"

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/InventoryComponent.h"
#include "HotbarWidget.generated.h"

class UInventorySlotWidget;
class UHorizontalBox;

UCLASS()
class CRYPTCRAFT_API UHotbarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // -----------------------------------------------------------------------
    //  API
    // -----------------------------------------------------------------------

    /**
     * Wire up to the character's UInventoryComponent.
     * Call once after AddToViewport.
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
    void Init(UInventoryComponent* Inventory);

    /** Rebuild all slot visuals (icons + counts). */
    UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
    void RefreshHotbar();

    /** Update selection highlight without re-fetching item data. */
    UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
    void RefreshSelection(int32 NewIndex);

    // -----------------------------------------------------------------------
    //  Designer settings
    // -----------------------------------------------------------------------

    /**
     * Widget class instantiated for each of the 10 hotbar slots.
     * Set to WBP_HotbarSlot (or WBP_InventorySlot) in the Blueprint details.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|UI")
    TSubclassOf<UInventorySlotWidget> SlotWidgetClass;

protected:
    // BindWidget: the Blueprint MUST contain a UHorizontalBox named "HotbarBox"
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UHorizontalBox> HotbarBox;

    virtual void NativeConstruct() override;
    virtual void NativeDestruct()  override;

private:
    UPROPERTY()
    TObjectPtr<UInventoryComponent> InventoryRef;

    UPROPERTY()
    TArray<TObjectPtr<UInventorySlotWidget>> SlotWidgets;

    /** Create slot widgets and add them to HotbarBox. */
    void BuildSlots();

    UFUNCTION() void HandleInventoryChanged();
    UFUNCTION() void HandleHotbarSelectionChanged(int32 NewIndex);
};
