#pragma once

#include "CoreMinimal.h"
#include "Framework/WSEventPhase.h"
#include "GameFramework/GameStateBase.h"

#include "WSGameStateBase.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWSOnPhaseChanged, EWSEventPhase, OldPhase, EWSEventPhase, NewPhase);

/** Replicated event state: the current lifecycle phase and the event clock. */
UCLASS()
class WORLDSPORTS_API AWSGameStateBase : public AGameStateBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "WorldSports|Event")
	EWSEventPhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Event")
	float GetEventClockSeconds() const { return EventClockSeconds; }

	UPROPERTY(BlueprintAssignable, Category = "WorldSports|Event")
	FWSOnPhaseChanged OnPhaseChanged;

	/** Authority only — called by the event game mode. */
	void SetPhase(EWSEventPhase NewPhase);
	void SetEventClockSeconds(float Seconds) { EventClockSeconds = Seconds; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void OnRep_Phase(EWSEventPhase OldPhase);

	UPROPERTY(ReplicatedUsing = OnRep_Phase)
	EWSEventPhase Phase = EWSEventPhase::Load;

	UPROPERTY(Replicated)
	float EventClockSeconds = 0.0f;
};
