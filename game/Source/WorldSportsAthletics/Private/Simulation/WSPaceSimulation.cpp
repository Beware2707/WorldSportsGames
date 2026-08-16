#include "Simulation/WSPaceSimulation.h"

#include "Math/RandomStream.h"
#include "Misc/SecureHash.h"

namespace WSPace
{
constexpr double MinEffort = 0.35;
constexpr double MaxEffort = 1.0;
/** Speed at zero effort, as a fraction of the event's top speed. */
constexpr double EffortSpeedFloor = 0.42;
/** How quickly effort follows the player's request (seconds). */
constexpr double EffortTau = 0.55;
/** How quickly speed follows effort (seconds). */
constexpr double SpeedTau = 1.15;

/** Sustainable effort at zero and full stamina. Below this you recover. */
constexpr double SustainableAt0 = 0.50;
constexpr double SustainableAt100 = 0.78;
/** Regeneration is slow — easing off is damage control, not a reset. */
constexpr double RegenRate = 0.022;

/** Once empty, speed collapses toward this fraction, over WallTau seconds. */
constexpr double WallFloor = 0.62;
constexpr double WallTau = 2.6;
/** Recovering from the wall is far slower than falling into it. */
constexpr double WallRecoverTau = 9.0;

/** Energy the kick unlocks, and the penalty for spending it too early. */
constexpr double KickReserve = 0.16;
constexpr double EarlyKickPenalty = 0.965;

/** Per 100m of race, so a 1500m is not abandoned for being long. */
constexpr double SafetyCapSecondsPer100m = 90.0;

double Normalized(float Attr)
{
	return FMath::Clamp(static_cast<double>(Attr), 0.0, 100.0) / 100.0;
}
}

