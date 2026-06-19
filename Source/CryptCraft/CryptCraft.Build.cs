// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CryptCraft : ModuleRules
{
	public CryptCraft(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"PhysicsCore",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"ProceduralMeshComponent"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// Editor-only: needed for FixBlockTextureCompression() in CryptCraft.cpp
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("AssetRegistry");
			// UEditorLoadingAndSavingUtils::SavePackages
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		PublicIncludePaths.AddRange(new string[] {
			"CryptCraft",
			"CryptCraft/Variant_Horror",
			"CryptCraft/Variant_Horror/UI",
			"CryptCraft/Variant_Shooter",
			"CryptCraft/Variant_Shooter/AI",
			"CryptCraft/Variant_Shooter/UI",
			"CryptCraft/Variant_Shooter/Weapons",
		"CryptCraft/Voxel",
		"CryptCraft/Ships"
	});

	// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
