// ShipBuildUI.h
// Placeholder widget for ship building (UI disabled for now).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Voxel/VoxelTypes.h"
#include "ShipBuildUI.generated.h"

class AControlBlock;

/**
 * Placeholder UI widget for ship building.
 * Currently just logs information; actual UI implementation deferred.
 */
UCLASS()
class CRYPTCRAFT_API UShipBuildUI : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Initialize with ship detection results (currently just logs).
	 */
	UFUNCTION(BlueprintCallable, Category = "Ship|UI")
	void InitializeUI(AControlBlock* InControlBlock, const TMap<FIntVector, EBlockType>& InDetectedBlocks);

private:
	UPROPERTY()
	TObjectPtr<AControlBlock> ControlBlock;

	TMap<FIntVector, EBlockType> DetectedBlocks;
	int32 DetectedBlockCount = 0;
};
