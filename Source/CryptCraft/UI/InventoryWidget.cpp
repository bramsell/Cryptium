#include "UI/InventoryWidget.h"

#include "UI/InventorySlotWidget.h"
#include "UI/InventoryDragDropOperation.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Button.h"
#include "Engine/Engine.h"

// ---------------------------------------------------------------------------
//  NativeConstruct / NativeDestruct
// ---------------------------------------------------------------------------

void UInventoryWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // --- Verify bound widgets ---
    UE_LOG(LogTemp, Log, TEXT("[InventoryWidget] NativeConstruct — MainGridPanel: %s | HotbarBox: %s | EquipmentBox: %s | MainArmorBox: %s | SecondaryEquipmentBox: %s | TopHalfSwitcher: %s | ToggleTopHalfButton: %s"),
        MainGridPanel  ? TEXT("FOUND") : TEXT("MISSING"),
        HotbarBox      ? TEXT("FOUND") : TEXT("MISSING"),
        EquipmentBox   ? TEXT("FOUND") : TEXT("MISSING"),
        MainArmorBox   ? TEXT("FOUND") : TEXT("MISSING"),
        SecondaryEquipmentBox ? TEXT("FOUND") : TEXT("MISSING"),
        TopHalfSwitcher ? TEXT("FOUND") : TEXT("MISSING"),
        ToggleTopHalfButton ? TEXT("FOUND") : TEXT("MISSING"));

    UE_LOG(LogTemp, Log, TEXT("[InventoryWidget] SlotWidgetClass: %s | EquipSlotWidgetClass: %s"),
        SlotWidgetClass      ? *SlotWidgetClass->GetName()      : TEXT("NOT SET"),
        EquipSlotWidgetClass ? *EquipSlotWidgetClass->GetName() : TEXT("NOT SET (will fall back to SlotWidgetClass)"));

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 8.f,
            (MainGridPanel && HotbarBox && (EquipmentBox || (MainArmorBox && SecondaryEquipmentBox))) ? FColor::Green : FColor::Red,
            FString::Printf(TEXT("[InventoryWidget] Panels — Grid:%s Hotbar:%s Equip:%s"),
                MainGridPanel ? TEXT("OK") : TEXT("MISSING"),
                HotbarBox     ? TEXT("OK") : TEXT("MISSING"),
                (EquipmentBox || (MainArmorBox && SecondaryEquipmentBox)) ? TEXT("OK") : TEXT("MISSING")));
    }

    if (ToggleTopHalfButton)
    {
        ToggleTopHalfButton->OnClicked.AddDynamic(this, &UInventoryWidget::HandleToggleTopHalfClicked);
    }

    ApplyTopHalfView();
}

void UInventoryWidget::NativeDestruct()
{
    if (ToggleTopHalfButton)
    {
        ToggleTopHalfButton->OnClicked.RemoveDynamic(this, &UInventoryWidget::HandleToggleTopHalfClicked);
    }

    if (InventoryRef)
    {
        InventoryRef->OnInventoryChanged.RemoveDynamic(
            this, &UInventoryWidget::HandleInventoryChanged);
        InventoryRef->OnEquipmentChanged.RemoveDynamic(
            this, &UInventoryWidget::HandleEquipmentChanged);
        InventoryRef->OnHotbarSelectionChanged.RemoveDynamic(
            this, &UInventoryWidget::HandleHotbarSelectionChanged);
    }
    Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
//  Drag-Drop Handling
// ---------------------------------------------------------------------------

bool UInventoryWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
    Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);

    // Get the mouse position in screen space
    FVector2D MousePos = InDragDropEvent.GetScreenSpacePosition();

    // Check grid slots
    for (int32 i = 0; i < GridSlotWidgets.Num(); ++i)
    {
        if (GridSlotWidgets[i])
        {
            FVector2D SlotPos = GridSlotWidgets[i]->GetTickSpaceGeometry().GetAbsolutePosition();
            FVector2D SlotSize = GridSlotWidgets[i]->GetTickSpaceGeometry().GetLocalSize();
            FSlateRect SlotRect(SlotPos, SlotSize);

            if (SlotRect.ContainsPoint(MousePos))
            {
                CurrentHoveredSlot = GridSlotWidgets[i];
                CurrentHoveredIsHotbar = false;
                CurrentHoveredIndex = i;
                return true;
            }
        }
    }

    // Check hotbar slots
    for (int32 i = 0; i < HotbarSlotWidgets.Num(); ++i)
    {
        if (HotbarSlotWidgets[i])
        {
            FVector2D SlotPos = HotbarSlotWidgets[i]->GetTickSpaceGeometry().GetAbsolutePosition();
            FVector2D SlotSize = HotbarSlotWidgets[i]->GetTickSpaceGeometry().GetLocalSize();
            FSlateRect SlotRect(SlotPos, SlotSize);

            if (SlotRect.ContainsPoint(MousePos))
            {
                CurrentHoveredSlot = HotbarSlotWidgets[i];
                CurrentHoveredIsHotbar = true;
                CurrentHoveredIndex = i;
                return true;
            }
        }
    }

    return false;
}

