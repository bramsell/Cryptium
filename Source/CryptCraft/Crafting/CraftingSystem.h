#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Crafting/RecipeManager.h"
#include "CraftingSystem.generated.h"

class UInventoryComponent;
struct FRecipeData;

/**
 * Manages crafting logic — validates recipes, produces outputs, handles validation changes.
 *
 * Attach to the player character or crafting table. On construction, initializes the recipe
 * manager and listens to inventory changes. Whenever crafting input slots change, validates
 * against recipes and automatically updates the output slot.
 */
UCLASS(ClassGroup = (CryptCraft))
class CRYPTCRAFT_API UCraftingSystem : public UObject
{
    GENERATED_BODY()

public:
    UCraftingSystem();

    // -----------------------------------------------------------------------
    //  Initialization
    // -----------------------------------------------------------------------

    /**
     * Initialize the crafting system with an inventory component.
     * Call this after construction or when attaching to a character.
     * @param InInventory The inventory component to monitor and modify.
     * @return true if initialization succeeded.
     */
    UFUNCTION(BlueprintCallable, Category = "Crafting")
    bool Initialize(UInventoryComponent* InInventory);

    // -----------------------------------------------------------------------
    //  Crafting execution
    // -----------------------------------------------------------------------

    /**
     * Manually execute a craft (remove inputs, produce outputs).
     * Called by UI buttons or when the player confirms the recipe.
     * @return true if the craft succeeded.
     */
    UFUNCTION(BlueprintCallable, Category = "Crafting")
    bool ExecuteCraft();

    /**
     * Validate the current crafting inputs against recipes.
     * Called automatically whenever inputs change, but can be called manually.
     */
    UFUNCTION(BlueprintCallable, Category = "Crafting")
    void ValidateCraftingInputs();

    // -----------------------------------------------------------------------
    //  Data access
    // -----------------------------------------------------------------------

    /**
     * Get the recipe that currently matches the crafting inputs (if any).
     * Not exposed to Blueprint due to struct pointer limitation.
     * @return Pointer to the matching recipe, or nullptr if no match.
     */
    FRecipeData* GetCurrentRecipe() const { return CurrentMatchingRecipe; }

    /**
     * Check if the crafting inputs currently form a valid recipe.
     */
    UFUNCTION(BlueprintPure, Category = "Crafting")
    bool HasValidRecipe() const { return CurrentMatchingRecipe != nullptr; }

private:
    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    /**
     * Callback when inventory changes (listening to OnInventoryChanged delegate).
     * Re-validates the current crafting inputs.
     */
    UFUNCTION()
    void OnInventoryChanged();

    /**
     * Apply a recipe's outputs to the crafting output slot.
     * Assumes the recipe is valid and has been matched.
     */
    void ApplyRecipeOutput(FRecipeData* Recipe);

    /**
     * Clear the crafting output slot.
     */
    void ClearCraftingOutput();

    // -----------------------------------------------------------------------
    //  Data
    // -----------------------------------------------------------------------

    /** The inventory component we're monitoring. */
    UPROPERTY()
    TObjectPtr<UInventoryComponent> InventoryComponent;

    /** Recipe manager for loading and matching recipes. */
    FRecipeManager RecipeManager;

    /** The currently matching recipe (if any). */
    FRecipeData* CurrentMatchingRecipe = nullptr;

    /** Flag to prevent re-validation during ApplyRecipeOutput. */
    bool bUpdatingOutput = false;
};
