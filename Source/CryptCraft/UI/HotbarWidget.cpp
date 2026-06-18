#include "UI/HotbarWidget.h"

#include "UI/InventorySlotWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Engine/Engine.h"

// ---------------------------------------------------------------------------
//  NativeConstruct / NativeDestruct
// ---------------------------------------------------------------------------

void UHotbarWidget::NativeConstruct()
{
    Super::NativeConstruct();

    UE_LOG(LogTemp, Log, TEXT("[HotbarWidget] NativeConstruct — HotbarBox: %s | SlotWidgetClass: %s"),
        HotbarBox       ? TEXT("FOUND")  : TEXT("MISSING"),
        SlotWidgetClass ? *SlotWidgetClass->GetName() : TEXT("NOT SET"));

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 8.f,
            (HotbarBox && SlotWidgetClass) ? FColor::Green : FColor::Red,
            FString::Printf(TEXT("[HotbarWidget] HotbarBox:%s SlotClass:%s"),
                HotbarBox       ? TEXT("OK")    : TEXT("MISSING"),
                SlotWidgetClass ? TEXT("OK")    : TEXT("NOT SET")));
    }
}

void UHotbarWidget::NativeDestruct()
{
    if (InventoryRef)
    {
        InventoryRef->OnInventoryChanged.RemoveDynamic(
            this, &UHotbarWidget::HandleInventoryChanged);
        InventoryRef->OnHotbarSelectionChanged.RemoveDynamic(
            this, &UHotbarWidget::HandleHotbarSelectionChanged);
    }
    Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
//  Init
// ---------------------------------------------------------------------------

void UHotbarWidget::Init(UInventoryComponent* Inventory)
{
    if (!Inventory)
    {
        UE_LOG(LogTemp, Warning, TEXT("[HotbarWidget] Init called with null InventoryComponent."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[HotbarWidget] Init — inventory found on '%s'"), *Inventory->GetOwner()->GetName());
    InventoryRef = Inventory;

    InventoryRef->OnInventoryChanged.AddDynamic(
        this, &UHotbarWidget::HandleInventoryChanged);
    InventoryRef->OnHotbarSelectionChanged.AddDynamic(
        this, &UHotbarWidget::HandleHotbarSelectionChanged);

    BuildSlots();
    RefreshHotbar();
    RefreshSelection(InventoryRef->ActiveHotbarIndex);
}

// ---------------------------------------------------------------------------
//  BuildSlots
// ---------------------------------------------------------------------------

void UHotbarWidget::BuildSlots()
{
    if (!HotbarBox)
    {
        UE_LOG(LogTemp, Warning, TEXT("[HotbarWidget] BuildSlots — HotbarBox is null. Is a UHorizontalBox named 'HotbarBox' in the Blueprint?"));
        return;
    }
    if (!SlotWidgetClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[HotbarWidget] BuildSlots — SlotWidgetClass is not set. Assign WBP_HotbarSlot in the Blueprint defaults."));
        return;
    }

    HotbarBox->ClearChildren();
    SlotWidgets.Empty();

    int32 CreatedCount = 0;
    for (int32 i = 0; i < UInventoryComponent::HotbarSize; ++i)
    {
        UInventorySlotWidget* SlotWidget =
            CreateWidget<UInventorySlotWidget>(this, SlotWidgetClass);
        if (!SlotWidget) continue;

        SlotWidget->SlotIndex = i;

        if (UHorizontalBoxSlot* BoxSlot = HotbarBox->AddChildToHorizontalBox(SlotWidget))
        {
            BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            BoxSlot->SetHorizontalAlignment(HAlign_Fill);
            BoxSlot->SetVerticalAlignment(VAlign_Fill);
        }

        SlotWidgets.Add(TObjectPtr<UInventorySlotWidget>(SlotWidget));
        ++CreatedCount;
    }

    UE_LOG(LogTemp, Log, TEXT("[HotbarWidget] BuildSlots — created %d / %d slots."), CreatedCount, UInventoryComponent::HotbarSize);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 6.f,
            CreatedCount == UInventoryComponent::HotbarSize ? FColor::Green : FColor::Orange,
            FString::Printf(TEXT("[HotbarWidget] Hotbar slots: %d / %d"), CreatedCount, UInventoryComponent::HotbarSize));
    }
}

// ---------------------------------------------------------------------------
//  RefreshHotbar
// ---------------------------------------------------------------------------

void UHotbarWidget::RefreshHotbar()
{
    if (!InventoryRef) return;

    for (int32 i = 0; i < SlotWidgets.Num(); ++i)
    {
        UInventorySlotWidget* SlotWidget = SlotWidgets[i];
        if (!SlotWidget) continue;

        const FInventorySlot& SlotData = InventoryRef->Hotbar[i];
        FItemData* Data = InventoryRef->GetItemData(SlotData.ItemID);
        SlotWidget->RefreshSlot(SlotData, Data);
    }

    RefreshSelection(InventoryRef->ActiveHotbarIndex);
}

// ---------------------------------------------------------------------------
//  RefreshSelection
// ---------------------------------------------------------------------------

void UHotbarWidget::RefreshSelection(int32 NewIndex)
{
    for (int32 i = 0; i < SlotWidgets.Num(); ++i)
    {
        if (SlotWidgets[i])
        {
            SlotWidgets[i]->SetSelected(i == NewIndex);
        }
    }
}

// ---------------------------------------------------------------------------
//  Delegate handlers
// ---------------------------------------------------------------------------

void UHotbarWidget::HandleInventoryChanged()
{
    RefreshHotbar();
}

void UHotbarWidget::HandleHotbarSelectionChanged(int32 NewIndex)
{
    RefreshSelection(NewIndex);
}
