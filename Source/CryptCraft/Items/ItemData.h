#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "ItemData.generated.h"

// ---------------------------------------------------------------------------
//  Item type categories
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EItemType : uint8
{
    Block       UMETA(DisplayName = "Block"),
    Weapon      UMETA(DisplayName = "Weapon"),
    Armor       UMETA(DisplayName = "Armor"),
    Charm       UMETA(DisplayName = "Charm"),
    Ring        UMETA(DisplayName = "Ring"),
    Consumable  UMETA(DisplayName = "Consumable"),
    Misc        UMETA(DisplayName = "Misc"),
};

// ---------------------------------------------------------------------------
//  Equipment slot identifiers (used by UInventoryComponent)
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EEquipmentSlot : uint8
{
    Helmet      UMETA(DisplayName = "Helmet"),
    Chestplate  UMETA(DisplayName = "Chestplate"),
    Leggings    UMETA(DisplayName = "Leggings"),
    Boots       UMETA(DisplayName = "Boots"),
    Back        UMETA(DisplayName = "Back"),
    Wings       UMETA(DisplayName = "Wings"),
    Necklace    UMETA(DisplayName = "Necklace"),
    LeftHand    UMETA(DisplayName = "Left Hand"),
    RightHand   UMETA(DisplayName = "Right Hand"),
    Charm1      UMETA(DisplayName = "Charm 1"),
    Charm2      UMETA(DisplayName = "Charm 2"),
    Charm3      UMETA(DisplayName = "Charm 3"),
    Ring1       UMETA(DisplayName = "Ring 1"),
    Ring2       UMETA(DisplayName = "Ring 2"),
    Ring3       UMETA(DisplayName = "Ring 3"),
    Ring4       UMETA(DisplayName = "Ring 4"),
    Ring5       UMETA(DisplayName = "Ring 5"),
    Ring6       UMETA(DisplayName = "Ring 6"),
    Ring7       UMETA(DisplayName = "Ring 7"),
    Ring8       UMETA(DisplayName = "Ring 8"),
};

// ---------------------------------------------------------------------------
//  Placeholder combat / utility stats – expand freely later
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct CRYPTCRAFT_API FItemStats
{
    GENERATED_BODY()

    /** Flat bonus to outgoing damage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float AttackDamage = 0.f;

    /** Flat damage reduction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float Defense = 0.f;

    /** Multiplier on base attack speed (1.0 = normal). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float AttackSpeed = 1.f;

    /** Flat addition to movement speed (UU/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float MoveSpeedBonus = 0.f;

    /** Maximum health bonus granted while equipped/held. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float MaxHealthBonus = 0.f;

    /** Luck value – used for loot table rolls. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float Luck = 0.f;
};

// ---------------------------------------------------------------------------
//  Item definition row – use this as the row type in a UDataTable asset
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct CRYPTCRAFT_API FItemData : public FTableRowBase
{
    GENERATED_BODY()

    /**
     * Unique identifier for this item (matches the DataTable row name).
     * Stored again here for convenience when the struct is passed by value.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    FName ItemID;

    /** Human-readable display name (localisation-friendly). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    FText DisplayName;

    /** Short description shown in tooltips. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    FText Description;

    /**
     * Inventory icon. Stored as a soft reference so textures are not loaded
     * until the inventory UI is open.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    TSoftObjectPtr<UTexture2D> Icon;

    /** How many of this item can share one inventory slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "1"))
    int32 MaxStackSize = 64;

    /** Broad category that drives where the item can be equipped / used. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    EItemType ItemType = EItemType::Misc;

    /**
     * For Block items this maps to a voxel type ID so the player can place it.
     * Unused (0) for non-block items.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Block")
    int32 BlockTypeID = 0;

    /** Which equipment slot this item occupies when equipped. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Equipment")
    EEquipmentSlot EquipSlot = EEquipmentSlot::Helmet;

    /** Stat modifiers this item grants when equipped or held. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Stats")
    FItemStats Stats;

    /** Whether this item can be dropped into the world. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    bool bDroppable = true;

    /** Rarity tier (0 = Common … 4 = Legendary). Drives tooltip colour etc. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "0", ClampMax = "4"))
    int32 Rarity = 0;
};
