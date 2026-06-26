#include "Crafting/CraftingSystem.h"
#include "Inventory/InventoryComponent.h"

UCraftingSystem::UCraftingSystem()
{
}

bool UCraftingSystem::Initialize(UInventoryComponent* InInventory)
{
    if (!InInventory)
    {
        UE_LOG(LogTemp, Error, TEXT("[CraftingSystem] Initialize called with null InventoryComponent"));
        return false;
    }

    InventoryComponent = InInventory;

    // Load recipes
    if (!RecipeManager.LoadRecipesFromJSON())
    {
        UE_LOG(LogTemp, Error, TEXT("[CraftingSystem] Failed to load recipes"));
        return false;
    }

    // Listen to inventory changes
    InventoryComponent->OnInventoryChanged.AddDynamic(this, &UCraftingSystem::OnInventoryChanged);

    UE_LOG(LogTemp, Warning, TEXT("[CraftingSystem] Initialized successfully"));

    // Perform initial validation
    ValidateCraftingInputs();

    return true;
}

bool UCraftingSystem::ExecuteCraft()
{
    if (!InventoryComponent || !CurrentMatchingRecipe)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CraftingSystem] ExecuteCraft called but no valid recipe"));
        return false;
    }

    UE_LOG(LogTemp, Warning, TEXT("[CraftingSystem] Executing craft for recipe '%s'"), *CurrentMatchingRecipe->RecipeName);

    // Consume inputs from crafting input slots
    for (int32 i = 0; i < InventoryComponent->CraftingInputSlots.Num(); ++i)
    {
        FInventorySlot& Slot = InventoryComponent->CraftingInputSlots[i];
        if (!Slot.IsEmpty())
        {
            Slot.StackCount -= 1;
            if (Slot.StackCount <= 0)
            {
                Slot.Clear();
            }
        }
    }

    // Notify that inventory changed (will trigger re-validation)
    InventoryComponent->NotifyInventoryChanged();

    return true;
}

void UCraftingSystem::ValidateCraftingInputs()
{
    if (!InventoryComponent)
    {
        return;
    }

    // Prevent re-validation while we're updating the output
    if (bUpdatingOutput)
    {
        return;
    }

    // Check if current inputs match a recipe
    FRecipeData* MatchingRecipe = RecipeManager.FindRecipeByInputs(InventoryComponent->CraftingInputSlots);

    // If the recipe changed, update the output
    if (MatchingRecipe != CurrentMatchingRecipe)
    {
        CurrentMatchingRecipe = MatchingRecipe;

        bUpdatingOutput = true;

        if (MatchingRecipe)
        {
            UE_LOG(LogTemp, Warning, TEXT("[CraftingSystem] Recipe matched: '%s'"), *MatchingRecipe->RecipeName);
            ApplyRecipeOutput(MatchingRecipe);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[CraftingSystem] No recipe matches current inputs"));
            ClearCraftingOutput();
        }

        bUpdatingOutput = false;
    }
}

void UCraftingSystem::OnInventoryChanged()
{
    // Re-validate crafting whenever inventory changes
    ValidateCraftingInputs();
}

void UCraftingSystem::ApplyRecipeOutput(FRecipeData* Recipe)
{
    if (!InventoryComponent || !Recipe || Recipe->Outputs.Num() == 0)
    {
        return;
    }

    // For now, assume first output (most recipes have 1)
    // TODO: Handle multi-output recipes later
    const FRecipeOutput& Output = Recipe->Outputs[0];

    InventoryComponent->CraftingOutputSlot.ItemID = Output.ItemID;
    InventoryComponent->CraftingOutputSlot.StackCount = Output.Quantity;

    UE_LOG(LogTemp, Warning, TEXT("[CraftingSystem] Applied output: %s x%d"), *Output.ItemID.ToString(), Output.Quantity);
}

void UCraftingSystem::ClearCraftingOutput()
{
    if (!InventoryComponent)
    {
        return;
    }

    InventoryComponent->CraftingOutputSlot.Clear();
}
