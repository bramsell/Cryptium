#pragma once

#include "CoreMinimal.h"
#include "Items/RecipeData.h"
#include "Inventory/InventoryComponent.h"

class UInventoryComponent;

// ---------------------------------------------------------------------------
//  FRecipeSignature — normalized input pattern for recipe matching
//
//  Encodes the crafting grid state in a canonical form so that:
//    - Shaped recipes match only at correct positions
//    - Shapeless recipes match regardless of order
//    - Position-shifted variants (e.g., recipe in top-left or bottom-right)
//      are normalized to the same key
// ---------------------------------------------------------------------------

/**
 * Manages recipe loading, parsing, and lookup.
 *
 * Loads recipes from Content/Data/Recipes.json at startup, builds a
 * signature-to-recipe map for O(1) recipe matching. Call FindRecipeByInputs()
 * whenever the crafting input slots change to see if a recipe matches.
 */
class CRYPTCRAFT_API FRecipeManager
{
public:
    FRecipeManager();
    ~FRecipeManager();

    // -----------------------------------------------------------------------
    //  Initialization
    // -----------------------------------------------------------------------

    /**
     * Load and parse recipes from Content/Data/Recipes.json.
     * Should be called once at game startup or when recipes are reloaded.
     * @return true if recipes loaded successfully.
     */
    bool LoadRecipesFromJSON();

    // -----------------------------------------------------------------------
    //  Recipe Lookup
    // -----------------------------------------------------------------------

    /**
     * Find a recipe matching the current crafting input grid.
     * Computes the normalized signature of the inputs and looks it up.
     * @param Inputs Array of 4 crafting input slots (2×2 grid).
     * @return Pointer to the matching recipe, or nullptr if no match.
     */
    FRecipeData* FindRecipeByInputs(const TArray<FInventorySlot>& Inputs);

    /**
     * Look up a recipe by its row ID (e.g., "Recipe_Stone").
     * @param RecipeID The row name in the recipes data.
     * @return Pointer to the recipe, or nullptr if not found.
     */
    FRecipeData* GetRecipeByID(FName RecipeID);

private:
    // -----------------------------------------------------------------------
    //  Internal Helpers
    // -----------------------------------------------------------------------

    /**
     * Compute a normalized signature for a set of crafting inputs.
     * Handles both shaped and shapeless recipes via sorting.
     * @param Inputs Array of inventory slots.
     * @param bShapeless If true, sort items before serializing (order-independent).
     * @return Canonical signature string.
     */
    FString ComputeSignature(const TArray<FInventorySlot>& Inputs, bool bShapeless = false) const;

    /**
     * Normalize a signature by shifting all non-empty items to the top-left corner.
     * This allows recipes to match regardless of where they're placed in the 2×2 grid.
     * @param Inputs Array of inventory slots.
     * @return Normalized array with items shifted to [0,1,2,3] top-left positions.
     */
    TArray<FInventorySlot> NormalizeInputs(const TArray<FInventorySlot>& Inputs) const;

    // -----------------------------------------------------------------------
    //  Data
    // -----------------------------------------------------------------------

    /** All loaded recipes keyed by row ID. */
    TMap<FName, FRecipeData> AllRecipes;

    /** Recipe lookup by normalized signature. Maps signature → RecipeID. */
    TMap<FString, FName> SignatureToRecipeID;

    /** Whether recipes have been loaded. */
    bool bRecipesLoaded = false;
};
