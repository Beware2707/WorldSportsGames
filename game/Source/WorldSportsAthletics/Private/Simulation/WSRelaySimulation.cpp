#include "Simulation/WSRelaySimulation.h"

#include "Math/RandomStream.h"
#include "Misc/SecureHash.h"

namespace WSRelay
{
// A relay leg IS a sprint, and it uses the sprint's shape. The speed
// ENDPOINTS live on the event row instead of here, so a relay can be
// recalibrated without moving a 100m time.
constexpr double TauAt0 = 1.35;
constexpr double TauGain = 0.45;
constexpr double AccuracyFloor = 0.66;
constexpr double FatiguePenalty = 0.13;
constexpr double FatigueRate = 0.055;
constexpr double BaselineAccuracy = 0.25;
constexpr double AutoReleaseSeconds = 1.5;
constexpr double PreGunSeconds = 3.0;
constexpr double SafetyCapSecondsPer100m = 90.0;
/**
 * How long after a handover another one is impossible.
 *
 * The next takeover zone is a whole leg away — a hundred metres is eleven
 * seconds — so no legitimate pass can follow within half a second. Two
 * presses landing in the SAME frame are drained by one Step(), and without
 * this the second was applied at the same metre against the next leg's
 * zone: a disqualification the player never earned, from a two-thumb tap.
 */
constexpr double HandoverLockoutSeconds = 0.5;

/** Cadence the leg is judged against, as a sprint's is. */
constexpr double DriveCadenceHz = 3.30;
constexpr double HoldCadenceHz = 4.60;
constexpr double DriveEndFraction = 0.30;

double Normalized(float Attr)
{
	return FMath::Clamp(static_cast<double>(Attr), 0.0, 100.0) / 100.0;
}
}

namespace WSRelayEvents
{
const TArray<FWSRelayEventSpec>& All()
{
	static const TArray<FWSRelayEventSpec> Events = []
	{
		TArray<FWSRelayEventSpec> Table;

		FWSRelayEventSpec Short;
		Short.Code = TEXT("relay-4x100");
		Short.DisplayName = TEXT("4x100m Relay");
		Short.LegCount = 4;
		Short.LegMetres = 100.0;
		Short.TakeoverZoneMetres = 30.0;
		Short.IdealPassBeforeLineMetres = 3.0;
		Short.CeilingAtZero = 64.00;
		Short.CeilingAtHundred = 36.50;
		Short.MinPlausibleSeconds = 35.00;
		Short.MaxPlausibleSeconds = 120.00;
		Short.SpeedAtZero = 6.80;
		Short.SpeedAtHundred = 11.90;
		Short.TopSpeedCurve = 1.70;
		Short.FatigueScale = 1.0;
		Short.FatigueDepthScale = 1.0;
		Short.FatigueStaminaSpread = 0.0;
		Short.GoverningAttributes = {
			TEXT("reaction"), TEXT("acceleration"), TEXT("max_speed"),
			TEXT("stride_efficiency"), TEXT("technique")};
		Table.Add(Short);

		FWSRelayEventSpec Long;
		Long.Code = TEXT("relay-4x400");
		Long.DisplayName = TEXT("4x400m Relay");
		Long.LegCount = 4;
		Long.LegMetres = 400.0;
		// The long relay's zone is 20m, and the baton is usually taken
		// nearer the line because the incoming runner is tiring.
		Long.TakeoverZoneMetres = 20.0;
		Long.IdealPassBeforeLineMetres = 2.0;
		Long.CeilingAtZero = 292.00;
		Long.CeilingAtHundred = 173.00;
		Long.MinPlausibleSeconds = 170.00;
		Long.MaxPlausibleSeconds = 420.00;
		Long.SpeedAtZero = 6.80;
		Long.SpeedAtHundred = 10.45;
		Long.TopSpeedCurve = 1.35;
		// A 400m leg is a fatigue event, and the spread is where a strong
		// athlete separates from a weak one over the distance. Gentler than
		// the individual 400m's numbers, because a relay leg starts on a
		// fresh pair of legs and at speed rather than from blocks.
		Long.FatigueScale = 2.20;
		Long.FatigueDepthScale = 1.20;
		Long.FatigueStaminaSpread = 0.40;
		Long.GoverningAttributes = {
			TEXT("reaction"), TEXT("acceleration"), TEXT("max_speed"),
			TEXT("stride_efficiency"), TEXT("recovery"), TEXT("technique")};
		Table.Add(Long);

		return Table;
	}();
	return Events;
}

const FWSRelayEventSpec& Find(const FString& Code)
{
	for (const FWSRelayEventSpec& Spec : All())
	{
		if (Spec.Code == Code)
		{
			return Spec;
		}
	}
	// An unknown code is a contract break, not a reason to invent an event.
	return All()[0];
}
}

