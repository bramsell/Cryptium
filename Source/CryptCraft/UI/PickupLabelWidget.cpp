#include "UI/PickupLabelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Fonts/SlateFontInfo.h"

void UPickupLabelWidget::NativeConstruct()
{
    Super::NativeConstruct();

    SetVisibility(ESlateVisibility::HitTestInvisible);

    UE_LOG(LogTemp, Warning,
        TEXT("[PickupLabelWidget] NativeConstruct for %s (initial QuantityText=%s)"),
        *GetName(),
        QuantityText ? TEXT("valid") : TEXT("null"));

    if (!QuantityText && WidgetTree)
    {
        if (UWidget* FoundWidget = WidgetTree->FindWidget(TEXT("QuantityText")))
        {
            QuantityText = Cast<UTextBlock>(FoundWidget);
            UE_LOG(LogTemp, Warning,
                TEXT("[PickupLabelWidget] FindWidget('QuantityText') -> %s"),
                QuantityText ? TEXT("UTextBlock found") : TEXT("found but not UTextBlock"));
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[PickupLabelWidget] FindWidget('QuantityText') returned null"));
        }
    }

    if (!QuantityText && WidgetTree)
    {
        QuantityText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuantityText"));
        WidgetTree->RootWidget = QuantityText;
        UE_LOG(LogTemp, Warning,
            TEXT("[PickupLabelWidget] Constructed fallback QuantityText and set as root"));
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[PickupLabelWidget] NativeConstruct final QuantityText=%s"),
        QuantityText ? TEXT("valid") : TEXT("null"));

    ApplyDefaultStyle();
    SetQuantity(1);
}

void UPickupLabelWidget::SetQuantity(int32 InQuantity)
{
    if (!QuantityText)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PickupLabelWidget] SetQuantity(%d) skipped: QuantityText is null"),
            InQuantity);
        return;
    }

    QuantityText->SetVisibility(ESlateVisibility::HitTestInvisible);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    QuantityText->SetText(FText::FromString(FString::Printf(TEXT("x%d"), FMath::Max(1, InQuantity))));

    UE_LOG(LogTemp, Warning,
        TEXT("[PickupLabelWidget] SetQuantity(%d) applied. Text=%s Visibility=%d"),
        InQuantity,
        *QuantityText->GetText().ToString(),
        static_cast<int32>(QuantityText->GetVisibility()));
}

void UPickupLabelWidget::ApplyDefaultStyle()
{
    if (!QuantityText)
    {
        return;
    }

    FSlateFontInfo FontInfo = QuantityText->GetFont();
    FontInfo.Size = 18;
    QuantityText->SetFont(FontInfo);
    QuantityText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    QuantityText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
    QuantityText->SetShadowOffset(FVector2D(2.f, 2.f));
    QuantityText->SetJustification(ETextJustify::Center);
}
