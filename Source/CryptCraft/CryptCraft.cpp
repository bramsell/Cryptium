// Copyright Epic Games, Inc. All Rights Reserved.

#include "CryptCraft.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Texture2D.h"
#include "TextureCompiler.h"              // FTextureCompilingManager
#include "FileHelpers.h"                   // UEditorLoadingAndSavingUtils::SavePackages
#endif

DEFINE_LOG_CATEGORY(LogCryptCraft)

// ---------------------------------------------------------------------------
//  Editor-only helper: ensures every Texture2D under /Game/Textures/Blocks
//  has CompressionSettings = TC_EditorIcon (UserInterface2D) so the atlas
//  builder can read raw BGRA8 pixels via BulkData::Lock() at runtime.
//  Runs once after the asset registry finishes its initial scan.
// ---------------------------------------------------------------------------
#if WITH_EDITOR
static void FixBlockTextureCompression()
{
	IAssetRegistry& AR =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Filter to Texture2D assets under the blocks folder only.
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game/Textures/Blocks"));
	Filter.bRecursivePaths  = true;
	Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = false;

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	int32 Fixed = 0;
	TArray<UPackage*> PackagesToSave;

	for (const FAssetData& AD : Assets)
	{
		UTexture2D* Tex = Cast<UTexture2D>(AD.GetAsset());
		if (!Tex) continue;

		if (Tex->CompressionSettings != TC_EditorIcon)
		{
			// PreEditChange: notify undo/property-change system before modifying.
			Tex->PreEditChange(nullptr);

			Tex->CompressionSettings = TC_EditorIcon;
			Tex->SRGB = true;

			// PostEditChange: triggers the async texture compiler to rebuild
			// platform data from source pixels → BGRA8 under TC_EditorIcon.
			// This is what was missing before — without it the DDC blob stays
			// compressed and GetPixelFormat() keeps returning 5 (DXT1).
			Tex->PostEditChange();
			Tex->MarkPackageDirty();

			// Collect the outer package so we can bulk-save after compilation.
			if (UPackage* Pkg = Tex->GetOutermost())
			{
				PackagesToSave.AddUnique(Pkg);
			}

			++Fixed;
			UE_LOG(LogCryptCraft, Log,
				TEXT("BlockTextureCompression: queued '%s' for TC_EditorIcon rebuild"),
				*Tex->GetName());
		}
		else
		{
			UE_LOG(LogCryptCraft, Log,
				TEXT("BlockTextureCompression: '%s' already TC_EditorIcon  pixelFmt=%d"),
				*Tex->GetName(), (int32)Tex->GetPixelFormat());
		}
	}

	if (Fixed > 0 && PackagesToSave.Num() > 0)
	{
		// PostEditChange() dispatches async compilation jobs. We must wait for
		// all of them to finish before saving — otherwise the BGRA8 platform
		// data hasn't been written yet and the saved .uasset would still contain
		// the old compressed format.
		UE_LOG(LogCryptCraft, Log,
			TEXT("BlockTextureCompression: waiting for %d texture(s) to finish compiling..."), Fixed);
		FTextureCompilingManager::Get().FinishAllCompilation();

		// SavePackages: bOnlyDirty=true skips packages that aren't dirty.
		const bool bOnlyDirty = true;
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, bOnlyDirty);

		UE_LOG(LogCryptCraft, Log,
			TEXT("BlockTextureCompression: saved %d package(s) — textures are now persistently BGRA8."),
			PackagesToSave.Num());
	}

	UE_LOG(LogCryptCraft, Log,
		TEXT("BlockTextureCompression: done — %d texture(s) updated under /Game/Textures/Blocks."),
		Fixed);
}
#endif  // WITH_EDITOR

// ---------------------------------------------------------------------------
//  Custom primary game module
// ---------------------------------------------------------------------------
class FCryptCraftModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();

#if WITH_EDITOR
		// The asset registry may still be scanning when StartupModule fires.
		// Bind to OnFilesLoaded if it is still busy, otherwise run immediately.
		IAssetRegistry& AR =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		if (AR.IsLoadingAssets())
		{
			AR.OnFilesLoaded().AddLambda([]() { FixBlockTextureCompression(); });
		}
		else
		{
			// Already loaded (e.g. hot-reload after initial scan).
			FixBlockTextureCompression();
		}
#endif
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FCryptCraftModule, CryptCraft, "CryptCraft");