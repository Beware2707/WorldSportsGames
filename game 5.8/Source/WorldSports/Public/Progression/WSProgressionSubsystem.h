#pragma once

#include "CoreMinimal.h"
#include "Online/WSDtos.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WSProgressionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWSOnProgressionChanged);

/**
 * DISPLAY MIRROR of server-owned progression. Every number here came out of
 * a backend response; nothing in the client may compute XP, levels or PBs
 * and push them into this state. If a value looks wrong here, the fix is on
 * the server, never a client-side adjustment.
 */
UCLASS()
class WORLDSPORTS_API UWSProgressionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintPure, Category = "WorldSports|Progression")
	int32 GetTotalXp() const { return TotalXp; }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Progression")
	FString GetCareerStage() const { return CareerStage; }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Progression")
	const FWSResultResponse& GetLastResult() const { return LastResult; }

	UPROPERTY(BlueprintAssignable, Category = "WorldSports|Progression")
	FWSOnProgressionChanged OnProgressionChanged;

private:
	UFUNCTION()
	void HandleResultSubmitted(const FWSResultResponse& Response, bool bAccepted);

	int32 TotalXp = 0;
	FString CareerStage;
	FWSResultResponse LastResult;
};
