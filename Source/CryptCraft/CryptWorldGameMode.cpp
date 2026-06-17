// CryptWorldGameMode.cpp

#include "CryptWorldGameMode.h"
#include "CryptWorldCharacter.h"
#include "Voxel/VoxelHUD.h"

ACryptWorldGameMode::ACryptWorldGameMode()
{
	// Use our concrete first-person character (loads its own input assets).
	DefaultPawnClass = ACryptWorldCharacter::StaticClass();

	// Register the coordinate HUD so it shows up automatically in CryptWorld.
	HUDClass = AVoxelHUD::StaticClass();
}
