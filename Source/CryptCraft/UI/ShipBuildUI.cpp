// ShipBuildUI.cpp

#include "UI/ShipBuildUI.h"
#include "Ships/ControlBlock.h"

void UShipBuildUI::InitializeUI(AControlBlock* InControlBlock, const TMap<FIntVector, EBlockType>& InDetectedBlocks)
{
	ControlBlock = InControlBlock;
	DetectedBlocks = InDetectedBlocks;
	DetectedBlockCount = DetectedBlocks.Num();

	UE_LOG(LogTemp, Log, TEXT("[UShipBuildUI] Initialized with %d blocks"), DetectedBlockCount);
}
