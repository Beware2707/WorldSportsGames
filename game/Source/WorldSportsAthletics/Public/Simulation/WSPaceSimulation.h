#pragma once

#include "CoreMinimal.h"
#include "Simulation/WSSprintSimulation.h" // FWSSprintAttributes: the ATHLETE's
                                           // attributes, not sprint-only ones

#include "WSPaceSimulation.generated.h"

/**
 * Middle distance: the 800m and the 1500m.
 *
 * This is a new event KIND, not a longer sprint, and the roadmap is explicit
 * that a new kind is real work while a new event within a kind should be
 * near-free. Three things make it a different kind:
 *
 * 1. No blocks, so no reaction to measure. The server's row says so
 *    (requires_reaction=False) and nothing here invents one.
 * 2. No wind. World Athletics does not rank marks beyond 200m by wind, so
 *    reporting one would be inventing a measurement the sport does not take.
 * 3. The skill is PACE JUDGEMENT against a finite energy budget, not stride
 *    cadence. Tapping a rhythm for four minutes is not a game; deciding how
 *    hard to run, and when to spend what is left, is the actual sport.
 *
 * What carries over unchanged is the part that makes results trustworthy:
 * fixed-step deterministic integration, a seeded race, and a ceiling taken
 * from the mean of the SERVER's governing attributes — so an honestly run
 * race is never rejected by validation.
 */

UENUM(BlueprintType)
enum class EWSPaceInputType : uint8
{
	/** Set the effort the athlete is trying to hold (0..1). */
	SetEffort,
	/** Spend everything that is left. Only pays inside the kick window. */
	Kick
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSPaceInputEvent
{
	GENERATED_BODY()

	/** Seconds on the race clock; 0.0 is the gun. There is no pre-gun
	 * phase — a standing start has nothing to hold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pace")
	double TimeSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pace")
	EWSPaceInputType Type = EWSPaceInputType::SetEffort;

	/** Effort target for SetEffort; ignored by Kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pace")
	double Value = 0.0;
};

/** One middle-distance event, as data. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSPaceEventSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FString Code = TEXT("middle-800m");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FString DisplayName = TEXT("800m");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double DistanceMetres = 800.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	int32 SplitCount = 2;

	// The server's model, mirrored. Out of sync means honest runs rejected.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double CeilingAtZero = 150.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double CeilingAtHundred = 101.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MinSplitSeconds = 45.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MinPlausibleSeconds = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MaxPlausibleSeconds = 900.0;

	/**
	 * Top speed in m/s at zero and at full governing attributes.
	 *
	 * Given as endpoints rather than as a scale over the sprint's, because
	 * the attribute SPREAD is narrower here: the gap between a novice and a
	 * champion is far wider over 100m than over 1500m, and a single scale
	 * factor can only move both ends together.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double SpeedAtZero = 7.20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double SpeedAtHundred = 8.75;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double TopSpeedCurve = 1.22;

	/** How fast the energy budget burns above sustainable effort. The
	 * longer the race, the less it can afford. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double DrainRate = 0.055;

	/** Where the kick becomes legal, as a fraction of the race. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double KickWindowFraction = 0.82;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	TArray<FName> GoverningAttributes;
};

/** Live state. Also the HUD's data source. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSPaceState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Pace") double RaceTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Pace") double Distance = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Pace") double Speed = 0.0;
	/** Effort actually being held, 0..1 — what the player is asking for. */
	UPROPERTY(BlueprintReadOnly, Category = "Pace") double Effort = 0.0;
	/** Effort this athlete can hold indefinitely. The pace to judge. */
	UPROPERTY(BlueprintReadOnly, Category = "Pace") double SustainableEffort = 0.0;
	/** 1.0 fresh, 0.0 empty. Falls faster the harder you run. */
	UPROPERTY(BlueprintReadOnly, Category = "Pace") double Energy = 1.0;
	/** True once the budget is gone and the athlete is dying. */
	UPROPERTY(BlueprintReadOnly, Category = "Pace") bool bWalled = false;
	UPROPERTY(BlueprintReadOnly, Category = "Pace") bool bKicked = false;
	UPROPERTY(BlueprintReadOnly, Category = "Pace") bool bFinished = false;
};

/** Everything a finished run reports. No reaction, no wind — see above. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSPaceOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Pace") bool bFinished = false;
	UPROPERTY(BlueprintReadOnly, Category = "Pace") double TimeSeconds = 0.0;
	/** SplitCount segment durations; they sum to the official time. */
	UPROPERTY(BlueprintReadOnly, Category = "Pace") TArray<double> Splits;
	/** Whether the athlete ran out of energy before the line. Presentation
	 * and coaching feedback only — never sent as a performance claim. */
	UPROPERTY(BlueprintReadOnly, Category = "Pace") bool bWalled = false;
	/** Where the tank ran dry, as a fraction of the race; -1 if it never
	 * did. Emptying at the line after a kick is a finish; emptying at
	 * halfway is a collapse, and the two must be distinguishable. */
	UPROPERTY(BlueprintReadOnly, Category = "Pace") double WallAtFraction = -1.0;
};

