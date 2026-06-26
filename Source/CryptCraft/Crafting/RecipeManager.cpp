#include "Crafting/RecipeManager.h"
#include "Inventory/InventoryComponent.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"

FRecipeManager::FRecipeManager()
{
}

FRecipeManager::~FRecipeManager()
{
}

bool FRecipeManager::LoadRecipesFromJSON()
{
    // Path to the recipes JSON file in Content/Data/
    FString RecipesPath = FPaths::ProjectContentDir() + TEXT("Data/Recipes.json");
    
    if (!FPaths::FileExists(RecipesPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[RecipeManager] Recipes.json not found at: %s"), *RecipesPath);
        return false;
    }

    // Read the file
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *RecipesPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[RecipeManager] Failed to read Recipes.json"));
        return false;
    }

    // Parse JSON
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[RecipeManager] Failed to parse Recipes.json as valid JSON"));
        return false;
    }

    // Extract recipes array
    TArray<TSharedPtr<FJsonValue>> RecipesArray = RootObject->GetArrayField(TEXT("recipes"));
    
    for (const TSharedPtr<FJsonValue>& RecipeValue : RecipesArray)
    {
        TSharedPtr<FJsonObject> RecipeObj = RecipeValue->AsObject();
        if (!RecipeObj.IsValid())
        {
            continue;
        }

        FRecipeData Recipe;
        
        // Parse recipe fields
        Recipe.RecipeName = RecipeObj->GetStringField(TEXT("name"));
        
        FString TypeStr = RecipeObj->GetStringField(TEXT("type"));
        Recipe.RecipeType = (TypeStr == TEXT("Smelting")) ? ERecipeType::Smelting : ERecipeType::Crafting;
        
        Recipe.CraftingTimeSeconds = RecipeObj->GetNumberField(TEXT("craftingTimeSeconds"));

        // Parse inputs
        TArray<TSharedPtr<FJsonValue>> InputsArray = RecipeObj->GetArrayField(TEXT("inputs"));
        Recipe.Inputs.SetNum(InputsArray.Num());
        
        for (int32 i = 0; i < InputsArray.Num(); ++i)
        {
            TSharedPtr<FJsonObject> InputObj = InputsArray[i]->AsObject();
            if (InputObj.IsValid())
            {
                Recipe.Inputs[i].ItemID = FName(*InputObj->GetStringField(TEXT("itemId")));
                Recipe.Inputs[i].Quantity = static_cast<int32>(InputObj->GetNumberField(TEXT("quantity")));
            }
        }

        // Parse outputs
        TArray<TSharedPtr<FJsonValue>> OutputsArray = RecipeObj->GetArrayField(TEXT("outputs"));
        Recipe.Outputs.SetNum(OutputsArray.Num());
        
        for (int32 i = 0; i < OutputsArray.Num(); ++i)
        {
            TSharedPtr<FJsonObject> OutputObj = OutputsArray[i]->AsObject();
            if (OutputObj.IsValid())
            {
                Recipe.Outputs[i].ItemID = FName(*OutputObj->GetStringField(TEXT("itemId")));
                Recipe.Outputs[i].Quantity = static_cast<int32>(OutputObj->GetNumberField(TEXT("quantity")));
            }
        }

        // Use the recipe ID as the key
        FName RecipeID = FName(*RecipeObj->GetStringField(TEXT("id")));
        AllRecipes.Add(RecipeID, Recipe);

        // Build the signature map entry
        TArray<FInventorySlot> InputSlots;
        InputSlots.SetNum(4);  // 2×2 grid
        
        for (int32 i = 0; i < Recipe.Inputs.Num() && i < 4; ++i)
        {
            InputSlots[i].ItemID = Recipe.Inputs[i].ItemID;
            InputSlots[i].StackCount = Recipe.Inputs[i].Quantity;
        }

        FString Signature = ComputeSignature(InputSlots, Recipe.RecipeType == ERecipeType::Crafting);
        SignatureToRecipeID.Add(Signature, RecipeID);

        UE_LOG(LogTemp, Log, TEXT("[RecipeManager] Loaded recipe '%s' with signature '%s'"), *RecipeID.ToString(), *Signature);
    }

    bRecipesLoaded = true;
    UE_LOG(LogTemp, Warning, TEXT("[RecipeManager] Successfully loaded %d recipes from Recipes.json"), AllRecipes.Num());
    
    return true;
}

