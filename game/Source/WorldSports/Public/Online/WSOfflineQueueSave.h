#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "WSOfflineQueueSave.generated.h"

/** Disk form of the pending result submissions. Each entry is the exact
 * ResultIn JSON that will be POSTed, so what was simulated is what is sent
 * even across an app restart. */
UCLASS()
class WORLDSPORTS_API UWSOfflineQueueSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FString> SerializedResults;
};
