#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "WSEventRules.generated.h"

/**
 * Designer tunables for one event, consumed by its game mode and simulation.
 * These shape the LOCAL simulation only; server-side validation has its own
 * copy of the physical limits and is the one that counts.
 */
UCLASS(BlueprintType)
class WORLDSPORTS_API UWSEventRules : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	FString EventCode;

	/** Race distance in metres; 0 for non-distance events. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	float DistanceMeters = 0.0f;

	/** Reaction faster than this is a false start (matches the server's floor). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	float FalseStartFloorMs = 100.0f;

	/** Named tuning scalars for the event simulation (curves come later as
	 * typed assets; scalars cover Phase 1 needs without schema churn). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	TMap<FName, float> Tunables;

	UFUNCTION(BlueprintPure, Category = "Rules")
	float GetTunable(FName Key, float Default) const
	{
		if (const float* Found = Tunables.Find(Key))
		{
			return *Found;
		}
		return Default;
	}

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("WSEventRules"), GetFName());
	}
};