FWSRelaySimulation::FWSRelaySimulation(const FWSSprintAttributes& InAttributes,
	uint32 InSeed, const FWSRelayEventSpec& InEventSpec)
	: Attributes(InAttributes)
	, EventSpec(InEventSpec)
{
	using namespace WSRelay;

	// The gun is at zero and the clock starts three seconds before it, as
	// every timed event's does. What the player cannot predict is WHEN the
	// "set" call comes inside that window, and the game mode owns that —
	// randomising the simulation's own zero would step it against a
	// different clock from the one its inputs are stamped with.
	GunTime = 0.0;
	State.RaceTime = -PreGunSeconds;
	State.MetresToHandover = EventSpec.LegMetres;
}

double FWSRelaySimulation::TargetCadenceAt(double LegDistanceMetres) const
{
	using namespace WSRelay;

	const double Fraction = EventSpec.LegMetres > 0.0
		? FMath::Clamp(LegDistanceMetres / EventSpec.LegMetres, 0.0, 1.0)
		: 1.0;
	if (Fraction < DriveEndFraction)
	{
		return DriveCadenceHz + (HoldCadenceHz - DriveCadenceHz) * (Fraction / DriveEndFraction);
	}
	return HoldCadenceHz;
}

void FWSRelaySimulation::AddInput(const FWSRelayInputEvent& Event)
{
	PendingEvents.Add(Event);
}

void FWSRelaySimulation::ApplyEvent(const FWSRelayInputEvent& Event)
{
	using namespace WSRelay;

	switch (Event.Type)
	{
	case EWSRelayInputType::Release:
	{
		if (State.bReleased)
		{
			break;
		}
		const double ReactionMs = (Event.TimeSeconds - GunTime) * 1000.0;
		if (ReactionMs < FalseStartFloorMs)
		{
			// Under 100ms is a false start at the server too: nobody reacts
			// to a gun that fast, so it is movement decided in advance.
			State.bFalseStart = true;
			Outcome.bFalseStart = true;
			Outcome.ReactionMs = ReactionMs;
			break;
		}
		State.bReleased = true;
		Outcome.ReactionMs = ReactionMs;
		// The legs measure RUNNING time, so the first one starts when the
		// athlete leaves the blocks — not when the gun went.
		//
		// The server checks sum(splits) + reaction == time. Measuring leg 1
		// from the gun counts the reaction twice, and the check then fails
		// by exactly the reaction: a team that took the automatic release
		// (1.5s) was refused with "splits (74.530s) plus reaction do not sum
		// to the recorded time (74.530s)" — two identical numbers, which is
		// the double-count showing through. FWSSprintSimulation has always
		// done this; the relay did not.
		LastLegTime = ReactionSeconds();
		break;
	}

	case EWSRelayInputType::Tap:
	{
		if (!State.bReleased)
		{
			break;
		}
		if (LastTapTime >= 0.0)
		{
			const double Interval = Event.TimeSeconds - LastTapTime;
			if (Interval > KINDA_SMALL_NUMBER)
			{
				const double Window = PrevTapInterval > 0.0
					? (Interval + PrevTapInterval) / 2.0
					: Interval;
				State.ActualCadenceHz = 1.0 / Window;
				PrevTapInterval = Interval;
			}
		}
		LastTapTime = Event.TimeSeconds;
		break;
	}

	case EWSRelayInputType::Pass:
	{
		if (!State.bReleased || State.Leg >= EventSpec.LegCount || bPassRequested)
		{
			break;
		}
		if (Event.TimeSeconds - LastHandoverTime < HandoverLockoutSeconds)
		{
			// A duplicate of the handover that just happened, not a new one.
			break;
		}
		bPassRequested = true;
		Handover(State.Distance);
		break;
	}
	}
}