FRecipeData* FRecipeManager::FindRecipeByInputs(const TArray<FInventorySlot>& Inputs)
{
    if (!bRecipesLoaded)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RecipeManager] Recipes not loaded! Call LoadRecipesFromJSON first."));
        return nullptr;
    }

    // Compute the normalized signature of the input grid
    FString InputSignature = ComputeSignature(Inputs, false);  // Assume shaped for now
    
    // Look up in the map
    if (FName* RecipeIDPtr = SignatureToRecipeID.Find(InputSignature))
    {
        if (FRecipeData* Recipe = AllRecipes.Find(*RecipeIDPtr))
        {
            UE_LOG(LogTemp, Log, TEXT("[RecipeManager] Found recipe '%s' for signature '%s'"), *RecipeIDPtr->ToString(), *InputSignature);
            return Recipe;
        }
    }

    // Try shapeless variant (inputs sorted)
    FString ShapelessSignature = ComputeSignature(Inputs, true);
    if (ShapelessSignature != InputSignature)
    {
        if (FName* RecipeIDPtr = SignatureToRecipeID.Find(ShapelessSignature))
        {
            if (FRecipeData* Recipe = AllRecipes.Find(*RecipeIDPtr))
            {
                UE_LOG(LogTemp, Log, TEXT("[RecipeManager] Found recipe '%s' for shapeless signature '%s'"), *RecipeIDPtr->ToString(), *ShapelessSignature);
                return Recipe;
            }
        }
    }

    return nullptr;
}

FRecipeData* FRecipeManager::GetRecipeByID(FName RecipeID)
{
    if (!bRecipesLoaded)
    {
        return nullptr;
    }

    return AllRecipes.Find(RecipeID);
}

FString FRecipeManager::ComputeSignature(const TArray<FInventorySlot>& Inputs, bool bShapeless) const
{
    // Normalize inputs to top-left corner
    TArray<FInventorySlot> Normalized = NormalizeInputs(Inputs);

    // For shapeless recipes, sort the item IDs so order doesn't matter
    TArray<FName> ItemIDs;
    for (const FInventorySlot& Slot : Normalized)
    {
        if (!Slot.IsEmpty())
        {
            ItemIDs.Add(Slot.ItemID);
        }
    }

    if (bShapeless)
    {
        ItemIDs.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
    }

    // Build signature string
    FString Signature;
    if (bShapeless)
    {
        // For shapeless: just concatenate sorted items
        for (const FName& ItemID : ItemIDs)
        {
            if (!Signature.IsEmpty()) Signature += TEXT(",");
            Signature += ItemID.ToString();
        }
    }
    else
    {
        // For shaped: preserve grid positions
        for (int32 i = 0; i < Normalized.Num(); ++i)
        {
            if (i > 0) Signature += TEXT(",");
            Signature += Normalized[i].IsEmpty() ? TEXT("none") : Normalized[i].ItemID.ToString();
        }
    }

    return Signature;
}

TArray<FInventorySlot> FRecipeManager::NormalizeInputs(const TArray<FInventorySlot>& Inputs) const
{
    // For a 2×2 grid, find the bounding box of non-empty slots and shift to top-left
    // Layout:
    //   0 1
    //   2 3
    
    int32 MinRow = 2, MinCol = 2;
    bool bHasItems = false;

    // Find bounding box
    for (int32 i = 0; i < Inputs.Num() && i < 4; ++i)
    {
        if (!Inputs[i].IsEmpty())
        {
            int32 Row = i / 2;
            int32 Col = i % 2;
            MinRow = FMath::Min(MinRow, Row);
            MinCol = FMath::Min(MinCol, Col);
            bHasItems = true;
        }
    }

    // If no items, return all empty
    if (!bHasItems)
    {
        TArray<FInventorySlot> Empty;
        Empty.SetNum(4);
        return Empty;
    }

    // Shift all items by (MinRow, MinCol) to top-left
    TArray<FInventorySlot> Normalized;
    Normalized.SetNum(4);

    for (int32 i = 0; i < Inputs.Num() && i < 4; ++i)
    {
        if (!Inputs[i].IsEmpty())
        {
            int32 Row = i / 2;
            int32 Col = i % 2;
            int32 NormalizedRow = Row - MinRow;
            int32 NormalizedCol = Col - MinCol;
            
            // Only place if within 2×2 bounds
            if (NormalizedRow < 2 && NormalizedCol < 2)
            {
                int32 NormalizedIndex = NormalizedRow * 2 + NormalizedCol;
                Normalized[NormalizedIndex] = Inputs[i];
            }
        }
    }

    return Normalized;
}
