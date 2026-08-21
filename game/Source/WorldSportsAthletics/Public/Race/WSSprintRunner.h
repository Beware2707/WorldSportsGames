#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Simulation/WSPaceSimulation.h"
#include "Simulation/WSSprintSimulation.h"

#include "WSSprintRunner.generated.h"

class UStaticMeshComponent;

/**
 * One athlete in the race — player or AI, identical class, and now any
 * running event rather than only a sprint. Each owns a simulation and is
 * driven ONLY by stepping it; nothing may write a position or a time
 * directly. The visual is a placeholder capsule body with a procedural
 * stride bob until character art exists.
 *
 * The class keeps its Sprint name for now: renaming the files would touch
 * the track, game mode, HUD and every test for no behavioural gain, and
 * the rename is worth doing in one deliberate pass rather than smuggled
 * into an event change.
 */
UCLASS()
class WORLDSPORTSATHLETICS_API AWSSprintRunner : public AActor
{
	GENERATED_BODY()

public:
	AWSSprintRunner();

	/** Create this runner's simulation. Call before the race starts. */
	void InitializeRace(const FWSSprintAttributes& InAttributes, uint32 Seed,
		int32 InLaneIndex, const FString& InDisplayName, bool bInIsPlayer,
		const FWSSprintEventSpec& InEventSpec);

	/** Create this runner's MIDDLE-DISTANCE simulation instead. Exactly one
	 * of the two Initialize calls applies to any given runner. */
	void InitializePaceRace(const FWSSprintAttributes& InAttributes, uint32 Seed,
		int32 InLaneIndex, const FString& InDisplayName, bool bInIsPlayer,
		const FWSPaceEventSpec& InPaceSpec);

	/** Queue an input event (player taps, or a pre-generated AI trace). */
	void PushInput(const FWSSprintInputEvent& Event);
	void PushTrace(const TArray<FWSSprintInputEvent>& Trace);

	/** The middle-distance equivalents: effort changes and the kick. */
	void PushPaceInput(const FWSPaceInputEvent& Event);
	void PushPaceTrace(const TArray<FWSPaceInputEvent>& Trace);

	bool IsPaceEvent() const { return PaceSimulation.IsValid(); }

	/** Live pace state, for the HUD's effort and energy readouts. Only
	 * meaningful when IsPaceEvent(). */
	const FWSPaceState& GetPaceState() const;

	/**
	 * Run to a time the SERVER already decided (a tournament rival).
	 *
	 * This is the one case where a runner is not simulated, and it is the
	 * honest one: the server generated and stored this field before the
	 * race, and it scores the player against those exact times. Simulating
	 * the rival instead would put a different race on screen from the one
	 * being scored. It is never used for the player, whose time must always
	 * emerge from play.
	 */
	void SetScriptedFinish(double FinishTimeSeconds);

	bool IsScripted() const { return ScriptedFinishSeconds > 0.0; }

	/** Metres to the next barrier, or -1 when there is none ahead. */
	double MetresToNextHurdle() const
	{
		if (!Simulation.IsValid())
		{
			return -1.0;
		}
		const double Barrier = Simulation->NextHurdleMetres();
		return Barrier < 0.0 ? -1.0 : Barrier - Simulation->GetState().Distance;
	}

	/** Barriers this run has hit badly. Feedback only, never a claim. */
	int32 GetHurdlesClattered() const
	{
		return Simulation.IsValid() ? Simulation->GetHurdlesClattered() : 0;
	}

	/** Metres between this event's split marks. */
	double GetSplitSegmentMetres() const;

	/** The distance this runner is racing, in metres. */
	double GetRaceDistance() const;

	/** Advance the simulation to RaceTime and update the visual. */
	void AdvanceTo(double RaceTime);

	const FWSRaceState& GetState() const;
	FWSRaceOutcome GetOutcome() const;

	/** Only sprints have a cadence band; middle distance has none. */
	double TargetCadenceAt(double Distance) const
	{
		return Simulation.IsValid() ? Simulation->TargetCadenceAt(Distance) : 0.0;
	}

	bool IsPlayer() const { return bIsPlayer; }
	int32 GetLaneIndex() const { return LaneIndex; }
	const FString& GetDisplayName() const { return DisplayName; }
	bool HasFinished() const { return GetState().bFinished; }
	bool HasFalseStarted() const { return GetState().bFalseStart; }

private:
	void UpdateVisual(double RaceTime);
	void ApplyKitColour();
	void ProjectPaceState();

	/** Exactly one of these is valid: the event decides which. */
	TSharedPtr<FWSSprintSimulation> Simulation;
	TSharedPtr<FWSMiddleDistanceSimulation> PaceSimulation;
	double SimulatedTime = 0.0;

	/** Middle-distance state projected into the shared race state, so the
	 * HUD, camera and standings have one shape to read. */
	FWSRaceState PaceProjectedState;

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
	/** >0 when this runner replays a server-decided finish time. */
	double ScriptedFinishSeconds = 0.0;
	double ScriptedDistanceMetres = 100.0;
	FWSRaceOutcome ScriptedOutcome;
	FWSRaceState ScriptedState;
};
