#pragma once

#include "CoreMinimal.h"
#include "Simulation/WSSprintSimulation.h" // FWSSprintAttributes

#include "WSJumpSimulation.generated.h"

/**
 * The long jump — the first event measured in METRES rather than seconds,
 * and the first where a bigger number is a better result.
 *
 * A third event kind, and it earns that by being three things in sequence
 * rather than one thing repeated:
 *
 * 1. An APPROACH, which is a sprint down a runway and reuses the cadence
 *    skill: how fast you arrive is most of how far you go.
 * 2. A TAKEOFF, judged against a board. The mark is measured from the
 *    BOARD, not from where the foot actually left the ground, so every
 *    centimetre short of it is a centimetre off the jump — and a single
 *    centimetre past it is a foul and no mark at all. That asymmetry is
 *    the whole tension of the event.
 * 3. A FLIGHT, which is projectile physics the player no longer controls.
 *
 * There is no reaction: a jumper starts their approach when they choose to,
 * so there is no gun and no false start. Wind IS measured and the +2.0 m/s
 * limit applies, exactly as in the sprints.
 */

UENUM(BlueprintType)
enum class EWSJumpInputType : uint8
{
	/** One stride of the approach, same rhythm skill as the sprint. */
	Tap,
	/** Leave the ground. Where this lands relative to the board is the jump. */
	Takeoff
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSJumpInputEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump")
	double TimeSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump")
	EWSJumpInputType Type = EWSJumpInputType::Tap;
};

/** One jumping event, as data. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSJumpEventSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FString Code = TEXT("jump-long");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FString DisplayName = TEXT("Long Jump");

	/** Runway length; the board sits at the end of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double RunwayMetres = 40.0;

	/** Attempts in a round. The best legal mark is the result. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	int32 Attempts = 3;

	/**
	 * How many takeoffs the jump is made of. One for a long jump. THREE
	 * for a triple jump — a hop, a step and a jump — and that is the whole
	 * event: the approach buys the speed, but the rhythm of the three
	 * takeoffs decides how much of it survives to the sand.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	int32 PhaseCount = 1;

	/** What share of the whole jump each phase covers. Empty means one
	 * phase covering all of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	TArray<double> PhaseShares;

	/** How wide the window around each landing is, in seconds. A takeoff
	 * inside it carries the jump on; the further from the landing, the
	 * more of that phase is lost. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double PhaseWindowSeconds = 0.18;

	/** What a completely mistimed phase costs — a stumbled step, not a
	 * foul: the jumper carries on, just shorter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double PhaseMissPenalty = 0.30;

	/**
	 * A VERTICAL jump: the athlete clears a bar or does not, and the result
	 * is the highest bar cleared rather than a distance measured from a
	 * board.
	 *
	 * That changes the competition as well as the measurement. A horizontal
	 * jump is the best of three attempts; a vertical one is a ladder, where
	 * clearing raises the bar and three failures at a height ends the day.
	 * There is also no foul line to overstep — a high jumper fails by not
	 * clearing, which is a different thing from being disqualified.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	bool bVertical = false;

	/** Where the bar starts and how far it rises each time it is cleared. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double StartBarMetres = 1.00;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double BarIncrementMetres = 0.05;

	/** Failures at one height before the competition is over. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	int32 FailuresAllowed = 3;

	/** The bar this attempt is against; set per attempt by the game mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double BarMetres = 0.0;

	/** Height cleared at zero and at full governing attributes, with a
	 * perfectly judged takeoff. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double HeightAtZero = 1.02;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double HeightAtHundred = 2.44;

	/** How height scales between those endpoints. Kept at 1.0 because the
	 * server's ceiling is a straight line: any exponent below 1 bows the
	 * client's curve ABOVE that line in the middle, and a mark above the
	 * ceiling is one the server refuses after the player has watched it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double HeightCurve = 1.0;

	// The server's model, mirrored. Higher is better here, so the ceiling
	// is a maximum rather than a minimum.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double CeilingAtZero = 3.20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double CeilingAtHundred = 8.85;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MinPlausibleMetres = 1.00;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MaxPlausibleMetres = 9.00;

	/** Approach speed available at zero and at full governing attributes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double SpeedAtZero = 6.20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double SpeedAtHundred = 10.95;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double TopSpeedCurve = 1.22;

	/** Takeoff angle, in degrees, at zero and full technique. Getting this
	 * right is what technique buys in a jump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double AngleAtZeroTechnique = 13.5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double AngleAtFullTechnique = 21.5;

	/** Scales the projectile range to a real jump: a human is not a point
	 * mass, and take-off height plus the landing extension add to it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double FlightScale = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double LandingBonusMetres = 0.55;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	TArray<FName> GoverningAttributes;
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSJumpState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Jump") double RaceTime = 0.0;
	/** Distance along the runway; the board is at RunwayMetres. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double Distance = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double Speed = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double CadenceAccuracy = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double TargetCadenceHz = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double ActualCadenceHz = 0.0;
	/** Metres to the board; negative once past it (a foul if airborne). */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double MetresToBoard = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") bool bAirborne = false;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") bool bFinished = false;

	/** Which phase is in the air: 0 on the runway, then 1..PhaseCount for
	 * the hop, the step and the jump. Always 1 for a single-phase jump. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") int32 Phase = 0;

	/** Seconds until this phase lands. Negative once it has, while the
	 * window for the next takeoff is still open. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double PhaseTimeRemaining = 0.0;

	/** True while the next takeoff can be timed — the cue the player is
	 * reading, and the only moment a tap does anything. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") bool bPhaseWindowOpen = false;

	/** How high off the ground the athlete is, in metres. Presentational:
	 * the mark is decided at takeoff, but a jumper who never leaves the
	 * ground is a runner, and that is what the world was showing. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double HeightAboveGround = 0.0;
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSJumpOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Jump") bool bFinished = false;
	/** The measured mark in metres. Zero on a foul: there is no mark. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double DistanceMetres = 0.0;
	/**
	 * No mark. Either the athlete took off beyond the board, or the jump
	 * did not reach the pit at all — both are failed attempts, and neither
	 * is a jump of zero metres.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") bool bFoul = false;

	/** True when the no-mark was an overstep rather than a short landing.
	 * The player is owed the difference: one is greed, the other is a
	 * takeoff so early the jump never reached the sand. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") bool bOverstepped = false;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double TakeoffSpeed = 0.0;
	/** How far behind the board the foot left the ground. Every centimetre
	 * of it comes off the mark, because the mark is measured from the
	 * board. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double BoardGapMetres = 0.0;

	/** Vertical jumps only: the height reached, and whether the bar
	 * survived. A failure is not a foul — the athlete simply did not
	 * clear, and gets to try that height again. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double HeightMetres = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") bool bCleared = false;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double Wind = 0.0;

	/** Multi-phase jumps only: how far off each landing the takeoff into
	 * the next phase was, 0 = on it, 1 = missed the window entirely. The
	 * first entry is always 0 — the board takeoff is judged by the gap to
	 * the board, not by a landing that has not happened yet. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") TArray<double> PhaseErrors;
};

namespace WSJumpEvents
{
/** The jumps table. Adding one is a ROW. */
WORLDSPORTSATHLETICS_API const TArray<FWSJumpEventSpec>& All();

