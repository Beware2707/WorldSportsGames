#pragma once

#include "CoreMinimal.h"

#include "WSSprintEvents.generated.h"

/**
 * An event as DATA.
 *
 * The roadmap's Phase 5 checkpoint: adding the 200m must not require new
 * gameplay code. It did — race distance was a compile-time constant, splits
 * were hardcoded to ten 10m marks, and the cadence curve used absolute
 * metres — so the framework was wrong and this is the fix. Everything that
 * differs between one timed running event and another now lives here, and
 * adding the 400m is a row in the table below.
 *
 * The ceilings and split counts MIRROR the server (backend
 * app/services/career.py EVENTS). They must stay in sync: the server
 * validates every submitted time against its own copy, so a client that
 * simulates to a different ceiling produces honest runs the server rejects.
 */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSSprintEventSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FString Code = TEXT("sprint-100m");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	FString DisplayName = TEXT("100m");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double DistanceMetres = 100.0;

	/** Splits reported to the server; segment length = Distance / this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	int32 SplitCount = 10;

	// --- The server's other validation limits, mirrored ----------------
	// A simulation that produces a time or a split the validator refuses is
	// a race the player ran and does not get to keep.

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MinSplitSeconds = 0.75;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MinPlausibleSeconds = 9.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double MaxPlausibleSeconds = 60.0;

	/** The server's ceiling model, mirrored so the client can assert it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double CeilingAtZero = 13.5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double CeilingAtHundred = 9.55;

	// --- Cadence profile, in FRACTIONS of the race ---------------------
	// Fractions, not metres: the same profile then describes a 100m and a
	// 400m without a second code path.

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double DriveEndFraction = 0.30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double FatigueStartFraction = 0.85;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double DriveCadenceHz = 3.2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double HoldCadenceHz = 4.6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double FatigueCadenceHz = 4.45;

	/**
	 * Exponent mapping the governing-attribute mean to top speed.
	 *
	 * The server's ceiling is LINEAR in that mean while race time goes as
	 * 1/speed, so a linear speed curve sags under the ceiling chord in the
	 * middle of the attribute range — and the longer the event, the wider
	 * the sag. Per event, because the sag is per event.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double TopSpeedCurve = 1.22;

	/**
	 * Top speed for THIS event as a fraction of the athlete's flat-out
	 * sprint speed. Nobody runs a lap at their 100m top end, and pretending
	 * they do is what let a maxed 400m runner finish under the ceiling the
	 * server holds them to.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double TopSpeedScale = 1.0;

	/** How FAST fatigue accumulates. The 400m is a fatigue event; the 100m
	 * barely is, and this is the single number that says so. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double FatigueScale = 1.0;

	/**
	 * How DEEP the fatigue penalty goes, as a multiple of the base.
	 *
	 * Rate alone cannot express a long event: over 400m fatigue saturates
	 * early and then every athlete carries the same flat penalty, which is
	 * exactly why a maxed-stamina athlete and a zero-stamina one finished
	 * within a second of each other. Depth is what makes the long events
	 * cost more than the short ones once everyone is already exhausted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double FatigueDepthScale = 1.0;

	/**
	 * How much a LOW stamina attribute deepens that penalty (0 = not at
	 * all). This is where stamina earns its place in the one-lap event: it
	 * cannot make you faster than the ceiling, it stops the distance from
	 * taking the speed you already have.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double FatigueStaminaSpread = 0.0;

	/** The attributes the SERVER averages for this event's ceiling. The
	 * simulation must average the same ones or a lopsided athlete runs a
	 * time the validator refuses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	TArray<FName> GoverningAttributes;

	/** Where the lean is allowed, as a fraction of the race. */
	double LeanWindowFraction = 0.95;

	// --- Hurdles -------------------------------------------------------
	// A hurdles race is NOT a new event kind: it keeps the blocks, the
	// reaction, the wind and the cadence band. What it adds is a second
	// skill on top of the rhythm — every barrier has to be taken off for
	// at the right moment — so it lives here as three numbers rather than
	// as a parallel simulation.

	/** 0 means a flat race. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	int32 HurdleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double FirstHurdleMetres = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	double HurdleSpacingMetres = 0.0;

	bool HasHurdles() const { return HurdleCount > 0; }

	/** Where the Index-th barrier stands, in metres from the start. */
	double HurdleMetres(int32 Index) const
	{
		return FirstHurdleMetres + HurdleSpacingMetres * Index;
	}
};

namespace WSSprintEvents
{
/** The event table. Adding a timed running event is a ROW here. */
WORLDSPORTSATHLETICS_API const TArray<FWSSprintEventSpec>& All();

/** Look up by the server's event code; falls back to the 100m. */
WORLDSPORTSATHLETICS_API const FWSSprintEventSpec& Find(const FString& Code);
}
