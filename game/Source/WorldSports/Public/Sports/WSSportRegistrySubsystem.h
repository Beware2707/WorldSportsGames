#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WSSportRegistrySubsystem.generated.h"

class UWSEventDefinition;
class UWSSportDefinition;

/**
 * Resolves sport/event definition assets by id. The scan is lazy: content
 * may still be loading during subsystem initialization, so the first lookup
 * waits for the asset registry instead of racing it.
 */
UCLASS()
class WORLDSPORTS_API UWSSportRegistrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Find the event definition whose EventCode matches, e.g. "sprint-100m". */
	UFUNCTION(BlueprintCallable, Category = "WorldSports|Sports")
	UWSEventDefinition* ResolveEvent(const FString& EventCode);

	UFUNCTION(BlueprintCallable, Category = "WorldSports|Sports")
	TArray<UWSSportDefinition*> GetAllSports();

	UFUNCTION(BlueprintCallable, Category = "WorldSports|Sports")
	TArray<UWSEventDefinition*> GetAllEvents();

private:
	void EnsureScanned();

	bool bScanned = false;

	UPROPERTY()
	TArray<TObjectPtr<UWSEventDefinition>> Events;

	UPROPERTY()
	TArray<TObjectPtr<UWSSportDefinition>> Sports;
};