void FWSRelaySimulation::Handover(double PassDistance)
{
	const double Line = EventSpec.LegLineMetres(State.Leg - 1);
	const double ZoneStart = Line - EventSpec.TakeoverZoneMetres;

	if (PassDistance < ZoneStart || PassDistance > Line)
	{
		// Outside the takeover zone. In the sport that is a
		// disqualification, and a team that is disqualified does not get a
		// slow time — it gets no time at all.
		Outcome.bBadExchange = true;
		Outcome.BadExchangeLeg = State.Leg;
		State.bFinished = true;
		Outcome.bFinished = true;
		return;
	}

	// How close to the mark the baton changed hands. The ideal is near the
	// end of the zone, with the outgoing runner already at speed.
	const double Ideal = Line - EventSpec.IdealPassBeforeLineMetres;
	const double HalfZone = FMath::Max(EventSpec.TakeoverZoneMetres * 0.5, 0.01);
	const double Quality = FMath::Clamp(
		1.0 - FMath::Abs(PassDistance - Ideal) / HalfZone, 0.0, 1.0);
	Outcome.ExchangeQuality.Add(Quality);

	// The outgoing runner leaves with the share of the incoming runner's
	// speed the exchange preserved. THIS is why a relay beats four
	// individual runs: nobody after the first leg starts from a standstill.
	State.Speed *= FMath::Lerp(
		EventSpec.ExchangeKeepAtWorst, EventSpec.ExchangeKeepAtBest, Quality);

	// A new athlete, so a fresh pair of legs. The clock does not stop.
	State.Fatigue = 0.0;
	Outcome.Splits.Add(State.RaceTime - LastLegTime);
	LastLegTime = State.RaceTime;
	LastHandoverTime = State.RaceTime;
	++State.Leg;
	bPassRequested = false;
	// The cadence the new runner is judged on starts over with them.
	LastTapTime = -1.0;
	PrevTapInterval = 0.0;
	State.ActualCadenceHz = 0.0;
}

