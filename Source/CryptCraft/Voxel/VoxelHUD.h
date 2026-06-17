// VoxelHUD.h – Simple HUD that draws the player's world coordinates in the
// top-left corner of the screen.  No Blueprint wrapper required.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "VoxelHUD.generated.h"

UCLASS()
class CRYPTCRAFT_API AVoxelHUD : public AHUD
{
	GENERATED_BODY()

public:
	/** Called every frame to draw the HUD. */
	virtual void DrawHUD() override;
};
