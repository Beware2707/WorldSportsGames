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
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSJumpOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Jump") bool bFinished = false;
	/** The measured mark in metres. Zero on a foul: there is no mark. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double DistanceMetres = 0.0;
	/** Took off beyond the board — no mark, however far it went. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") bool bFoul = false;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double TakeoffSpeed = 0.0;
	/** How far behind the board the foot left the ground. Every centimetre
	 * of it comes off the mark, because the mark is measured from the
	 * board. */
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double BoardGapMetres = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Jump") double Wind = 0.0;
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
	void Land();

	FWSSprintAttributes Attributes;
	FWSJumpEventSpec EventSpec;
	FWSJumpState State;
	FWSJumpOutcome Outcome;

	double Wind = 0.0;
	double VMax = 0.0;
	double GoverningFraction = 0.0;
	double BandDriftPhase = 0.0;

	TArray<FWSJumpInputEvent> PendingEvents;
	int32 NextEventIndex = 0;
	double LastTapTime = -1.0;
	double PrevTapInterval = 0.0;
};
