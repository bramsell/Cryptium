// InventoryWidget.h
// Full inventory screen: 10×5 main grid, 10-slot hotbar section, equipment panel.
// Blueprint child: WBP_Inventory
// Required bound widgets:
//   UUniformGridPanel  "MainGridPanel"
//   UHorizontalBox     "HotbarBox"
// Optional for top-half swapping:
//   UWidgetSwitcher    "TopHalfSwitcher"      (index 0 = primary, 1 = secondary)
//   UButton            "ToggleTopHalfButton"  (switches between primary/secondary)
//   UVerticalBox       "MainArmorBox"         (Helmet/Chestplate/Leggings/Boots)
//   UVerticalBox       "SecondaryEquipmentBox"(Back/Wings/Necklace/Hands/Charms/Rings)
// Backward-compatible fallback:
//   UVerticalBox       "EquipmentBox"         (single list for all slots)

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Items/ItemData.h"
#include "Inventory/InventoryComponent.h"
#include "InventoryWidget.generated.h"

class UInventorySlotWidget;
class UUniformGridPanel;
class UHorizontalBox;
class UVerticalBox;
class UWidgetSwitcher;
class UButton;
class UTextBlock;

UCLASS()
class CRYPTCRAFT_API UInventoryWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // -----------------------------------------------------------------------
    //  API
    // -----------------------------------------------------------------------

    /** Wire up to the character's UInventoryComponent. Call once after opening. */
    UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
    void Init(UInventoryComponent* Inventory);

    /** Refresh every section (grid, hotbar strip, equipment). */
    UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
    void RefreshAll();

    // -----------------------------------------------------------------------
    //  Designer settings
    // -----------------------------------------------------------------------

    /** Widget class for the 50 main-grid and hotbar-strip slots (WBP_InventorySlot). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|UI")
    TSubclassOf<UInventorySlotWidget> SlotWidgetClass;

    /** Widget class for the 9 equipment slots (can reuse WBP_InventorySlot). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|UI")
    TSubclassOf<UInventorySlotWidget> EquipSlotWidgetClass;

protected:
    // -----------------------------------------------------------------------
    //  Bound widgets  (BindWidget = must exist in Blueprint with exact name)
    // -----------------------------------------------------------------------

    /** 10-column uniform grid, 5 rows → 50 slots. */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UUniformGridPanel> MainGridPanel;

    /** Row of 10 hotbar slots inside the inventory screen. */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UHorizontalBox> HotbarBox;

    /** Optional single-box equipment layout (legacy fallback). */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UVerticalBox> EquipmentBox;

    /** Optional top-half switcher (0=primary, 1=secondary). */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidgetSwitcher> TopHalfSwitcher;

    /** Optional button that toggles TopHalfSwitcher between 0 and 1. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> ToggleTopHalfButton;

    /** Optional primary panel box for the 4 core armor slots. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UVerticalBox> MainArmorBox;

    /** Optional secondary panel box for all non-core equipment slots. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UVerticalBox> SecondaryEquipmentBox;

    virtual void NativeConstruct() override;
    virtual void NativeDestruct()  override;

private:
    UPROPERTY()
    TObjectPtr<UInventoryComponent> InventoryRef;

    // Slot widget caches
    UPROPERTY()
    TArray<TObjectPtr<UInventorySlotWidget>> GridSlotWidgets;

    UPROPERTY()
    TArray<TObjectPtr<UInventorySlotWidget>> HotbarSlotWidgets;

    UPROPERTY()
    TMap<EEquipmentSlot, TObjectPtr<UInventorySlotWidget>> EquipSlotWidgets;

    // Build helpers (run once)
    void BuildGridSlots();
    void BuildHotbarSlots();
    void BuildEquipmentSlots();
    void ApplyTopHalfView();

    /** Returns true for the always-visible armor quartet. */
    bool IsPrimaryArmorSlot(EEquipmentSlot InEquipSlot) const;

    /** Returns which vertical box should host a given equipment slot. */
    UVerticalBox* GetEquipmentContainerForSlot(EEquipmentSlot InEquipSlot) const;

    // Refresh helpers
    void RefreshGrid();
    void RefreshHotbarSection();
    void RefreshEquipment();

    // Delegate handlers
    UFUNCTION() void HandleInventoryChanged();
    UFUNCTION() void HandleEquipmentChanged(EEquipmentSlot ChangedSlot);
    UFUNCTION() void HandleHotbarSelectionChanged(int32 NewIndex);
    UFUNCTION() void HandleToggleTopHalfClicked();

    /** False = primary view (main armor/crafting), true = secondary gear view. */
    bool bShowSecondaryTopHalf = false;
};