namespace WSPaceEvents
{
/** The middle-distance table. Adding one is a ROW. */
WORLDSPORTSATHLETICS_API const TArray<FWSPaceEventSpec>& All();

/** Look up by the server's event code; falls back to the 800m. */
WORLDSPORTSATHLETICS_API const FWSPaceEventSpec& Find(const FString& Code);
}

class WORLDSPORTSATHLETICS_API FWSMiddleDistanceSimulation
{
public:
	static constexpr double StepHz = 120.0;
	static constexpr double StepDt = 1.0 / StepHz;

	FWSMiddleDistanceSimulation(const FWSSprintAttributes& InAttributes, uint32 InSeed,
		const FWSPaceEventSpec& InEventSpec);

	const FWSPaceEventSpec& GetEvent() const { return EventSpec; }
	double GetRaceDistance() const { return EventSpec.DistanceMetres; }

	/** Feed one input event. Events must arrive in time order. */
	void AddInput(const FWSPaceInputEvent& Event);

	/** Advance one fixed step. Returns false once finished. */
	bool Step();

	const FWSPaceState& GetState() const { return State; }
	FWSPaceOutcome GetOutcome() const { return Outcome; }

	/** Run to completion against a full input trace (replay, AI, tests). */
	static FWSPaceOutcome RunTrace(const FWSSprintAttributes& Attributes, uint32 Seed,
		const TArray<FWSPaceInputEvent>& Trace, const FWSPaceEventSpec& EventSpec);

	/** Deterministic digest of an input trace (audit breadcrumb). */
	static FString DigestTrace(const TArray<FWSPaceInputEvent>& Trace);

	/**
	 * The best race this athlete can run: the highest even effort that does
	 * not empty the tank before the line, plus a kick.
	 *
	 * Found by bisection over the effort, running this exact simulation each
	 * time — so "perfect play" is a real race someone could run, not a
	 * formula. That is what makes the ceiling calibration meaningful.
	 */
	static TArray<FWSPaceInputEvent> GeneratePerfectTrace(
		const FWSSprintAttributes& Attributes, uint32 Seed,
		const FWSPaceEventSpec& EventSpec);

	/**
	 * An AI rival's race. Consistency 1.0 judges the pace perfectly;
	 * lower drifts off it, which is exactly how the event is lost.
	 * Runs through this same simulation — never a scripted finish time.
	 */
	static TArray<FWSPaceInputEvent> GenerateAITrace(
		const FWSSprintAttributes& Attributes, uint32 RaceSeed, uint32 InputSeed,
		double Consistency, const FWSPaceEventSpec& EventSpec);

private:
	void ApplyEvent(const FWSPaceInputEvent& Event);

	FWSSprintAttributes Attributes;
	FWSPaceEventSpec EventSpec;
	FWSPaceState State;
	FWSPaceOutcome Outcome;

	double VMax = 0.0;
	double GoverningFraction = 0.0;
	double EffortTarget = 0.0;
	double WallFactor = 1.0;
	double KickReserve = 0.0;
	double PaceDriftPhase = 0.0;

	TArray<FWSPaceInputEvent> PendingEvents;
	int32 NextEventIndex = 0;
	int32 NextSplitMark = 1;
	double LastSplitTime = 0.0;
};
