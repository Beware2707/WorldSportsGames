#include "Sports/WSSportRegistrySubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/WSLog.h"
#include "Sports/Definitions/WSEventDefinition.h"
#include "Sports/Definitions/WSSportDefinition.h"

void UWSSportRegistrySubsystem::EnsureScanned()
{
	if (bScanned)
	{
		return;
	}
	bScanned = true;

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.WaitForCompletion();

	TArray<FAssetData> Found;
	AssetRegistry.GetAssetsByClass(
		UWSEventDefinition::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses=*/true);
	for (const FAssetData& Asset : Found)
	{
		if (UWSEventDefinition* Definition = Cast<UWSEventDefinition>(Asset.GetAsset()))
		{
			Events.Add(Definition);
		}
	}

	Found.Reset();
	AssetRegistry.GetAssetsByClass(
		UWSSportDefinition::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses=*/true);
	for (const FAssetData& Asset : Found)
	{
		if (UWSSportDefinition* Definition = Cast<UWSSportDefinition>(Asset.GetAsset()))
		{
			Sports.Add(Definition);
		}
	}

	UE_LOG(LogWorldSports, Log, TEXT("Sport registry: %d sport(s), %d event(s)"),
		Sports.Num(), Events.Num());
}

UWSEventDefinition* UWSSportRegistrySubsystem::ResolveEvent(const FString& EventCode)
{
	EnsureScanned();
	for (UWSEventDefinition* Event : Events)
	{
		if (Event->EventCode == EventCode)
		{
			return Event;
		}
	}
	return nullptr;
}

TArray<UWSSportDefinition*> UWSSportRegistrySubsystem::GetAllSports()
{
	EnsureScanned();
	return TArray<UWSSportDefinition*>(Sports);
}

TArray<UWSEventDefinition*> UWSSportRegistrySubsystem::GetAllEvents()
{
	EnsureScanned();
	return TArray<UWSEventDefinition*>(Events);
}
