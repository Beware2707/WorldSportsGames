#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "WSDifficultyProfile.generated.h"

/** One AI difficulty tier. These shape the AI's INPUT (reaction, effort
 * consistency) — never its output time, which must emerge from the shared
 * simulation like the player's does. */
UCLASS(BlueprintType)
class WORLDSPORTS_API UWSDifficultyProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI")
	FText DisplayName;

	/** Mean simulated reaction, milliseconds. Must stay >= the false-start
	 * floor or the AI disqualifies itself. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI", meta = (ClampMin = "100"))
	float ReactionMeanMs = 180.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI", meta = (ClampMin = "0"))
	float ReactionSpreadMs = 40.0f;

	/** 0..1 how close to optimal the AI holds its effort. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI", meta = (ClampMin = "0", ClampMax = "1"))
	float EffortConsistency = 0.7f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("WSDifficulty"), GetFName());
	}
};
