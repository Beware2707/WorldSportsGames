#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "WSSportDefinition.generated.h"

class UWSEventDefinition;

/** A sport groups events ("athletics" → 100m, 200m, long jump ...). */
UCLASS(BlueprintType)
class WORLDSPORTS_API UWSSportDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sport")
	FName SportId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sport")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sport")
	TArray<TSoftObjectPtr<UWSEventDefinition>> Events;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("WSSport"), GetFName());
	}
};
