#pragma once

#include "CoreMinimal.h"
#include "Simulation/WSSprintSimulation.h" // FWSSprintAttributes

#include "WSThrowSimulation.generated.h"

/**
 * The shot put — measured in metres like a jump, but thrown from a CIRCLE
 * rather than run at from a board.
 *
 * There is no approach and no takeoff to place. The athlete winds up in
 * place and the release is the whole event, which makes it a different
 * kind again:
 *
 * - POWER builds through the wind-up on a curve. It peaks and then falls
 *   away, because a throw held too long is a throw the athlete has already
 *   started to unwind from.
 * - RELEASING is a single decision, and it sets both how fast the shot
 *   leaves the hand and — through technique — the angle it leaves at.
 * - NOT releasing before the wind-up runs out is a foul: the athlete has
 *   carried the throw out of the circle. Like a jumper past the board,
 *   that is no mark rather than a short one.
 *
 * No reaction (there is nothing to react to) and no wind: World Athletics
 * records wind for the short sprints and the horizontal jumps, not for
 * throws, so reporting one would be inventing a measurement.
 */

UENUM(BlueprintType)
enum class EWSThrowInputType : uint8
{
	/** Let it go. The one decision the event turns on. */
	Release
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSThrowInputEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw")
	double TimeSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throw")
	EWSThrowInputType Type = EWSThrowInputType::Release;
};

/** One throwing event, as data. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSThrowEventSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FString Code = TEXT("throw-shot");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FString DisplayName = TEXT("Shot Put");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	int32 Attempts = 3;

	// The server's model, mirrored. Higher is better, so the ceiling is a
	// maximum and a legal mark sits at or below it.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double CeilingAtZero = 4.50;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double CeilingAtHundred = 23.00;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MinPlausibleMetres = 1.00;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MaxPlausibleMetres = 24.00;

	/** How long the wind-up lasts before the athlete carries it out of the
	 * circle. Releasing after this is a foul. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double WindUpSeconds = 2.30;

	/** Where in the wind-up the power peaks, as a fraction of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double PeakFraction = 0.72;

	/** Release speed in m/s at zero and at full governing attributes, when
	 * the release is perfectly timed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double SpeedAtZero = 6.60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double SpeedAtHundred = 14.20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double TopSpeedCurve = 0.72;

	/** Release angle in degrees at zero and full technique. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double AngleAtZeroTechnique = 28.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double AngleAtFullTechnique = 38.0;

	/** The shot leaves the hand above the ground, which is why a throw
	 * carries further than the textbook 45-degree range formula. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double ReleaseHeightMetres = 2.10;

	/**
	 * True when the throw is made from a CIRCLE — the shot and the discus.
	 * A javelin is not: it is thrown from a runway, over a foul arc, and
	 * the athlete who does not let go has overstepped the arc rather than
	 * carried it out of a circle they were never standing in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	bool bFromCircle = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	TArray<FName> GoverningAttributes;
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSThrowState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Throw") double RaceTime = 0.0;
	/** 0..1 across the wind-up; the HUD's power bar. */
	UPROPERTY(BlueprintReadOnly, Category = "Throw") double WindUp = 0.0;
	/** Fraction of this athlete's best release speed available right now. */
	UPROPERTY(BlueprintReadOnly, Category = "Throw") double Power = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Throw") bool bReleased = false;
	UPROPERTY(BlueprintReadOnly, Category = "Throw") bool bFinished = false;
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSThrowOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Throw") bool bFinished = false;
	/** The measured mark in metres. Zero on a foul: there is no mark. */
	UPROPERTY(BlueprintReadOnly, Category = "Throw") double DistanceMetres = 0.0;
	/** No mark. Either the athlete never let go and carried it out of the
	 * circle, or the throw fell short of a mark the sport records. */
	UPROPERTY(BlueprintReadOnly, Category = "Throw") bool bFoul = false;
	/** True when the foul was carrying it out rather than a short throw. */
	UPROPERTY(BlueprintReadOnly, Category = "Throw") bool bCarriedOut = false;
	UPROPERTY(BlueprintReadOnly, Category = "Throw") double ReleaseSpeed = 0.0;
	/** How far from the peak of the wind-up the release came, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Throw") double TimingError = 0.0;
};

namespace WSThrowEvents
{
/** The throws table. Adding one is a ROW. */
WORLDSPORTSATHLETICS_API const TArray<FWSThrowEventSpec>& All();

WORLDSPORTSATHLETICS_API const FWSThrowEventSpec& Find(const FString& Code);
}

class WORLDSPORTSATHLETICS_API FWSThrowSimulation
{
public:
	static constexpr double StepHz = 120.0;
	static constexpr double StepDt = 1.0 / StepHz;

	FWSThrowSimulation(const FWSSprintAttributes& InAttributes, uint32 InSeed,
		const FWSThrowEventSpec& InEventSpec);

	const FWSThrowEventSpec& GetEvent() const { return EventSpec; }

	void AddInput(const FWSThrowInputEvent& Event);
	bool Step();

	const FWSThrowState& GetState() const { return State; }
	FWSThrowOutcome GetOutcome() const { return Outcome; }

	static FWSThrowOutcome RunTrace(const FWSSprintAttributes& Attributes, uint32 Seed,
		const TArray<FWSThrowInputEvent>& Trace, const FWSThrowEventSpec& EventSpec);

	static FString DigestTrace(const TArray<FWSThrowInputEvent>& Trace);

	/**
	 * A throw by an athlete of a given quality. Consistency 1.0 releases at
	 * the peak of the wind-up; lower drifts either side of it, and low
	 * enough never lets go at all — which is a foul, exactly as it is in
	 * the circle.
	 */
	static TArray<FWSThrowInputEvent> GenerateAITrace(
		const FWSSprintAttributes& Attributes, uint32 RaceSeed, uint32 InputSeed,
		double Consistency, const FWSThrowEventSpec& EventSpec);

private:
	void ApplyEvent(const FWSThrowInputEvent& Event);

	FWSSprintAttributes Attributes;
	FWSThrowEventSpec EventSpec;
	FWSThrowState State;
	FWSThrowOutcome Outcome;

	double BestSpeed = 0.0;
	double GoverningFraction = 0.0;
	double PeakDriftPhase = 0.0;

	TArray<FWSThrowInputEvent> PendingEvents;
	int32 NextEventIndex = 0;
};
