#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sports/Scoring/WSScoringStrategy.h"

#include "WSEventDefinition.generated.h"

class UWSEventRules;
class UWorld;

/**
 * One competitive event ("sprint-100m"), as a data asset. Adding an event of
 * an existing kind must be data-only — if a new event needs C++ in the core,
 * the framework is wrong (roadmap §6 checkpoint).
 *
 * EventCode must match the backend's career event catalogue exactly; the
 * registry treats it as the join key between client content and server rules.
 */
UCLASS(BlueprintType)
class WORLDSPORTS_API UWSEventDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Backend event code, e.g. "sprint-100m". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Event")
	FString EventCode;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Event")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Event")
	EWSValueKind ValueKind = EWSValueKind::Time;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Event")
	FString Unit = TEXT("s");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Event")
	int32 SplitsExpected = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Event")
	bool bRequiresReaction = false;

	/** The playable level for this event. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Event")
	TSoftObjectPtr<UWorld> Level;

	/** Designer tunables consumed by the event game mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Event")
	TSoftObjectPtr<UWSEventRules> Rules;

	const UWSScoringStrategy* GetScoring() const { return UWSScoringStrategy::Resolve(ValueKind); }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("WSEvent"), GetFName());
	}
};
