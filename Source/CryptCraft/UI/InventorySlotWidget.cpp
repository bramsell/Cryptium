#include "UI/InventorySlotWidget.h"

#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

// ---------------------------------------------------------------------------
//  RefreshSlot
// ---------------------------------------------------------------------------

void UInventorySlotWidget::RefreshSlot(const FInventorySlot& InSlot, const FItemData* Data)
{
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
        // Gold when active, faint white when inactive
        SelectionBorder->SetBrushColor(bSelected
            ? FLinearColor(1.f, 0.85f, 0.f, 1.f)
            : FLinearColor(1.f, 1.f, 1.f, 0.15f));
    }
}
