#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Inventory/InventoryComponent.h"
#include "InventoryDragDropOperation.generated.h"

/**
 * Custom drag-drop operation for inventory items.
 * Carries all necessary data about the item being dragged.
 */
UCLASS()
class CRYPTCRAFT_API UInventoryDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	/** True if dragging from hotbar, false if from main grid */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Drag")
	bool bFromHotbar = false;

	/** Container type of the source slot (for proper SwapSlots routing) */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Drag")
	EInventoryContainer SourceContainer = EInventoryContainer::MainGrid;

	/** Index of the source slot */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Drag")
	int32 FromIndex = -1;

	/** ItemID being dragged */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Drag")
	FName ItemID;

	/** Stack count being dragged */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Drag")
	int32 Count = 0;

	/** Weak reference to the source slot widget for visual feedback */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Drag")
	TWeakObjectPtr<class UInventorySlotWidget> SourceSlotWidget;
};
