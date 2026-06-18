#include "UI/InventorySlotWidget.h"

#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "UI/InventoryDragDropOperation.h"
#include "Blueprint/DragDropOperation.h"
#include "Engine/Engine.h"
#include "Input/Reply.h"

// ---------------------------------------------------------------------------
//  RefreshSlot
// ---------------------------------------------------------------------------

void UInventorySlotWidget::RefreshSlot(const FInventorySlot& InSlot, const FItemData* Data)
{
    // Cache slot data for drag operations
    CachedSlotData = InSlot;
    CachedItemData = const_cast<FItemData*>(Data);

    const bool bEmpty = InSlot.IsEmpty() || Data == nullptr;

    if (ItemIcon)
    {
        if (!bEmpty && !Data->Icon.IsNull())
        {
            UTexture2D* Tex = Data->Icon.IsValid()
                ? Data->Icon.Get()
                : Data->Icon.LoadSynchronous();

            if (Tex)
            {
                ItemIcon->SetBrushFromTexture(Tex);
            }
            ItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            ItemIcon->SetVisibility(ESlateVisibility::Hidden);
        }
    }

    if (StackCountText)
    {
        if (!bEmpty && InSlot.StackCount > 1)
        {
            StackCountText->SetText(FText::AsNumber(InSlot.StackCount));
            StackCountText->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            StackCountText->SetVisibility(ESlateVisibility::Hidden);
        }
    }
}

// ---------------------------------------------------------------------------
//  SetSelected
// ---------------------------------------------------------------------------

void UInventorySlotWidget::SetSelected(bool bSelected)
{
    if (SelectionBorder)
    {
        // Gold when active, medium grey when inactive
        SelectionBorder->SetBrushColor(bSelected
            ? FLinearColor(1.f, 0.85f, 0.f, 1.f)
            : FLinearColor(0.7f, 0.7f, 0.7f, 1.f));
    }
}

void UInventorySlotWidget::RestoreItemVisibility()
{
    if (ItemIcon)
    {
        ItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
}

// ---------------------------------------------------------------------------
//  Drag-Drop
// ---------------------------------------------------------------------------

FReply UInventorySlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && !CachedSlotData.IsEmpty())
    {
        // Start drag detection on left mouse button
        return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
    }

    return FReply::Unhandled();
}

void UInventorySlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
    Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);

    if (CachedSlotData.IsEmpty())
    {
        return;
    }

    // Create the drag-drop operation
    UInventoryDragDropOperation* DragOp = NewObject<UInventoryDragDropOperation>();
    if (!DragOp) return;

    DragOp->bFromHotbar = bIsHotbarSlot;
    DragOp->FromIndex = SlotIndex;
    DragOp->ItemID = CachedSlotData.ItemID;
    DragOp->Count = CachedSlotData.StackCount;
    DragOp->SourceSlotWidget = this;

    // Create a larger decorator widget showing icon + stack count
    if (ItemIcon && ItemIcon->IsVisible())
    {
        UBorder* DecoratorBorder = NewObject<UBorder>();
        if (DecoratorBorder)
        {
            DecoratorBorder->SetDesiredSizeScale(FVector2D(1.2f, 1.2f));

            // Clone the item icon for the decorator
            UImage* DecoratorIcon = NewObject<UImage>(DecoratorBorder);
            if (DecoratorIcon)
            {
                DecoratorIcon->SetBrush(ItemIcon->GetBrush());
                DecoratorBorder->AddChild(DecoratorIcon);
            }

            DragOp->DefaultDragVisual = DecoratorBorder;
            DragOp->Pivot = EDragPivot::MouseDown;
        }
    }

    // Hide the source item completely while dragging
    if (ItemIcon)
    {
        ItemIcon->SetVisibility(ESlateVisibility::Hidden);
    }

    OutOperation = DragOp;
}

bool UInventorySlotWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
    Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);

    // Check if this is an inventory drag-drop operation
    UInventoryDragDropOperation* InventoryOp = Cast<UInventoryDragDropOperation>(InOperation);
    if (InventoryOp)
    {
        // Signal that this slot can accept the drop
        return true;
    }

    return false;
}

bool UInventorySlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
    Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);

    if (!InOperation || !InventoryComponent)
    {
        return false;
    }

    UInventoryDragDropOperation* InventoryOp = Cast<UInventoryDragDropOperation>(InOperation);
    if (!InventoryOp)
    {
        return false;
    }

    // Restore visibility of source slot
    if (InventoryOp->SourceSlotWidget.IsValid())
    {
        UInventorySlotWidget* SourceSlot = InventoryOp->SourceSlotWidget.Get();
        if (SourceSlot && SourceSlot->ItemIcon)
        {
            SourceSlot->ItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
    }

    // Perform the swap
    InventoryComponent->SwapSlots(InventoryOp->bFromHotbar, InventoryOp->FromIndex,
                                   bIsHotbarSlot, SlotIndex);

    return true;
}

void UInventorySlotWidget::NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
    Super::NativeOnDragCancelled(InDragDropEvent, InOperation);

    UInventoryDragDropOperation* InventoryOp = Cast<UInventoryDragDropOperation>(InOperation);
    if (!InventoryOp)
    {
        return;
    }

    // Restore visibility of source slot if drag was cancelled
    if (InventoryOp->SourceSlotWidget.IsValid())
    {
        UInventorySlotWidget* SourceSlot = InventoryOp->SourceSlotWidget.Get();
        if (SourceSlot && SourceSlot->ItemIcon)
        {
            SourceSlot->ItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
    }
}
