#pragma once

#include "CoreMinimal.h"

#include "WSSprintSimulation.generated.h"

/**
 * Deterministic 100m sprint simulation.
 *
 * The whole competitive loop hangs on three properties:
 *
 * 1. DETERMINISM — identical (attributes, input trace, seed) must reproduce
 *    the identical time, split for split. Fixed-step integration only; no
 *    frame dt, no unseeded randomness. This is what lets the server replay
 *    a submitted result and what makes the finish replay exact.
 * 2. CADENCE ACCURACY, NEVER TAP COUNT — speed follows how close the tap
 *    rhythm is to the target cadence, so a mechanical clicker gains nothing
 *    (GAME_DESIGN.md §2 rejects taps/second explicitly).
 * 3. CEILING RESPECT — perfect play approaches but never beats the server's
 *    attribute ceiling (13.5s at 0-attrs → 9.55s at 100), so an honestly
 *    simulated result is never rejected by validation.
 *
 * The simulation is a plain struct — no UObject, no world. Player and AI
 * both feed it input traces; AI generates a trace from a difficulty profile
 * and runs through this exact code path (the brief forbids AI that teleports
 * to a predetermined time).
 */

UENUM(BlueprintType)
enum class EWSSprintInputType : uint8
{
	HoldStart,   // settle into blocks (must be before the gun)
	Release,     // leave the blocks — time vs gun = reaction
	Tap,         // one stride-rhythm tap
	Lean         // dip at the line (last metres only)
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSSprintInputEvent
{
	GENERATED_BODY()

	/** Seconds on the RACE clock: 0.0 is the gun. Pre-gun events are negative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	double TimeSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	EWSSprintInputType Type = EWSSprintInputType::Tap;
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSSprintAttributes
{
	GENERATED_BODY()

	/** All 0..100, matching the backend career attribute scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint") float Reaction = 40.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint") float Acceleration = 40.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint") float MaxSpeed = 40.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint") float StrideEfficiency = 40.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint") float Stamina = 40.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint") float Technique = 40.0f;

	/**
	 * Mean of the server's governing attributes for the 100m — the exact
	 * input its ceiling formula uses (backend services/career.py:
	 * attribute_ceiling over ("reaction", "acceleration", "max_speed",
	 * "stride_efficiency", "stamina")).
	 *
	 * Top speed is derived from THIS mean rather than from MaxSpeed alone.
	 * Otherwise a lopsided athlete — max_speed 90, everything else 40 —
	 * simulates faster than the mean-based ceiling permits, and an honestly
	 * run race comes back rejected, which reads to the player as the game
	 * calling them a cheat.
	 */
	double GoverningMean() const
	{
		return (Reaction + Acceleration + MaxSpeed + StrideEfficiency + Stamina) / 5.0;
	}
};

/** Live state, stepped at a fixed rate; also the HUD's data source. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSSprintState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sprint") double RaceTime = 0.0;      // seconds since gun
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") double Distance = 0.0;      // metres
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") double Speed = 0.0;         // m/s
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") double Fatigue = 0.0;       // 0..1
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") double CadenceAccuracy = 0.0; // smoothed 0..1
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") double TargetCadenceHz = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") double ActualCadenceHz = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") bool bReleased = false;
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") bool bFinished = false;
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") bool bFalseStart = false;
};

/** Everything a finished (or disqualified) run reports. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSSprintOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sprint") bool bFalseStart = false;
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") bool bFinished = false;
	/** Official time: gun to torso, includes reaction. Millisecond precision. */
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") double TimeSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") double ReactionMs = 0.0;
	/** 10 segment durations; ReactionMs/1000 + sum == TimeSeconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") TArray<double> Splits;
	UPROPERTY(BlueprintReadOnly, Category = "Sprint") double Wind = 0.0;
};

class WORLDSPORTSATHLETICS_API FWSSprintSimulation
{
public:
	static constexpr double StepHz = 120.0;
	static constexpr double StepDt = 1.0 / StepHz;
	static constexpr double RaceDistance = 100.0;
	static constexpr double FalseStartFloorMs = 100.0; // mirrors the server

	FWSSprintSimulation(const FWSSprintAttributes& InAttributes, uint32 InSeed);

	/** Wind for this race, from the seed. Legal range biased: [-1.5, +2.0]. */
	double GetWind() const { return Wind; }

	/** Target cadence the player is asked to match at a given distance. */
	double TargetCadenceAt(double DistanceMetres) const;

	/** Feed one input event. Events must arrive in time order. */
	void AddInput(const FWSSprintInputEvent& Event);

	/** Advance one fixed step. Returns false once finished/false-started. */
	bool Step();

	/** Run to completion against a full input trace (server replay, AI, tests). */
	static FWSSprintOutcome RunTrace(const FWSSprintAttributes& Attributes,
		uint32 Seed, const TArray<FWSSprintInputEvent>& Trace);

	/** Deterministic digest of an input trace (audit breadcrumb). */
	static FString DigestTrace(const TArray<FWSSprintInputEvent>& Trace);

	/**
	 * AI: build a trace from a skill profile. The same simulation runs it.
	 *
	 * RaceSeed must be the seed of the race the trace will be REPLAYED in —
	 * the planning pass reads wind and band drift from it, and planning
	 * against different conditions than the athlete races in silently
	 * degrades their rhythm. InputSeed varies execution only.
	 */
	static TArray<FWSSprintInputEvent> GenerateAITrace(
		const FWSSprintAttributes& Attributes, uint32 RaceSeed, uint32 InputSeed,
		double ReactionMeanMs, double ReactionSpreadMs, double Consistency);

	const FWSSprintState& GetState() const { return State; }
	FWSSprintOutcome GetOutcome() const { return Outcome; }

private:
	void ApplyEvent(const FWSSprintInputEvent& Event);

	FWSSprintAttributes Attributes;
	FWSSprintState State;
	FWSSprintOutcome Outcome;
	double Wind = 0.0;
	double BandDriftPhase = 0.0; // seeded phase for the fatigue-zone drift

	TArray<FWSSprintInputEvent> PendingEvents; // time-ordered, not yet applied
	int32 NextEventIndex = 0;

	double LastTapTime = -1.0;
	double PrevTapInterval = 0.0;
	double ReactionSeconds = -1.0;
	bool bHeld = false;
	bool bLeanUsed = false;
	double LeanBonusMetres = 0.0;
	int32 NextSplitMark = 1; // 10m marks 1..10
	double LastSplitTime = 0.0;
};