WORLDSPORTSATHLETICS_API const FWSJumpEventSpec& Find(const FString& Code);
}

class WORLDSPORTSATHLETICS_API FWSJumpSimulation
{
public:
	static constexpr double StepHz = 120.0;
	static constexpr double StepDt = 1.0 / StepHz;

	FWSJumpSimulation(const FWSSprintAttributes& InAttributes, uint32 InSeed,
		const FWSJumpEventSpec& InEventSpec);

	const FWSJumpEventSpec& GetEvent() const { return EventSpec; }
	double GetWind() const { return Wind; }

	/** Target cadence for the approach at a given point on the runway. */
	double TargetCadenceAt(double DistanceMetres) const;

	void AddInput(const FWSJumpInputEvent& Event);
	bool Step();

	const FWSJumpState& GetState() const { return State; }
	FWSJumpOutcome GetOutcome() const { return Outcome; }

	static FWSJumpOutcome RunTrace(const FWSSprintAttributes& Attributes, uint32 Seed,
		const TArray<FWSJumpInputEvent>& Trace, const FWSJumpEventSpec& EventSpec);

	static FString DigestTrace(const TArray<FWSJumpInputEvent>& Trace);

	/**
	 * A jump by an athlete of a given quality. Consistency 1.0 hits the
	 * board to the centimetre with a flawless approach; lower drifts off
	 * the rhythm and misjudges the board, which is exactly how the event is
	 * lost — including by fouling.
	 */
	static TArray<FWSJumpInputEvent> GenerateAITrace(
		const FWSSprintAttributes& Attributes, uint32 RaceSeed, uint32 InputSeed,
		double Consistency, const FWSJumpEventSpec& EventSpec);

private:
	void ApplyEvent(const FWSJumpInputEvent& Event);

	/** The whole jump's raw distance from the speed it left the board at,
	 * before the gap to the board and before any phase is mistimed. */
	double ComputeRawDistance() const;

	/** How high a vertical jump reaches. Known at takeoff, which is what
	 * lets the arc rise to it. */
	double ComputeVerticalHeight() const;

	/** Put the athlete into the air for a phase, and work out when it
	 * lands. */
	void BeginPhase(int32 PhaseIndex);

	/** Close the window on the phase in the air, bank what it covered, and
	 * either start the next one or land the jump. */
	void ResolvePhase();

	double PhaseShare(int32 PhaseIndex) const;
	void Land();

	FWSSprintAttributes Attributes;
	FWSJumpEventSpec EventSpec;
	FWSJumpState State;
	FWSJumpOutcome Outcome;

	double Wind = 0.0;
	double VMax = 0.0;

	/** Multi-phase jumps: the whole jump's raw distance, banked at takeoff
	 * so every phase is a share of one number rather than three guesses. */
	double RawTotal = 0.0;
	/** Metres banked by phases already completed. */
	double BankedMetres = 0.0;
	/**
	 * What the phase currently in the air is worth, 0..1, after the takeoff
	 * that launched it.
	 *
	 * A separate factor rather than a cut to RawTotal: shrinking the whole
	 * remaining total charged the miss to every LATER phase as well, and at
	 * a fraction of its size — a completely missed hop-to-step takeoff cost
	 * 5.8% of the jump instead of 9%, and a third of that came off the jump
	 * phase, which had been timed perfectly.
	 */
	double PhaseFactor = 1.0;
	/** When the phase in the air lands, and when its window shuts. */
	double PhaseLandingTime = 0.0;
	/** How long the phase in the air lasts, for the arc it draws. */
	double PhaseDuration = 0.0;
	/** Vertical jumps: the height this attempt will reach. */
	double PeakHeight = 0.0;
	/** The takeoff into the next phase, or -1 if it has not come. */
	double PhaseTapTime = -1.0;
	double GoverningFraction = 0.0;
	double BandDriftPhase = 0.0;

	TArray<FWSJumpInputEvent> PendingEvents;
	int32 NextEventIndex = 0;
	double LastTapTime = -1.0;
	double PrevTapInterval = 0.0;
};
