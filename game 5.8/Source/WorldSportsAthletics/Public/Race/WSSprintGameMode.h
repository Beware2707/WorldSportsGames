#pragma once

#include "CoreMinimal.h"
#include "Framework/WSEventGameMode.h"
#include "Simulation/WSSprintSimulation.h"

#include "WSSprintGameMode.generated.h"

class AWSSprintRunner;
class AWSSprintTrack;
class UWSSprintHud;

/** One finisher, for the result screen. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSRaceStanding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Race") int32 Position = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Race") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Race") double TimeSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Race") double ReactionMs = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Race") bool bIsPlayer = false;
	UPROPERTY(BlueprintReadOnly, Category = "Race") bool bFalseStart = false;
};

/**
 * The 100m race. Sprint-specific ONLY in its input handling and camera
 * choreography; everything else runs on the sport-agnostic phase machine
 * in AWSEventGameMode, which is what lets the 200m be data.
 *
 * Race clock: negative before the gun (Set phase), 0.0 at the gun. Every
 * runner — player and AI alike — advances through its own simulation
 * against that single clock.
 */
UCLASS()
class WORLDSPORTSATHLETICS_API AWSSprintGameMode : public AWSEventGameMode
{
	GENERATED_BODY()

public:
	AWSSprintGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// -- Player input (routed from the controller) -----------------------
	void PlayerHold();
	void PlayerRelease();
	void PlayerTap();
	void PlayerLean();

	/** Start (or restart) a race. Safe to call from the result screen. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void StartRace();

	// -- HUD queries -----------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "Race")
	double GetRaceClock() const { return RaceClock; }

	UFUNCTION(BlueprintPure, Category = "Race")
	AWSSprintRunner* GetPlayerRunner() const { return PlayerRunner; }

	UFUNCTION(BlueprintPure, Category = "Race")
	int32 GetPlayerPosition() const;

	UFUNCTION(BlueprintPure, Category = "Race")
	const TArray<FWSRaceStanding>& GetStandings() const { return Standings; }

	/** Server's answer, once it arrives. Empty until then. */
	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetServerVerdict() const { return ServerVerdict; }

	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsAwaitingServer() const { return bAwaitingServer; }

protected:
	virtual bool CanLeavePhase(EWSEventPhase Phase) const override;
	virtual void OnPhaseEntered(EWSEventPhase NewPhase) override;

private:
	void SpawnField();
	void UpdateCamera(float DeltaSeconds);
	void BuildStandings();
	void SubmitPlayerResult();
	FWSSprintAttributes ResolvePlayerAttributes() const;

	UPROPERTY()
	TObjectPtr<AWSSprintTrack> Track;

	UPROPERTY()
	TArray<TObjectPtr<AWSSprintRunner>> Runners;

	UPROPERTY()
	TObjectPtr<AWSSprintRunner> PlayerRunner;

	UPROPERTY()
	TObjectPtr<AActor> RaceCamera;

	UPROPERTY()
	TObjectPtr<UWSSprintHud> Hud;

	// Not "HudClass": AGameModeBase already has one (the AHUD class).
	UPROPERTY()
	TSubclassOf<UWSSprintHud> SprintHudClass;

	TArray<FWSSprintInputEvent> PlayerTrace; // submitted digest source
	TArray<FWSRaceStanding> Standings;

	double RaceClock = 0.0;
	uint32 RaceSeed = 0;
	bool bRaceRunning = false;
	bool bHolding = false;
	bool bAwaitingServer = false;
	FString ServerVerdict;
	float ResultDwell = 0.0f;
};