bool UInventoryWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
    Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);

    if (!InOperation || !InventoryRef)
    {
        return false;
    }

    UInventoryDragDropOperation* InventoryOp = Cast<UInventoryDragDropOperation>(InOperation);
    if (!InventoryOp)
    {
        return false;
    }

    // If no valid slot was hovered, reject the drop
    if (CurrentHoveredIndex < 0)
    {
        return false;
    }

    // Restore visibility of source slot
    if (InventoryOp->SourceSlotWidget.IsValid())
    {
        UInventorySlotWidget* SourceSlot = InventoryOp->SourceSlotWidget.Get();
        if (SourceSlot)
        {
            SourceSlot->RestoreItemVisibility();
        }
    }

    // Perform the swap using the hovered slot that NativeOnDragOver tracked
    InventoryRef->SwapSlots(InventoryOp->bFromHotbar, InventoryOp->FromIndex,
                             CurrentHoveredIsHotbar, CurrentHoveredIndex);

    return true;
}

// ---------------------------------------------------------------------------
//  Init
// ---------------------------------------------------------------------------

void UInventoryWidget::Init(UInventoryComponent* Inventory)
{
    if (!Inventory)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InventoryWidget] Init called with null InventoryComponent — no slots will be built."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[InventoryWidget] Init — inventory found on '%s'"), *Inventory->GetOwner()->GetName());
    InventoryRef = Inventory;

    InventoryRef->OnInventoryChanged.AddDynamic(
        this, &UInventoryWidget::HandleInventoryChanged);
    InventoryRef->OnEquipmentChanged.AddDynamic(
        this, &UInventoryWidget::HandleEquipmentChanged);
    InventoryRef->OnHotbarSelectionChanged.AddDynamic(
        this, &UInventoryWidget::HandleHotbarSelectionChanged);

    BuildGridSlots();
    BuildHotbarSlots();
    BuildEquipmentSlots();
    RefreshAll();
}

// ---------------------------------------------------------------------------
//  Build helpers
// ---------------------------------------------------------------------------

void UInventoryWidget::BuildGridSlots()
{
    if (!MainGridPanel)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InventoryWidget] BuildGridSlots — MainGridPanel is null. Is a UUniformGridPanel named 'MainGridPanel' in the Blueprint?"));
        return;
    }
    if (!SlotWidgetClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InventoryWidget] BuildGridSlots — SlotWidgetClass is not set. Assign WBP_InventorySlot in the Blueprint defaults."));
        return;
    }

    MainGridPanel->ClearChildren();
    GridSlotWidgets.Empty();

    int32 CreatedCount = 0;

    for (int32 i = 0; i < UInventoryComponent::GridSize; ++i)
    {
        UInventorySlotWidget* SlotWidget =
            CreateWidget<UInventorySlotWidget>(this, SlotWidgetClass);
        if (!SlotWidget) continue;

        SlotWidget->SlotIndex = i;
        SlotWidget->bIsHotbarSlot = false;
        SlotWidget->InventoryComponent = InventoryRef;

        const int32 Col = i % UInventoryComponent::GridWidth;
        const int32 Row = i / UInventoryComponent::GridWidth;

        if (UUniformGridSlot* GridSlot = MainGridPanel->AddChildToUniformGrid(SlotWidget, Row, Col))
        {
            GridSlot->SetHorizontalAlignment(HAlign_Fill);
            GridSlot->SetVerticalAlignment(VAlign_Fill);
        }

        GridSlotWidgets.Add(SlotWidget);
        ++CreatedCount;
    }

    UE_LOG(LogTemp, Log, TEXT("[InventoryWidget] BuildGridSlots — created %d / %d slots."), CreatedCount, UInventoryComponent::GridSize);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 6.f,
            CreatedCount == UInventoryComponent::GridSize ? FColor::Green : FColor::Orange,
            FString::Printf(TEXT("[InventoryWidget] Grid slots: %d / %d"), CreatedCount, UInventoryComponent::GridSize));
    }
}

