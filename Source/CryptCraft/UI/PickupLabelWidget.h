#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PickupLabelWidget.generated.h"

class UTextBlock;

UCLASS(BlueprintType, Blueprintable)
class CRYPTCRAFT_API UPickupLabelWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "PickupLabel")
    void SetQuantity(int32 InQuantity);

protected:
    virtual void NativeConstruct() override;

private:
    void ApplyDefaultStyle();

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> QuantityText;
};
