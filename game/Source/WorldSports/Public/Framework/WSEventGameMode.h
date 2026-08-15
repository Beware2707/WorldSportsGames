#pragma once

#include "CoreMinimal.h"
#include "Framework/WSEventPhase.h"
#include "Framework/WSGameModeBase.h"

#include "WSEventGameMode.generated.h"

class UWSEventDefinition;
class UWSEventRules;
struct FWSEventResult;

/**
 * Owns the sport-agnostic event lifecycle. One subclass per event KIND
 * (sprint, jump, match...), never per sport — the 200m must reuse the sprint
 * game mode with different data (roadmap §6 checkpoint).
 */
UCLASS()
class WORLDSPORTS_API AWSEventGameMode : public AWSGameModeBase
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintPure, Category = "WorldSports|Event")
	EWSEventPhase GetPhase() const;

	/** Advance to the next lifecycle phase if the current one is complete. */
	UFUNCTION(BlueprintCallable, Category = "WorldSports|Event")
	void AdvancePhase();

	UFUNCTION(BlueprintPure, Category = "WorldSports|Event")
	UWSEventDefinition* GetEventDefinition() const { return EventDefinition; }

protected:
	/** May the lifecycle leave `Phase` yet? Subclasses gate e.g. Active until
	 * every athlete has finished. */
	virtual bool CanLeavePhase(EWSEventPhase Phase) const { return true; }

	/** Hook for phase-entry work (start the clock, fire the gun...). */
	virtual void OnPhaseEntered(EWSEventPhase NewPhase);

	/** Called by the finishing logic: persist locally, submit, feed Reward. */
	void SubmitLocalResult(const FWSEventResult& Result);

	void SetPhase(EWSEventPhase NewPhase);

	/** Which event this level is running; resolved from the definition's code
	 * or set by the travel options. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldSports|Event")
	TObjectPtr<UWSEventDefinition> EventDefinition;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports|Event")
	TObjectPtr<UWSEventRules> Rules;
};