bool FWSRelaySimulation::Step()
{
	using namespace WSRelay;

	if (State.bFinished || State.bFalseStart)
	{
		return false;
	}

	while (NextEventIndex < PendingEvents.Num() &&
		PendingEvents[NextEventIndex].TimeSeconds <= State.RaceTime + StepDt)
	{
		ApplyEvent(PendingEvents[NextEventIndex]);
		++NextEventIndex;
		if (State.bFalseStart || State.bFinished)
		{
			return false;
		}
	}

	const double PrevDistance = State.Distance;
	State.RaceTime += StepDt;

	if (!State.bReleased)
	{
		if (State.RaceTime >= AutoReleaseSeconds)
		{
			FWSRelayInputEvent Auto;
			Auto.Type = EWSRelayInputType::Release;
			Auto.TimeSeconds = AutoReleaseSeconds;
			ApplyEvent(Auto);
			if (State.bFalseStart)
			{
				return false;
			}
		}
		if (!State.bReleased)
		{
			return State.RaceTime <
				SafetyCapSecondsPer100m * (EventSpec.TotalMetres() / 100.0);
		}
	}

	const double GoverningFraction = FMath::Clamp(
		Attributes.GoverningMean(EventSpec.GoverningAttributes), 0.0, 100.0) / 100.0;
	// Every per-attribute effect is capped at the mean the SERVER's ceiling
	// is computed from, so no single attribute can buy a time the mean does
	// not allow. The same invariant every other event holds.
	const auto Capped = [GoverningFraction](float Attr)
	{
		return FMath::Min(Normalized(Attr), GoverningFraction);
	};

	// --- Cadence, judged per LEG -----------------------------------------
	const double LegStart = EventSpec.LegMetres * (State.Leg - 1);
	const double LegDistance = State.Distance - LegStart;
	State.TargetCadenceHz = TargetCadenceAt(LegDistance);
	double InstantAccuracy = BaselineAccuracy;
	const double SinceLastTap = State.RaceTime - LastTapTime;
	if (LastTapTime >= 0.0 && State.ActualCadenceHz > 0.0 &&
		SinceLastTap < 2.0 / FMath::Max(State.TargetCadenceHz, 0.1))
	{
		const double ToleranceHz = 0.70 +
			0.25 * Capped(Attributes.StrideEfficiency) +
			0.10 * Capped(Attributes.Technique);
		const double Error = FMath::Abs(State.ActualCadenceHz - State.TargetCadenceHz);
		InstantAccuracy = FMath::Max(BaselineAccuracy, 1.0 - Error / ToleranceHz);
	}
	State.CadenceAccuracy +=
		(InstantAccuracy - State.CadenceAccuracy) * FMath::Min(1.0, 4.0 * StepDt);

	// --- Speed -----------------------------------------------------------
	const double VMax = EventSpec.SpeedAtZero +
		(EventSpec.SpeedAtHundred - EventSpec.SpeedAtZero) *
		FMath::Pow(GoverningFraction, EventSpec.TopSpeedCurve);
	const double Tau = TauAt0 - TauGain * Capped(Attributes.Acceleration);
	const double AccuracyFactor =
		AccuracyFloor + (1.0 - AccuracyFloor) * State.CadenceAccuracy;
	const double FatigueDepth = FatiguePenalty * EventSpec.FatigueDepthScale *
		(1.0 + EventSpec.FatigueStaminaSpread * (1.0 - Capped(Attributes.Stamina)));
	const double FatigueFactor = 1.0 - FatigueDepth * State.Fatigue;
	const double VTarget = VMax * AccuracyFactor * FatigueFactor;

	State.Speed += (VTarget - State.Speed) / Tau * StepDt;
	State.Distance += State.Speed * StepDt;

	const double Intensity = VMax > 0.0 ? State.Speed / VMax : 0.0;
	State.Fatigue += FatigueRate * EventSpec.FatigueScale *
		Intensity * Intensity * Intensity *
		(1.35 - 0.65 * Capped(Attributes.Stamina)) *
		(1.45 - 0.65 * State.CadenceAccuracy) * StepDt;
	State.Fatigue = FMath::Min(State.Fatigue, 1.0);

	// --- The zone and the line -------------------------------------------
	const bool bLastLeg = State.Leg >= EventSpec.LegCount;
	const double Line = EventSpec.LegLineMetres(State.Leg - 1);
	State.MetresToHandover = bLastLeg ? 0.0 : Line - State.Distance;
	State.bInTakeoverZone = !bLastLeg &&
		State.Distance >= Line - EventSpec.TakeoverZoneMetres &&
		State.Distance <= Line;

	if (!bLastLeg && State.Distance > Line)
	{
		// The zone is behind them and the baton never changed hands. That
		// is a disqualification, not a slow leg.
		Outcome.bBadExchange = true;
		Outcome.BadExchangeLeg = State.Leg;
		State.bFinished = true;
		Outcome.bFinished = true;
		return false;
	}

	if (bLastLeg && State.Distance >= EventSpec.TotalMetres() &&
		PrevDistance < State.Distance)
	{
		const double Alpha = (EventSpec.TotalMetres() - PrevDistance) /
			(State.Distance - PrevDistance);
		const double CrossTime = (State.RaceTime - StepDt) + Alpha * StepDt;
		Outcome.Splits.Add(CrossTime - LastLegTime);
		State.bFinished = true;
		Outcome.bFinished = true;
		// Official athletics timing truncates up to the millisecond.
		Outcome.TimeSeconds = FMath::CeilToDouble(CrossTime * 1000.0) / 1000.0;
		return false;
	}

	return State.RaceTime <
		SafetyCapSecondsPer100m * (EventSpec.TotalMetres() / 100.0);
}

