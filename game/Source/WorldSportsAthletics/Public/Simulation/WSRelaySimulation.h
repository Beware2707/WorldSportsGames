#pragma once

#include "CoreMinimal.h"
#include "Simulation/WSSprintSimulation.h" // FWSSprintAttributes

#include "WSRelaySimulation.generated.h"

/**
 * The relays — four legs and three baton exchanges, timed as one clock.
 *
 * Everything before this event turned on ONE decision or a rhythm held by
 * a single athlete. A relay adds the thing that actually decides relays:
 * the handover.
 *
 * - Each leg is a sprint. The first starts from blocks off a gun; the
 *   other three start at whatever speed the exchange preserved, which is
 *   why a relay team is faster than four individual runs added up.
 * - The baton must change hands INSIDE the takeover zone. Outside it is a
 *   disqualification, not a slow time — the same shape of rule as a false
 *   start, and modelled the same way.
 * - Passing at the right moment keeps almost all of the incoming runner's
 *   speed. Passing early or late costs it, and the outgoing runner has to
 *   build it again from a near standstill.
 *
 * Fatigue resets at every exchange because the next leg is a different
 * athlete. The ceiling is still ONE athlete's: a team is four runners of
 * the player's quality, so the server's attribute ceiling still bounds the
 * whole clock.
 *
 * No wind: World Athletics records wind for the 100m, the 200m and the
 * horizontal jumps. A relay is none of those, so reporting one would be
 * inventing a measurement.
 */

UENUM(BlueprintType)
enum class EWSRelayInputType : uint8
{
	/** Out of the blocks, first leg only. */
	Release,
	/** One stride of the cadence the leg is being judged on. */
	Tap,
	/** Hand the baton over. The one decision a relay turns on. */
	Pass
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSRelayInputEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relay")
	double TimeSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relay")
	EWSRelayInputType Type = EWSRelayInputType::Tap;
};

/** One relay, as data. Adding another is a ROW. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSRelayEventSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FString Code = TEXT("relay-4x100");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FString DisplayName = TEXT("4x100m Relay");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	int32 LegCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double LegMetres = 100.0;

	/**
	 * How long the takeover zone is, ending at the leg line. The baton
	 * changing hands anywhere else is a disqualification.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double TakeoverZoneMetres = 30.0;

	/** How far before the line the exchange ideally happens: near the end
	 * of the zone, with the outgoing runner already at speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double IdealPassBeforeLineMetres = 3.0;

	/** What share of the incoming runner's speed survives a perfect
	 * exchange, and a legal but badly judged one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double ExchangeKeepAtBest = 0.985;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double ExchangeKeepAtWorst = 0.55;

	// The server's model, mirrored. Lower is better here.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double CeilingAtZero = 64.00;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double CeilingAtHundred = 36.50;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MinPlausibleSeconds = 35.00;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MaxPlausibleSeconds = 120.00;

	/** Top speed in m/s at zero and at full governing attributes. Held per
	 * event rather than shared with the sprints, so a relay can be
	 * recalibrated without moving a 100m time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double SpeedAtZero = 6.80;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double SpeedAtHundred = 11.90;

	/**
	 * Above 1.0, because the server's ceiling is linear in the attribute
	 * mean while race time goes as 1/V: a linear speed curve dips UNDER
	 * that ceiling through the middle of the range, and a client that runs
	 * faster than the server allows shows a time it then refuses.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double TopSpeedCurve = 1.55;

	/** How hard a single LEG tires its runner. A 400m leg is a fatigue
	 * event; a 100m leg barely is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double FatigueScale = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double FatigueDepthScale = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double FatigueStaminaSpread = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	TArray<FName> GoverningAttributes;

	double TotalMetres() const { return LegCount * LegMetres; }

	/** Where leg `Index` (0-based) hands over, in metres. */
	double LegLineMetres(int32 Index) const { return (Index + 1) * LegMetres; }
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSRelayState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Relay") double RaceTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") double Distance = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") double Speed = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") double Fatigue = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") double CadenceAccuracy = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") double TargetCadenceHz = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") double ActualCadenceHz = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") bool bReleased = false;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") bool bFinished = false;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") bool bFalseStart = false;

	/** Which leg is being run, 1-based. */
	UPROPERTY(BlueprintReadOnly, Category = "Relay") int32 Leg = 1;

	/** Metres to the next leg line; negative past it. Zero on the last leg,
	 * where the line ahead is a finish rather than a handover. */
	UPROPERTY(BlueprintReadOnly, Category = "Relay") double MetresToHandover = 0.0;

	/** True inside the takeover zone — the only place a pass is legal. */
	UPROPERTY(BlueprintReadOnly, Category = "Relay") bool bInTakeoverZone = false;
};

USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSRelayOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Relay") bool bFinished = false;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") double TimeSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") double ReactionMs = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Relay") bool bFalseStart = false;

	/** One per leg. */
	UPROPERTY(BlueprintReadOnly, Category = "Relay") TArray<double> Splits;

	/**
	 * Disqualified for an exchange outside the takeover zone — the relay's
	 * own version of a false start, and just as final. A team that drops
	 * the baton does not get a slow time; it gets no time.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Relay") bool bBadExchange = false;

	/** Which handover went wrong, 1-based; 0 if none did. */
	UPROPERTY(BlueprintReadOnly, Category = "Relay") int32 BadExchangeLeg = 0;

	/** How well each handover went, 0 = scraped through, 1 = on the mark. */
	UPROPERTY(BlueprintReadOnly, Category = "Relay") TArray<double> ExchangeQuality;
};

namespace WSRelayEvents
{
/** The relays table. Adding one is a ROW. */
WORLDSPORTSATHLETICS_API const TArray<FWSRelayEventSpec>& All();

WORLDSPORTSATHLETICS_API const FWSRelayEventSpec& Find(const FString& Code);
}

class WORLDSPORTSATHLETICS_API FWSRelaySimulation
{
public:
	static constexpr double StepHz = 120.0;
	static constexpr double StepDt = 1.0 / StepHz;
	static constexpr double FalseStartFloorMs = 100.0; // mirrors the server

	FWSRelaySimulation(const FWSSprintAttributes& InAttributes, uint32 InSeed,
		const FWSRelayEventSpec& InEventSpec);

	const FWSRelayEventSpec& GetEvent() const { return EventSpec; }

	/** Target cadence for the leg being run at a given point on it. */
	double TargetCadenceAt(double LegDistanceMetres) const;

	void AddInput(const FWSRelayInputEvent& Event);
	bool Step();

	const FWSRelayState& GetState() const { return State; }
	FWSRelayOutcome GetOutcome() const { return Outcome; }

	static FWSRelayOutcome RunTrace(const FWSSprintAttributes& Attributes, uint32 Seed,
		const TArray<FWSRelayInputEvent>& Trace, const FWSRelayEventSpec& EventSpec);

	static FString DigestTrace(const TArray<FWSRelayInputEvent>& Trace);

	/**
	 * A relay run by a team of a given quality. Consistency 1.0 holds the
	 * cadence and hands over on the mark; lower drifts on both, and low
	 * enough hands over outside the zone — which is a disqualification,
	 * exactly as it is on a real track.
	 */
	static TArray<FWSRelayInputEvent> GenerateAITrace(
		const FWSSprintAttributes& Attributes, uint32 RaceSeed, uint32 InputSeed,
		double ReactionMeanMs, double ReactionSpreadMs, double Consistency,
		const FWSRelayEventSpec& EventSpec);

private:
	void ApplyEvent(const FWSRelayInputEvent& Event);
	void Handover(double PassDistance);

	/** When the athlete left the blocks, in seconds after the gun. The legs
	 * are measured from here because they are RUNNING times. */
	double ReactionSeconds() const { return Outcome.ReactionMs / 1000.0; }

	FWSSprintAttributes Attributes;
	FWSRelayEventSpec EventSpec;
	FWSRelayState State;
	FWSRelayOutcome Outcome;

	double GunTime = 0.0;
	double LastTapTime = -1.0;
	double PrevTapInterval = 0.0;
	double LastLegTime = 0.0;
	/** Set when a pass has been asked for on this leg. */
	bool bPassRequested = false;
	/** When the baton last changed hands. Starts far enough in the past
	 * that the first handover is never treated as a duplicate. */
	double LastHandoverTime = -1000.0;

	TArray<FWSRelayInputEvent> PendingEvents;
	int32 NextEventIndex = 0;
};
