// VoxelHUD.cpp

#include "VoxelHUD.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

void AVoxelHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas) return;

	// Get the owning player's pawn location
	APlayerController* PC = GetOwningPlayerController();
	if (!PC) return;

	APawn* Pawn = PC->GetPawn();
	if (!Pawn) return;

	const FVector Loc = Pawn->GetActorLocation();
	const FVector BlockLoc = Loc / 100.f;

	// Display in block units (100 UU = 1 block).
	const FString CoordText = FString::Printf(
		TEXT("X: %.1f  Y: %.1f  Z: %.1f"),
		BlockLoc.X, BlockLoc.Y, BlockLoc.Z
	);

	// Draw at top-left with a small margin
	const float MarginX = 20.f;
	const float MarginY = 20.f;
	const float FontScale = 1.25f;

	FCanvasTextItem TextItem(
		FVector2D(MarginX, MarginY),
		FText::FromString(CoordText),
		GEngine->GetSmallFont(),
		FLinearColor::White
	);
	TextItem.Scale    = FVector2D(FontScale, FontScale);
	TextItem.bOutlined = true;
	TextItem.OutlineColor = FLinearColor::Black;   // thin black edge for legibility
	Canvas->DrawItem(TextItem);
}