void UInventoryWidget::BuildHotbarSlots()
{
    if (!HotbarBox)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InventoryWidget] BuildHotbarSlots — HotbarBox is null. Is a UHorizontalBox named 'HotbarBox' in the Blueprint?"));
        return;
    }
    if (!SlotWidgetClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InventoryWidget] BuildHotbarSlots — SlotWidgetClass is not set."));
        return;
    }

    HotbarBox->ClearChildren();
    HotbarSlotWidgets.Empty();

    int32 CreatedCount = 0;
    for (int32 i = 0; i < UInventoryComponent::HotbarSize; ++i)
    {
        UInventorySlotWidget* SlotWidget =
            CreateWidget<UInventorySlotWidget>(this, SlotWidgetClass);
        if (!SlotWidget) continue;

        SlotWidget->SlotIndex = i;
        SlotWidget->bIsHotbarSlot = true;
        SlotWidget->InventoryComponent = InventoryRef;

        if (UHorizontalBoxSlot* BoxSlot = HotbarBox->AddChildToHorizontalBox(SlotWidget))
        {
            BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            BoxSlot->SetHorizontalAlignment(HAlign_Fill);
            BoxSlot->SetVerticalAlignment(VAlign_Fill);
        }

        HotbarSlotWidgets.Add(SlotWidget);
        ++CreatedCount;
    }

    UE_LOG(LogTemp, Log, TEXT("[InventoryWidget] BuildHotbarSlots — created %d / %d slots."), CreatedCount, UInventoryComponent::HotbarSize);
}

void UInventoryWidget::BuildEquipmentSlots()
{
    if (!EquipmentBox && !(MainArmorBox && SecondaryEquipmentBox))
    {
        UE_LOG(LogTemp, Warning, TEXT("[InventoryWidget] BuildEquipmentSlots — no equipment container found. Add either 'EquipmentBox' or both 'MainArmorBox' and 'SecondaryEquipmentBox'."));
        return;
    }

    // Fallback: reuse SlotWidgetClass if no dedicated equip class is set
    TSubclassOf<UInventorySlotWidget> EffectiveClass =
        EquipSlotWidgetClass ? EquipSlotWidgetClass : SlotWidgetClass;

    if (!EffectiveClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InventoryWidget] BuildEquipmentSlots — neither EquipSlotWidgetClass nor SlotWidgetClass is set."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[InventoryWidget] BuildEquipmentSlots — using class '%s'"), *EffectiveClass->GetName());

    if (EquipmentBox)
    {
        EquipmentBox->ClearChildren();
    }
    if (MainArmorBox)
    {
        MainArmorBox->ClearChildren();
    }
    if (SecondaryEquipmentBox)
    {
        SecondaryEquipmentBox->ClearChildren();
    }
    EquipSlotWidgets.Empty();

    int32 CreatedCount = 0;

    // Build in display order - only main armor slots
    const TArray<EEquipmentSlot> SlotOrder = {
        EEquipmentSlot::Helmet,
        EEquipmentSlot::Chestplate,
        EEquipmentSlot::Leggings,
        EEquipmentSlot::Boots,
    };

    for (EEquipmentSlot EquipSlot : SlotOrder)
    {
        UInventorySlotWidget* SlotWidget =
            CreateWidget<UInventorySlotWidget>(this, EffectiveClass);
        if (!SlotWidget) continue;

        SlotWidget->SlotIndex = static_cast<int32>(EquipSlot);
        SlotWidget->bIsHotbarSlot = false;
        SlotWidget->InventoryComponent = InventoryRef;

        UVerticalBox* TargetBox = GetEquipmentContainerForSlot(EquipSlot);
        if (!TargetBox)
        {
            continue;
        }

        if (UVerticalBoxSlot* VBoxSlot = TargetBox->AddChildToVerticalBox(SlotWidget))
        {
            VBoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            VBoxSlot->SetHorizontalAlignment(HAlign_Fill);
        }

        EquipSlotWidgets.Add(EquipSlot, SlotWidget);
        ++CreatedCount;
    }

    const int32 ExpectedCount = SlotOrder.Num();
    UE_LOG(LogTemp, Log, TEXT("[InventoryWidget] BuildEquipmentSlots — created %d / %d slots."), CreatedCount, ExpectedCount);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 6.f,
            CreatedCount == ExpectedCount ? FColor::Green : FColor::Orange,
            FString::Printf(TEXT("[InventoryWidget] Equip slots: %d / %d"), CreatedCount, ExpectedCount));
    }
}

