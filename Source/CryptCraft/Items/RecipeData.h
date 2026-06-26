#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "RecipeData.generated.h"

// ---------------------------------------------------------------------------
//  Recipe input requirement — one slot of the crafting grid
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct CRYPTCRAFT_API FRecipeInput
{
    GENERATED_BODY()

    /** The item ID that must be in this input slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    FName ItemID;

    /** How many of this item are required in the slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe", meta = (ClampMin = "1"))
    int32 Quantity = 1;
};

// ---------------------------------------------------------------------------
//  Recipe output — what the player receives after crafting
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct CRYPTCRAFT_API FRecipeOutput
{
    GENERATED_BODY()

    /** The item ID produced by the recipe. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    FName ItemID;

    /** How many of this item are created. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe", meta = (ClampMin = "1"))
    int32 Quantity = 1;
};

// ---------------------------------------------------------------------------
//  Recipe type — distinguishes crafting from smelting, etc.
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class ERecipeType : uint8
{
    Crafting UMETA(DisplayName = "Crafting"),
    Smelting UMETA(DisplayName = "Smelting"),
};

// ---------------------------------------------------------------------------
//  FRecipeData — a single crafting recipe
//
//  Stored in a UDataTable with RowName as the recipe identifier.
//  The crafting UI and logic look up recipes by name and validate inputs.
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct CRYPTCRAFT_API FRecipeData : public FTableRowBase
{
    GENERATED_BODY()

    /** Human-readable recipe name (e.g., "Stone Block"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    FString RecipeName = TEXT("Unnamed Recipe");

    /** Type of recipe (crafting, smelting, etc.). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    ERecipeType RecipeType = ERecipeType::Crafting;

    /**
     * Array of input requirements. Must exactly match the crafting grid layout:
     *   Slot 0 = top-left
     *   Slot 1 = top-right
     *   Slot 2 = bottom-left
     *   Slot 3 = bottom-right
     *
     * If a slot is not part of the recipe, leave it empty or set ItemID to None.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    TArray<FRecipeInput> Inputs;

    /** Items produced when the recipe is completed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    TArray<FRecipeOutput> Outputs;

    /** Time in seconds to craft this recipe (for future smelting systems). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe", meta = (ClampMin = "0"))
    float CraftingTimeSeconds = 1.0f;

    /**
     * Icon/texture to display in the recipe browser.
     * Leave nullptr to auto-derive from the first output item.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
    TObjectPtr<UTexture2D> RecipeIcon = nullptr;
};