FWSRelayOutcome FWSRelaySimulation::RunTrace(const FWSSprintAttributes& Attributes,
	uint32 Seed, const TArray<FWSRelayInputEvent>& Trace,
	const FWSRelayEventSpec& EventSpec)
{
	FWSRelaySimulation Simulation(Attributes, Seed, EventSpec);
	for (const FWSRelayInputEvent& Event : Trace)
	{
		Simulation.AddInput(Event);
	}
	while (Simulation.Step())
	{
	}
	return Simulation.GetOutcome();
}

FString FWSRelaySimulation::DigestTrace(const TArray<FWSRelayInputEvent>& Trace)
{
	FString Payload;
	for (const FWSRelayInputEvent& Event : Trace)
	{
		Payload += FString::Printf(TEXT("%.4f:%d;"),
			Event.TimeSeconds, static_cast<int32>(Event.Type));
	}
	return FMD5::HashAnsiString(*Payload);
}

TArray<FWSRelayInputEvent> FWSRelaySimulation::GenerateAITrace(
	const FWSSprintAttributes& Attributes, uint32 RaceSeed, uint32 InputSeed,
	double ReactionMeanMs, double ReactionSpreadMs, double Consistency,
	const FWSRelayEventSpec& EventSpec)
{
	using namespace WSRelay;

	// Closed loop, like every other event's: the team is watched running
	// and the baton is called for when the zone actually arrives, never at
	// a time decided in advance.
	FRandomStream Stream(static_cast<int32>(InputSeed ^ 0x9E3779B9u));
	auto Gaussish = [&Stream]()
	{
		return (Stream.FRand() + Stream.FRand() + Stream.FRand()) - 1.5;
	};

	const double Skill = FMath::Clamp(Consistency, 0.0, 1.0);
	const double Sloppiness = 1.0 - Skill;
	const double Jitter = Sloppiness * 0.35;

	TArray<FWSRelayInputEvent> Trace;
	FWSRelaySimulation Shadow(Attributes, RaceSeed, EventSpec);

	FWSRelayInputEvent Release;
	Release.Type = EWSRelayInputType::Release;
	Release.TimeSeconds = FMath::Max(FalseStartFloorMs + 1.0,
		ReactionMeanMs + ReactionSpreadMs * Gaussish()) / 1000.0;
	Trace.Add(Release);
	Shadow.AddInput(Release);

	double NextTapAt = Release.TimeSeconds + 0.10;
	int32 PassedLeg = 0;
	while (Shadow.Step())
	{
		const FWSRelayState& Live = Shadow.GetState();
		if (Live.RaceTime < 0.0)
		{
			continue;
		}
		while (NextTapAt <= Live.RaceTime)
		{
			FWSRelayInputEvent Tap;
			Tap.TimeSeconds = NextTapAt;
			Tap.Type = EWSRelayInputType::Tap;
			Trace.Add(Tap);
			Shadow.AddInput(Tap);

			const double LegStart = EventSpec.LegMetres * (Live.Leg - 1);
			const double TargetHz = FMath::Max(
				Shadow.TargetCadenceAt(Live.Distance - LegStart), 0.5);
			NextTapAt += FMath::Max(1.0 / TargetHz * (1.0 + Jitter * Gaussish()), 0.05);
		}

		// Hand over when the mark arrives. A sloppy team is early or late
		// on it; a very sloppy one misses the zone, which is a DQ and is
		// supposed to be.
		if (Live.Leg <= EventSpec.LegCount - 1 && Live.Leg != PassedLeg)
		{
			const double Line = EventSpec.LegLineMetres(Live.Leg - 1);
			const double Ideal = Line - EventSpec.IdealPassBeforeLineMetres +
				Sloppiness * EventSpec.TakeoverZoneMetres * 0.45 * Gaussish();
			// One step of lookahead, because a stride covers ground: aiming
			// at an exact metre means stepping straight over it.
			if (Live.Distance >= Ideal - Live.Speed * StepDt)
			{
				FWSRelayInputEvent Pass;
				Pass.TimeSeconds = Live.RaceTime;
				Pass.Type = EWSRelayInputType::Pass;
				Trace.Add(Pass);
				Shadow.AddInput(Pass);
				PassedLeg = Live.Leg;
			}
		}
	}
	return Trace;
}