namespace WSPaceEvents
{
const TArray<FWSPaceEventSpec>& All()
{
	static const TArray<FWSPaceEventSpec> Events = []
	{
		TArray<FWSPaceEventSpec> Table;

		FWSPaceEventSpec Middle800;
		Middle800.Code = TEXT("middle-800m");
		Middle800.DisplayName = TEXT("800m");
		Middle800.DistanceMetres = 800.0;
		Middle800.SplitCount = 2;            // one per lap
		Middle800.CeilingAtZero = 150.0;
		Middle800.CeilingAtHundred = 101.0;
		Middle800.MinSplitSeconds = 45.0;
		Middle800.MinPlausibleSeconds = 100.0;
		Middle800.MaxPlausibleSeconds = 900.0;
		Middle800.SpeedAtZero = 7.412;
		Middle800.SpeedAtHundred = 9.298;
		Middle800.TopSpeedCurve = 1.22;
		Middle800.DrainRate = 0.083;
		Middle800.KickWindowFraction = 0.78;
		Middle800.GoverningAttributes = {
			TEXT("max_speed"), TEXT("stride_efficiency"), TEXT("stamina"),
			TEXT("recovery"), TEXT("technique")};
		Table.Add(Middle800);

		// A row, not a code path. The 1500m is the same event kind run
		// further: a lower share of top speed and a tighter energy budget.
		FWSPaceEventSpec Middle1500;
		Middle1500.Code = TEXT("middle-1500m");
		Middle1500.DisplayName = TEXT("1500m");
		Middle1500.DistanceMetres = 1500.0;
		Middle1500.SplitCount = 5;           // 300m segments
		Middle1500.CeilingAtZero = 310.0;
		Middle1500.CeilingAtHundred = 206.0;
		Middle1500.MinSplitSeconds = 33.0;
		Middle1500.MinPlausibleSeconds = 205.0;
		Middle1500.MaxPlausibleSeconds = 1800.0;
		Middle1500.SpeedAtZero = 6.752;
		Middle1500.SpeedAtHundred = 8.514;
		Middle1500.TopSpeedCurve = 1.45;
		Middle1500.DrainRate = 0.049;
		Middle1500.KickWindowFraction = 0.84;
		Middle1500.GoverningAttributes = {
			TEXT("max_speed"), TEXT("stride_efficiency"), TEXT("stamina"),
			TEXT("recovery"), TEXT("technique")};
		Table.Add(Middle1500);

		return Table;
	}();
	return Events;
}

const FWSPaceEventSpec& Find(const FString& Code)
{
	for (const FWSPaceEventSpec& Spec : All())
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

FWSMiddleDistanceSimulation::FWSMiddleDistanceSimulation(
	const FWSSprintAttributes& InAttributes, uint32 InSeed,
	const FWSPaceEventSpec& InEventSpec)
	: Attributes(InAttributes)
	, EventSpec(InEventSpec)
{
	using namespace WSPace;

	FRandomStream Stream(static_cast<int32>(InSeed));
	// No wind is modelled — see the header. The seed still varies the race:
	// the sustainable-pace band drifts, so the pace that felt right a lap
	// ago is not exactly the pace that is right now. Reading that is the
	// skill, and it must be the same drift for everyone in the race.
	PaceDriftPhase = Stream.FRand() * 2.0 * PI;

	GoverningFraction = FMath::Clamp(
		Attributes.GoverningMean(EventSpec.GoverningAttributes), 0.0, 100.0) / 100.0;
	VMax = EventSpec.SpeedAtZero + (EventSpec.SpeedAtHundred - EventSpec.SpeedAtZero) *
		FMath::Pow(GoverningFraction, EventSpec.TopSpeedCurve);

	// The athlete starts running, not standing: a standing start still
	// means going from a stop, but there is no set/gun phase to hold.
	EffortTarget = MinEffort;
	State.Effort = MinEffort;
	State.SustainableEffort = SustainableAt0;
}

void FWSMiddleDistanceSimulation::AddInput(const FWSPaceInputEvent& Event)
{
	PendingEvents.Add(Event);
}

void FWSMiddleDistanceSimulation::ApplyEvent(const FWSPaceInputEvent& Event)
{
	using namespace WSPace;

	switch (Event.Type)
	{
	case EWSPaceInputType::SetEffort:
		EffortTarget = FMath::Clamp(Event.Value, MinEffort, MaxEffort);
		break;

	case EWSPaceInputType::Kick:
		if (State.bKicked)
		{
			break;
		}
		State.bKicked = true;
		EffortTarget = MaxEffort;
		if (State.Distance >= EventSpec.DistanceMetres * EventSpec.KickWindowFraction)
		{
			// Inside the window the reserve is real, and it is the whole
			// reason to have saved anything.
			KickReserve = WSPace::KickReserve;
			State.Energy += KickReserve;
		}
		else
		{
			// Too early: nothing left for the finish, and the surge itself
			// costs. Kicking from the front with a lap to go loses races.
			State.Speed *= EarlyKickPenalty;
		}
		break;
	}
}

bool FWSMiddleDistanceSimulation::Step()
{
	using namespace WSPace;

	if (State.bFinished)
	{
		return false;
	}

	while (NextEventIndex < PendingEvents.Num() &&
		PendingEvents[NextEventIndex].TimeSeconds <= State.RaceTime + StepDt)
	{
		ApplyEvent(PendingEvents[NextEventIndex]);
		++NextEventIndex;
	}

	const double PrevDistance = State.Distance;
	State.RaceTime += StepDt;

	// --- Effort ---------------------------------------------------------
	State.Effort += (EffortTarget - State.Effort) * FMath::Min(1.0, StepDt / EffortTau);

	// --- The sustainable band -------------------------------------------
	// Capped at the governing mean for the same reason the sprint caps its
	// per-attribute terms: the server's ceiling is a function of that mean,
	// so no single attribute may buy a time the mean does not allow.
	const double CappedStamina =
		FMath::Min(Normalized(Attributes.Stamina), GoverningFraction);
	const double CappedRecovery =
		FMath::Min(Normalized(Attributes.Recovery), GoverningFraction);
	// The band drifts through the race, so holding pace is a judgement made
	// continuously rather than a number memorised once.
	const double Drift = 0.018 * FMath::Sin(2.0 * PI * 0.045 * State.RaceTime + PaceDriftPhase);
	State.SustainableEffort =
		SustainableAt0 + (SustainableAt100 - SustainableAt0) * CappedStamina + Drift;

	// --- Energy ----------------------------------------------------------
	if (State.Effort > State.SustainableEffort)
	{
		const double Over = State.Effort - State.SustainableEffort;
		State.Energy -= EventSpec.DrainRate * Over * Over * StepDt * 100.0;
	}
	else
	{
		const double Under = State.SustainableEffort - State.Effort;
		State.Energy = FMath::Min(1.0,
			State.Energy + RegenRate * Under * CappedRecovery * StepDt);
	}

	if (State.Energy <= 0.0)
	{
		State.Energy = 0.0;
		if (!State.bWalled)
		{
			Outcome.WallAtFraction = EventSpec.DistanceMetres > 0.0
				? State.Distance / EventSpec.DistanceMetres
				: 0.0;
		}
		State.bWalled = true;
		Outcome.bWalled = true;
	}

	// Falling apart is fast; getting back is slow. That asymmetry is what
	// makes going out too hard a decision the athlete cannot take back.
	const double WallTarget = State.bWalled && State.Energy <= 0.0 ? WallFloor : 1.0;
	const double Tau = WallTarget < WallFactor ? WallTau : WallRecoverTau;
	WallFactor += (WallTarget - WallFactor) * FMath::Min(1.0, StepDt / Tau);

	// --- Speed -----------------------------------------------------------
	const double EffortSpeed = EffortSpeedFloor + (1.0 - EffortSpeedFloor) * State.Effort;
	const double VTarget = VMax * EffortSpeed * WallFactor;
	State.Speed += (VTarget - State.Speed) * FMath::Min(1.0, StepDt / SpeedTau);
	State.Distance += State.Speed * StepDt;

	// --- Splits and the finish -------------------------------------------
	const double SegmentMetres = EventSpec.SplitCount > 0
		? EventSpec.DistanceMetres / EventSpec.SplitCount
		: EventSpec.DistanceMetres;
	while (NextSplitMark <= EventSpec.SplitCount &&
		State.Distance >= NextSplitMark * SegmentMetres &&
		PrevDistance < State.Distance)
	{
		const double Mark = NextSplitMark * SegmentMetres;
		const double Alpha = (Mark - PrevDistance) / (State.Distance - PrevDistance);
		const double CrossTime = (State.RaceTime - StepDt) + Alpha * StepDt;
		Outcome.Splits.Add(CrossTime - LastSplitTime);
		LastSplitTime = CrossTime;
		if (NextSplitMark == EventSpec.SplitCount)
		{
			State.bFinished = true;
			Outcome.bFinished = true;
			// Official athletics timing truncates up to the millisecond.
			Outcome.TimeSeconds = FMath::CeilToDouble(CrossTime * 1000.0) / 1000.0;
		}
		++NextSplitMark;
	}

	if (State.bFinished)
	{
		return false;
	}
	return State.RaceTime <
		SafetyCapSecondsPer100m * (EventSpec.DistanceMetres / 100.0);
}

FWSPaceOutcome FWSMiddleDistanceSimulation::RunTrace(const FWSSprintAttributes& Attributes,
	uint32 Seed, const TArray<FWSPaceInputEvent>& Trace, const FWSPaceEventSpec& EventSpec)
{
	FWSMiddleDistanceSimulation Simulation(Attributes, Seed, EventSpec);
	for (const FWSPaceInputEvent& Event : Trace)
	{
		Simulation.AddInput(Event);
	}
	while (Simulation.Step())
	{
	}
	return Simulation.GetOutcome();
}

FString FWSMiddleDistanceSimulation::DigestTrace(const TArray<FWSPaceInputEvent>& Trace)
{
	FString Payload;
	for (const FWSPaceInputEvent& Event : Trace)
	{
		Payload += FString::Printf(TEXT("%.4f:%d:%.4f;"),
			Event.TimeSeconds, static_cast<int32>(Event.Type), Event.Value);
	}
	return FMD5::HashAnsiString(*Payload);
}

TArray<FWSPaceInputEvent> FWSMiddleDistanceSimulation::GeneratePerfectTrace(
	const FWSSprintAttributes& Attributes, uint32 Seed, const FWSPaceEventSpec& EventSpec)
{
	// Build a candidate race at a given even effort, kicking once the window
	// opens. The kick is placed by distance, not time, so it lands in the
	// window regardless of how fast the athlete is.
	auto TraceAtEffort = [&EventSpec](double Effort)
	{
		TArray<FWSPaceInputEvent> Trace;
		FWSPaceInputEvent Start;
		Start.TimeSeconds = 0.0;
		Start.Type = EWSPaceInputType::SetEffort;
		Start.Value = Effort;
		Trace.Add(Start);
		return Trace;
	};

	// Search for the effort that produces the FASTEST finish — not the
	// fastest one that avoids emptying the tank.
	//
	// Those are not the same race, and assuming they were is what let an
	// honestly run 800m come back rejected: going a shade over sustainable
	// pace and dying over the last 100m beats holding a pace you can
	// survive, exactly as it does on a real track. Calibrating the ceiling
	// against the survivable pace therefore understated the true optimum,
	// and the difference was enough to cross the server's limit.
	//
	// A coarse scan, then a local refine — NOT a ternary search. Finish time
	// against effort is not unimodal: past the sustainable pace it jumps as
	// the athlete starts dying, and further up it can improve again before
	// collapsing for good. A ternary search on that shape happily converges
	// into the wrong basin, which it did — at zero attributes it settled on
	// a pace 19 seconds slower than one the scan finds immediately.
	auto TimeAtEffort = [&Attributes, Seed, &EventSpec, &TraceAtEffort](double Effort)
	{
		const FWSPaceOutcome Candidate =
			RunTrace(Attributes, Seed, TraceAtEffort(Effort), EventSpec);
		// A run that never reaches the line is not a candidate at all.
		return Candidate.bFinished
			? Candidate.TimeSeconds
			: TNumericLimits<double>::Max();
	};

	constexpr int32 ScanSteps = 26;
	const double Span = WSPace::MaxEffort - WSPace::MinEffort;
	double BestEffort = WSPace::MinEffort;
	double BestTime = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index <= ScanSteps; ++Index)
	{
		const double Effort = WSPace::MinEffort + Span * Index / ScanSteps;
		const double Time = TimeAtEffort(Effort);
		if (Time < BestTime)
		{
			BestTime = Time;
			BestEffort = Effort;
		}
	}

	// Refine inside the winning bracket, where the curve is smooth.
	const double Step = Span / ScanSteps;
	double Low = FMath::Max(WSPace::MinEffort, BestEffort - Step);
	double High = FMath::Min(WSPace::MaxEffort, BestEffort + Step);
	for (int32 Iteration = 0; Iteration < 10; ++Iteration)
	{
		const double OneThird = Low + (High - Low) / 3.0;
		const double TwoThirds = High - (High - Low) / 3.0;
		if (TimeAtEffort(OneThird) <= TimeAtEffort(TwoThirds))
		{
			High = TwoThirds;
		}
		else
		{
			Low = OneThird;
		}
	}
	Low = TimeAtEffort(0.5 * (Low + High)) <= BestTime ? 0.5 * (Low + High) : BestEffort;

	// The kick spends what is left over the closing stretch. It is added
	// after the search because the reserve it unlocks is only available
	// inside the window, so it cannot change what pace was survivable.
	TArray<FWSPaceInputEvent> Trace = TraceAtEffort(Low);
	FWSMiddleDistanceSimulation Probe(Attributes, Seed, EventSpec);
	for (const FWSPaceInputEvent& Event : Trace)
	{
		Probe.AddInput(Event);
	}
	const double KickAt = EventSpec.DistanceMetres * EventSpec.KickWindowFraction;
	double KickTime = -1.0;
	while (Probe.Step())
	{
		if (Probe.GetState().Distance >= KickAt)
		{
			KickTime = Probe.GetState().RaceTime;
			break;
		}
	}
	if (KickTime >= 0.0)
	{
		FWSPaceInputEvent Kick;
		Kick.TimeSeconds = KickTime;
		Kick.Type = EWSPaceInputType::Kick;
		Trace.Add(Kick);
	}
	return Trace;
}

TArray<FWSPaceInputEvent> FWSMiddleDistanceSimulation::GenerateAITrace(
	const FWSSprintAttributes& Attributes, uint32 RaceSeed, uint32 InputSeed,
	double Consistency, const FWSPaceEventSpec& EventSpec)
{
	// Start from the perfect race for THIS athlete in THIS race, then
	// degrade the pace judgement. A rival who cannot misjudge a pace is not
	// running the event — they are a stopwatch with a name.
	TArray<FWSPaceInputEvent> Trace =
		GeneratePerfectTrace(Attributes, RaceSeed, EventSpec);
	const double Skill = FMath::Clamp(Consistency, 0.0, 1.0);
	if (Skill >= 1.0 || Trace.Num() == 0)
	{
		return Trace;
	}

	FRandomStream Stream(static_cast<int32>(InputSeed));
	const double PlannedEffort = Trace[0].Value;
	// A weaker runner goes out too fast and pays for it, or too slow and
	// leaves time out there. Both are real ways to lose a middle-distance
	// race, so the error is signed rather than always costly.
	const double Error = (1.0 - Skill) * 0.16 * (Stream.FRand() * 2.0 - 1.0);

	TArray<FWSPaceInputEvent> Degraded;
	FWSPaceInputEvent Opening;
	Opening.TimeSeconds = 0.0;
	Opening.Type = EWSPaceInputType::SetEffort;
	Opening.Value = PlannedEffort + Error;
	Degraded.Add(Opening);

	// Mid-race correction, which a better runner makes sooner and smaller.
	FWSPaceInputEvent Correction;
	Correction.TimeSeconds = 20.0 + 25.0 * (1.0 - Skill);
	Correction.Type = EWSPaceInputType::SetEffort;
	Correction.Value = PlannedEffort + Error * (1.0 - Skill);
	Degraded.Add(Correction);

	for (const FWSPaceInputEvent& Event : Trace)
	{
		if (Event.Type == EWSPaceInputType::Kick)
		{
			// A less consistent runner kicks late, which is the cheapest
			// way to lose a race you were in.
			FWSPaceInputEvent Kick = Event;
			Kick.TimeSeconds += (1.0 - Skill) * 6.0;
			Degraded.Add(Kick);
		}
	}
	return Degraded;
}