void UInventoryWidget::ApplyTopHalfView()
{
    if (!TopHalfSwitcher)
    {
        return;
    }

    TopHalfSwitcher->SetActiveWidgetIndex(bShowSecondaryTopHalf ? 1 : 0);
}

bool UInventoryWidget::IsPrimaryArmorSlot(EEquipmentSlot InEquipSlot) const
{
    return InEquipSlot == EEquipmentSlot::Helmet
        || InEquipSlot == EEquipmentSlot::Chestplate
        || InEquipSlot == EEquipmentSlot::Leggings
        || InEquipSlot == EEquipmentSlot::Boots;
}

UVerticalBox* UInventoryWidget::GetEquipmentContainerForSlot(EEquipmentSlot InEquipSlot) const
{
    if (MainArmorBox && SecondaryEquipmentBox)
    {
        return IsPrimaryArmorSlot(InEquipSlot) ? MainArmorBox.Get() : SecondaryEquipmentBox.Get();
    }

    // Backward-compatible single panel layout.
    return EquipmentBox.Get();
}

// ---------------------------------------------------------------------------
//  Refresh
// ---------------------------------------------------------------------------

void UInventoryWidget::RefreshAll()
{
    RefreshGrid();
    RefreshHotbarSection();
    RefreshEquipment();
}

void UInventoryWidget::RefreshGrid()
{
    if (!InventoryRef) return;

    for (int32 i = 0; i < GridSlotWidgets.Num(); ++i)
    {
        UInventorySlotWidget* SlotWidget = GridSlotWidgets[i];
        if (!SlotWidget) continue;

        const FInventorySlot& SlotData = InventoryRef->MainGrid[i];
        FItemData* Data = InventoryRef->GetItemData(SlotData.ItemID);
        SlotWidget->RefreshSlot(SlotData, Data);
    }
}

void UInventoryWidget::RefreshHotbarSection()
{
    if (!InventoryRef) return;

    for (int32 i = 0; i < HotbarSlotWidgets.Num(); ++i)
    {
        UInventorySlotWidget* SlotWidget = HotbarSlotWidgets[i];
        if (!SlotWidget) continue;

        const FInventorySlot& SlotData = InventoryRef->Hotbar[i];
        FItemData* Data = InventoryRef->GetItemData(SlotData.ItemID);
        SlotWidget->RefreshSlot(SlotData, Data);
        SlotWidget->SetSelected(i == InventoryRef->ActiveHotbarIndex);
    }
}

void UInventoryWidget::RefreshEquipment()
{
    if (!InventoryRef) return;

    for (auto& Pair : EquipSlotWidgets)
    {
        UInventorySlotWidget* SlotWidget = Pair.Value;
        if (!SlotWidget) continue;

        const FInventorySlot* SlotData = InventoryRef->EquipmentSlots.Find(Pair.Key);
        if (SlotData)
        {
            FItemData* Data = InventoryRef->GetItemData(SlotData->ItemID);
            SlotWidget->RefreshSlot(*SlotData, Data);
        }
        else
        {
            SlotWidget->RefreshSlot(FInventorySlot(), nullptr);
        }
    }
}

// ---------------------------------------------------------------------------
//  Delegate handlers
// ---------------------------------------------------------------------------

void UInventoryWidget::HandleInventoryChanged()
{
    RefreshAll();
}

void UInventoryWidget::HandleEquipmentChanged(EEquipmentSlot ChangedSlot)
{
    // Targeted refresh – only re-draw the changed equipment slot
    TObjectPtr<UInventorySlotWidget>* WidgetPtr = EquipSlotWidgets.Find(ChangedSlot);
    if (WidgetPtr && WidgetPtr->Get() && InventoryRef)
    {
        const FInventorySlot* SlotData = InventoryRef->EquipmentSlots.Find(ChangedSlot);
        FItemData* Data = SlotData ? InventoryRef->GetItemData(SlotData->ItemID) : nullptr;
        const FInventorySlot Empty;
        WidgetPtr->Get()->RefreshSlot(SlotData ? *SlotData : Empty, Data);
    }
}

void UInventoryWidget::HandleHotbarSelectionChanged(int32 NewIndex)
{
    // Only update the selection highlight on hotbar strip slots
    for (int32 i = 0; i < HotbarSlotWidgets.Num(); ++i)
    {
        if (HotbarSlotWidgets[i])
        {
            HotbarSlotWidgets[i]->SetSelected(i == NewIndex);
        }
    }
}

void UInventoryWidget::HandleToggleTopHalfClicked()
{
    bShowSecondaryTopHalf = !bShowSecondaryTopHalf;
    ApplyTopHalfView();
}
