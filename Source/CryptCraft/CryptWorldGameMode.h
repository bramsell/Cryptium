// CryptWorldGameMode.h
// Concrete (non-abstract) GameMode for CryptWorld.
// All logic lives in the C++ parent ACryptCraftGameMode – this class simply
// makes it instantiable without needing a Blueprint wrapper.

#pragma once

#include "CoreMinimal.h"
#include "CryptCraftGameMode.h"
#include "CryptWorldGameMode.generated.h"

UCLASS()
class CRYPTCRAFT_API ACryptWorldGameMode : public ACryptCraftGameMode
{
	GENERATED_BODY()

public:
	ACryptWorldGameMode();
};
