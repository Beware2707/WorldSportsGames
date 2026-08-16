#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Simulation/WSSprintSimulation.h"

#include "WSSprintRunner.generated.h"

class UStaticMeshComponent;

/**
 * One athlete in the race — player or AI, identical class. Each owns a
 * simulation and is driven ONLY by stepping it; nothing may write a
 * position or a time directly. The visual is a placeholder capsule body
 * with a procedural stride bob until character art exists.
 */
UCLASS()
class WORLDSPORTSATHLETICS_API AWSSprintRunner : public AActor
{
	GENERATED_BODY()

public:
	AWSSprintRunner();

	/** Create this runner's simulation. Call before the race starts. */
	void InitializeRace(const FWSSprintAttributes& InAttributes, uint32 Seed,
		int32 InLaneIndex, const FString& InDisplayName, bool bInIsPlayer);

	/** Queue an input event (player taps, or a pre-generated AI trace). */
	void PushInput(const FWSSprintInputEvent& Event);
	void PushTrace(const TArray<FWSSprintInputEvent>& Trace);

	/** Advance the simulation to RaceTime and update the visual. */
	void AdvanceTo(double RaceTime);

	const FWSSprintState& GetState() const { return Simulation->GetState(); }
	FWSSprintOutcome GetOutcome() const { return Simulation->GetOutcome(); }
	double TargetCadenceAt(double Distance) const { return Simulation->TargetCadenceAt(Distance); }

	bool IsPlayer() const { return bIsPlayer; }
	int32 GetLaneIndex() const { return LaneIndex; }
	const FString& GetDisplayName() const { return DisplayName; }
	bool HasFinished() const { return GetState().bFinished; }
	bool HasFalseStarted() const { return GetState().bFalseStart; }

private:
	void UpdateVisual(double RaceTime);

	TSharedPtr<FWSSprintSimulation> Simulation;
	double SimulatedTime = 0.0;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Head;

	UPROPERTY()
	TObjectPtr<class UMaterial> BodyMaterial;

	int32 LaneIndex = 0;
	FString DisplayName;
	bool bIsPlayer = false;
	double StridePhase = 0.0;
};
