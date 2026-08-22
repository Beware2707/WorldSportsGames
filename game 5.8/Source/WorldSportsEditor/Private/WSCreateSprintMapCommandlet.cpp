#include "WSCreateSprintMapCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

/**
 * Generates the sprint level as a real .umap asset.
 *
 * The level itself is deliberately EMPTY — the track, athletes, camera and
 * HUD are all spawned by AWSSprintGameMode at BeginPlay. That keeps the one
 * binary asset in the repository trivial and reviewable, and it means level
 * content can never drift out of sync with the code that drives the race.
 *
 * Run: UnrealEditor-Cmd.exe <project> -run=WSCreateSprintMap
 */

UWSCreateSprintMapCommandlet::UWSCreateSprintMapCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UWSCreateSprintMapCommandlet::Main(const FString& Params)
{
	const FString PackageName = TEXT("/Game/Sports/Athletics/Sprint100/L_Sprint100");
	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackageName, FPackageName::GetMapPackageExtension());

	UPackage* Package = CreatePackage(*PackageName);
	UWorld* World = UWorld::CreateWorld(EWorldType::Inactive, /*bInformEngineOfWorld=*/false,
		FName(TEXT("L_Sprint100")), Package);
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create the sprint world"));
		return 1;
	}
	// A world only saves as a map once its package carries the map flag and
	// the target directory exists.
	Package->SetPackageFlags(PKG_ContainsMap);
	World->SetFlags(RF_Public | RF_Standalone);
	Package->MarkPackageDirty();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*FPaths::GetPath(FileName));

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GWarn;
	const bool bSaved = UPackage::SavePackage(Package, World, *FileName, SaveArgs);

	UE_LOG(LogTemp, Display, TEXT("Sprint map save %s -> %s"),
		bSaved ? TEXT("succeeded") : TEXT("FAILED"), *FileName);
	return bSaved ? 0 : 1;
}
