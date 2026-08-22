#include "Simulation/WSJumpSimulation.h"

#include "Math/RandomStream.h"
#include "Misc/SecureHash.h"

namespace WSJump
{
constexpr double TauAt0 = 1.35;
constexpr double TauGain = 0.45;
constexpr double AccuracyFloor = 0.66;
constexpr double BaselineAccuracy = 0.25;
constexpr double WindPerMs = 0.004;
constexpr double GravityMs2 = 9.81;
// The approach is a sprint, so it uses the sprint's cadence shape.
constexpr double DriveEndFraction = 0.35;
constexpr double DriveCadenceHz = 3.2;
constexpr double HoldCadenceHz = 4.6;
// A jump that never takes off ends when the runway does.
constexpr double OverrunMetres = 6.0;
/** Where a high jumper wants to leave the ground, in metres before the
 * bar, and how much of the height a badly placed takeoff costs. */
constexpr double VerticalMissPenalty = 0.22;
constexpr double SafetyCapSeconds = 60.0;

double Normalized(float Attr)
{
	return FMath::Clamp(static_cast<double>(Attr), 0.0, 100.0) / 100.0;
}
}

namespace WSJumpEvents
{
const TArray<FWSJumpEventSpec>& All()
{
	static const TArray<FWSJumpEventSpec> Events = []
	{
		TArray<FWSJumpEventSpec> Table;

		FWSJumpEventSpec LongJump;
		LongJump.Code = TEXT("jump-long");
		LongJump.DisplayName = TEXT("Long Jump");
		LongJump.RunwayMetres = 40.0;
		LongJump.Attempts = 3;
		LongJump.CeilingAtZero = 3.20;
		LongJump.CeilingAtHundred = 8.85;
		LongJump.MinPlausibleMetres = 1.00;
		LongJump.MaxPlausibleMetres = 9.00;
		LongJump.SpeedAtZero = 7.50;
		LongJump.SpeedAtHundred = 10.90;
		// Below 1.0: distance goes as the SQUARE of speed while the server's
		// ceiling is linear in the attribute mean, so a speed curve that
		// grows slowly at first leaves the middle of the range unable to
		// reach its own ceiling — the mirror image of the sag the sprints
		// correct with an exponent above 1.0.
		LongJump.TopSpeedCurve = 0.68;
		LongJump.AngleAtZeroTechnique = 13.5;
		LongJump.AngleAtFullTechnique = 21.5;
		LongJump.FlightScale = 1.0;
		LongJump.LandingBonusMetres = 0.55;
		LongJump.GoverningAttributes = {
			TEXT("acceleration"), TEXT("max_speed"),
			TEXT("stride_efficiency"), TEXT("technique")};
		Table.Add(LongJump);

		// The high jump: same approach and takeoff, a different question.
		// A shorter run-up, no foul line to overstep, and a bar that either
		// survives or does not.
		FWSJumpEventSpec HighJump;
		HighJump.Code = TEXT("jump-high");
		HighJump.DisplayName = TEXT("High Jump");
		HighJump.RunwayMetres = 18.0;
		HighJump.Attempts = 3;
		HighJump.bVertical = true;
		HighJump.StartBarMetres = 1.00;
		HighJump.BarIncrementMetres = 0.05;
		HighJump.FailuresAllowed = 3;
		// Calibrated to sit just UNDER the server's straight ceiling line
		// (1.00 m at zero, 2.42 m at a hundred) at every attribute level,
		// so a perfectly judged jump approaches a height the server will
		// accept and never one it will refuse.
		HighJump.HeightAtZero = 0.97;
		HighJump.HeightAtHundred = 2.38;
		HighJump.HeightCurve = 1.0;
		HighJump.CeilingAtZero = 1.00;
		HighJump.CeilingAtHundred = 2.42;
		HighJump.MinPlausibleMetres = 0.80;
		HighJump.MaxPlausibleMetres = 2.50;
		HighJump.SpeedAtZero = 6.10;
		HighJump.SpeedAtHundred = 8.40;
		HighJump.TopSpeedCurve = 0.80;
		HighJump.GoverningAttributes = {
			TEXT("acceleration"), TEXT("stride_efficiency"), TEXT("technique")};
		Table.Add(HighJump);

		// The pole vault: a ladder like the high jump, but bought with
		// runway SPEED carried onto the pole rather than with the plant
		// alone. A long run-up, a bar that starts high and moves in tens.
		FWSJumpEventSpec PoleVault;
		PoleVault.Code = TEXT("jump-pole");
		PoleVault.DisplayName = TEXT("Pole Vault");
		PoleVault.RunwayMetres = 40.0;
		PoleVault.Attempts = 3;
		PoleVault.bVertical = true;
		PoleVault.StartBarMetres = 2.60;
		// The bar moves in tens here, not fives: a vault clears far more
		// than a high jump, and a 5cm ladder would take fifty attempts.
		PoleVault.BarIncrementMetres = 0.10;
		PoleVault.FailuresAllowed = 3;
		// Under the server's line (2.10 at zero, 6.25 at a hundred) at
		// every level, for the same reason the high jump is.
		PoleVault.HeightAtZero = 2.05;
		PoleVault.HeightAtHundred = 6.15;
		PoleVault.HeightCurve = 1.0;
		PoleVault.CeilingAtZero = 2.10;
		PoleVault.CeilingAtHundred = 6.25;
		PoleVault.MinPlausibleMetres = 1.50;
		PoleVault.MaxPlausibleMetres = 6.50;
		PoleVault.SpeedAtZero = 7.20;
		PoleVault.SpeedAtHundred = 10.20;
		PoleVault.TopSpeedCurve = 0.80;
		PoleVault.GoverningAttributes = {
			TEXT("acceleration"), TEXT("max_speed"), TEXT("technique")};
		Table.Add(PoleVault);

		// The triple jump: a hop, a step and a jump, measured from the
		// board to where the last one lands. Three takeoffs, not one, and
		// the rhythm between them is the event.
		FWSJumpEventSpec TripleJump;
		TripleJump.Code = TEXT("jump-triple");
		TripleJump.DisplayName = TEXT("Triple Jump");
		TripleJump.RunwayMetres = 40.0;
		TripleJump.Attempts = 3;
		TripleJump.PhaseCount = 3;
		// Roughly the shares a real triple jumper covers: the hop and the
		// jump are long, the step is the one that gets neglected.
		TripleJump.PhaseShares = {0.36, 0.30, 0.34};
		TripleJump.CeilingAtZero = 6.40;
		TripleJump.CeilingAtHundred = 18.10;
		TripleJump.MinPlausibleMetres = 3.00;
		TripleJump.MaxPlausibleMetres = 18.50;
		TripleJump.SpeedAtZero = 7.20;
		TripleJump.SpeedAtHundred = 10.30;
		// 0.85, not the long jump's 0.68. Distance goes as the square of
		// speed while the server's ceiling is linear in the attribute
		// mean, and a triple jump spans nearly twelve metres between its
		// endpoints where a long jump spans five — so the same exponent
		// that keeps a long jump honest bulges a triple jump ABOVE its
		// ceiling through the middle of the range.
		TripleJump.TopSpeedCurve = 0.85;
		TripleJump.AngleAtZeroTechnique = 13.0;
		TripleJump.AngleAtFullTechnique = 20.0;
		// A triple jump covers roughly twice a long jump from the same
		// approach, because the speed is spent three times rather than once.
		TripleJump.FlightScale = 2.54;
		TripleJump.LandingBonusMetres = 0.55;
		TripleJump.GoverningAttributes = {
			TEXT("acceleration"), TEXT("max_speed"),
			TEXT("stride_efficiency"), TEXT("technique")};
		Table.Add(TripleJump);

		return Table;
	}();
	return Events;
}

const FWSJumpEventSpec& Find(const FString& Code)
{
	for (const FWSJumpEventSpec& Spec : All())
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

FWSJumpSimulation::FWSJumpSimulation(const FWSSprintAttributes& InAttributes,
	uint32 InSeed, const FWSJumpEventSpec& InEventSpec)
	: Attributes(InAttributes)
	, EventSpec(InEventSpec)
{
	FRandomStream Stream(static_cast<int32>(InSeed));
	// The same wind band as the sprints, and the same +2.0 legality rule at
	// the server: a jump is a wind-affected mark.
	Wind = -1.5 + 3.5 * Stream.FRand();
	BandDriftPhase = Stream.FRand() * 2.0 * PI;
	Outcome.Wind = FMath::RoundToDouble(Wind * 10.0) / 10.0;

	GoverningFraction = FMath::Clamp(
		Attributes.GoverningMean(EventSpec.GoverningAttributes), 0.0, 100.0) / 100.0;
	VMax = EventSpec.SpeedAtZero + (EventSpec.SpeedAtHundred - EventSpec.SpeedAtZero)
		* FMath::Pow(GoverningFraction, EventSpec.TopSpeedCurve);

	State.MetresToBoard = EventSpec.RunwayMetres;
}

double FWSJumpSimulation::TargetCadenceAt(double DistanceMetres) const
{
	using namespace WSJump;
	const double Fraction = EventSpec.RunwayMetres > 0.0
		? DistanceMetres / EventSpec.RunwayMetres
		: 0.0;
	if (Fraction < DriveEndFraction)
	{
		const double Ramp = Fraction / DriveEndFraction;
		return DriveCadenceHz + (HoldCadenceHz - DriveCadenceHz) * Ramp;
	}
	return HoldCadenceHz;
}

void FWSJumpSimulation::AddInput(const FWSJumpInputEvent& Event)
{
	PendingEvents.Add(Event);
}

void FWSJumpSimulation::ApplyEvent(const FWSJumpInputEvent& Event)
{
	switch (Event.Type)
	{
	case EWSJumpInputType::Tap:
	{
		if (State.bAirborne)
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

	case EWSJumpInputType::Takeoff:
		if (State.bAirborne)
		{
			// Mid-jump, a takeoff is the hop-to-step or step-to-jump one,
			// and it only counts inside the window around the landing.
			if (EventSpec.PhaseCount > 1 && State.bPhaseWindowOpen && PhaseTapTime < 0.0)
			{
				PhaseTapTime = Event.TimeSeconds;
			}
			break;
		}
		State.bAirborne = true;
		Outcome.TakeoffSpeed = State.Speed;
		// The mark is measured from the BOARD. Taking off past it is a foul
		// and no mark at all; taking off short of it simply costs that gap,
		// because the tape starts at the board either way.
		if (!EventSpec.bVertical && State.Distance > EventSpec.RunwayMetres)
		{
			Outcome.bFoul = true;
			Outcome.bOverstepped = true;
			Outcome.DistanceMetres = 0.0;
			State.bFinished = true;
			Outcome.bFinished = true;
			break;
		}
		Outcome.BoardGapMetres = EventSpec.RunwayMetres - State.Distance;
		// EVERY jump flies. A long jump is one phase and a triple jump is
		// three, but neither is over at the moment the foot leaves the
		// board: resolving there put the mark on screen while the athlete
		// was still standing on the runway.
		RawTotal = ComputeRawDistance();
		BeginPhase(1);
		break;
	}
}

double FWSJumpSimulation::PhaseShare(int32 PhaseIndex) const
{
	if (!EventSpec.PhaseShares.IsValidIndex(PhaseIndex - 1))
	{
		return EventSpec.PhaseCount > 0 ? 1.0 / EventSpec.PhaseCount : 1.0;
	}
	return EventSpec.PhaseShares[PhaseIndex - 1];
}

double FWSJumpSimulation::ComputeVerticalHeight() const
{
	using namespace WSJump;

	const double CappedTechnique =
		FMath::Min(Normalized(Attributes.Technique), GoverningFraction);
	const double Best = EventSpec.HeightAtZero +
		(EventSpec.HeightAtHundred - EventSpec.HeightAtZero) *
		FMath::Pow(GoverningFraction, EventSpec.HeightCurve);

	// The takeoff spot matters as much as the speed: too far out and the
	// arc peaks before the bar, too close and it is still rising. A high
	// jumper plants ON their mark, so the error is the distance from it in
	// EITHER direction — there is no foul side here. Technique widens the
	// window that still works.
	const double Window = 0.55 + 0.65 * CappedTechnique;
	const double Error = FMath::Clamp(
		FMath::Abs(Outcome.BoardGapMetres) / Window, 0.0, 1.0);
	return FMath::RoundToDouble(
		Best * (1.0 - VerticalMissPenalty * Error) * 100.0) / 100.0;
}

double FWSJumpSimulation::ComputeRawDistance() const
{
	using namespace WSJump;

	const double CappedTechnique =
		FMath::Min(Normalized(Attributes.Technique), GoverningFraction);
	const double AngleRadians = FMath::DegreesToRadians(FMath::Lerp(
		EventSpec.AngleAtZeroTechnique, EventSpec.AngleAtFullTechnique, CappedTechnique));
	const double Speed = FMath::Max(Outcome.TakeoffSpeed, 0.0);
	const double Range = Speed * Speed * FMath::Sin(2.0 * AngleRadians) / GravityMs2;
	return EventSpec.FlightScale * Range + EventSpec.LandingBonusMetres;
}

void FWSJumpSimulation::BeginPhase(int32 PhaseIndex)
{
	if (EventSpec.bVertical && PhaseIndex == 1)
	{
		// Known at takeoff, so the arc can rise to the height the athlete
		// actually reached rather than to a guess.
		PeakHeight = ComputeVerticalHeight();
	}
	State.Phase = PhaseIndex;
	State.bPhaseWindowOpen = false;
	PhaseTapTime = -1.0;

	// How long this phase hangs: the ground it covers at the speed it is
	// covered at. A hop is longer in the air than a step because it is
	// longer on the ground.
	const double Speed = FMath::Max(Outcome.TakeoffSpeed, 1.0);
	// The ground this phase will actually cover, penalty included, so the
	// time it hangs matches the distance it makes.
	const double PhaseMetres = RawTotal * PhaseShare(PhaseIndex) * PhaseFactor;
	PhaseDuration = FMath::Max(PhaseMetres / Speed, 0.05);
	PhaseLandingTime = State.RaceTime + PhaseDuration;
	State.PhaseTimeRemaining = PhaseDuration;
}

void FWSJumpSimulation::ResolvePhase()
{
	// How far off the landing the next takeoff came. No takeoff at all is
	// a full miss — a stumble, which costs distance and does NOT foul: a
	// triple jumper who lands badly still lands somewhere.
	State.HeightAboveGround = 0.0;
	const double Window = FMath::Max(EventSpec.PhaseWindowSeconds, KINDA_SMALL_NUMBER);
	const double Error = State.Phase >= EventSpec.PhaseCount
		? 0.0
		: (PhaseTapTime < 0.0
			? 1.0
			: FMath::Clamp(FMath::Abs(PhaseTapTime - PhaseLandingTime) / Window, 0.0, 1.0));

	// The phase just flown is banked at what its OWN takeoff left it worth;
	// what a mistimed takeoff costs is the phase it launches, because a bad
	// step is short, not a bad hop retroactively — and not a bad jump
	// afterwards either.
	BankedMetres += RawTotal * PhaseShare(State.Phase) * PhaseFactor;

	if (State.Phase >= EventSpec.PhaseCount)
	{
		Land();
		return;
	}

	Outcome.PhaseErrors.Add(Error);
	// Charge the miss to the phase about to be flown, and to that phase
	// alone: the next takeoff decides the next phase on its own merits.
	PhaseFactor = 1.0 - EventSpec.PhaseMissPenalty * Error;
	BeginPhase(State.Phase + 1);
}

void FWSJumpSimulation::Land()
{
	using namespace WSJump;

	if (EventSpec.bVertical)
	{
		// A vertical jump asks a different question: how HIGH, and did the
		// bar survive. There is no foul line here — a high jumper who takes
		// off badly fails to clear, which is not the same as being
		// disqualified, and they get that height again.
		Outcome.HeightMetres = ComputeVerticalHeight();
		Outcome.bCleared = EventSpec.BarMetres <= 0.0
			|| Outcome.HeightMetres >= EventSpec.BarMetres;
		// The MARK of a vertical jump is the bar, not the arc: clearing
		// 2.05m by a foot still records 2.05m, because that is the height
		// the athlete cleared.
		Outcome.DistanceMetres = Outcome.bCleared ? EventSpec.BarMetres : 0.0;
		Outcome.bFoul = !Outcome.bCleared;
		Outcome.bOverstepped = false;
		State.bFinished = true;
		Outcome.bFinished = true;
		return;
	}

	// Projectile range at the athlete's takeoff angle. Technique is what
	// buys the angle: a novice leaves the ground too flat to convert the
	// speed they arrived with into distance.
	const double CappedTechnique =
		FMath::Min(Normalized(Attributes.Technique), GoverningFraction);
	const double AngleDegrees = FMath::Lerp(
		EventSpec.AngleAtZeroTechnique, EventSpec.AngleAtFullTechnique, CappedTechnique);
	const double AngleRadians = FMath::DegreesToRadians(AngleDegrees);
	const double Speed = FMath::Max(Outcome.TakeoffSpeed, 0.0);

	const double Range = Speed * Speed * FMath::Sin(2.0 * AngleRadians) / GravityMs2;
	// A multi-phase jump measures what its phases actually covered — every
	// mistimed takeoff has already been taken out of them — rather than
	// recomputing an ideal the athlete did not jump.
	// What the phases actually covered. For a single-phase jump that is
	// the same number ComputeRawDistance() produced at takeoff; for a
	// triple jump every mistimed takeoff has already come out of it.
	const double Raw = BankedMetres;

	// Every centimetre short of the board is a centimetre off the mark.
	const double Measured = Raw - Outcome.BoardGapMetres;

	// A jump that does not reach the pit is NOT a jump of zero metres — it
	// is a failed attempt, exactly as it is on a real runway.
	//
	// The threshold is the SERVER's plausible minimum, not zero. Between
	// the two lies a band of marks the client would happily measure and the
	// server would then refuse as implausible — a jump the player is shown,
	// told counts, and then told does not. Using the server's own limit
	// means the client never offers a mark that cannot stand.
	if (Measured < EventSpec.MinPlausibleMetres)
	{
		Outcome.bFoul = true;
		Outcome.bOverstepped = false;
		Outcome.DistanceMetres = 0.0;
		State.bFinished = true;
		Outcome.bFinished = true;
		return;
	}

	Outcome.DistanceMetres = FMath::RoundToDouble(Measured * 100.0) / 100.0;
	State.bFinished = true;
	Outcome.bFinished = true;
}

bool FWSJumpSimulation::Step()
{
	using namespace WSJump;

	if (State.bFinished)
	{
		return false;
	}

	while (NextEventIndex < PendingEvents.Num() &&
		PendingEvents[NextEventIndex].TimeSeconds <= State.RaceTime + StepDt)
	{
		ApplyEvent(PendingEvents[NextEventIndex]);
		++NextEventIndex;
		if (State.bFinished)
		{
			return false;
		}
	}

	State.RaceTime += StepDt;

	// --- In the air on a multi-phase jump --------------------------------
	// The runway is behind them: nothing here integrates approach speed,
	// because there is no approach left to run.
	if (State.bAirborne)
	{
		const double Window = FMath::Max(EventSpec.PhaseWindowSeconds, KINDA_SMALL_NUMBER);
		// A hop covers ground. Freezing the athlete on the board for the
		// whole jump would show fifteen metres of travel happening in one
		// place, so the horizontal speed they left with carries them on —
		// which is also what it does in the air.
		State.Distance += FMath::Max(Outcome.TakeoffSpeed, 0.0) * StepDt;
		// Keep this honest while they travel: the board is behind them now,
		// and a readout frozen at the takeoff gap would say otherwise.
		State.MetresToBoard = EventSpec.RunwayMetres - State.Distance;
		State.PhaseTimeRemaining = PhaseLandingTime - State.RaceTime;

		// The arc, for the eye only. A vertical jumper rises to the height
		// they actually reached, so a clearance LOOKS like one; a
		// horizontal jumper rises about half a metre of hang.
		const double Progress = PhaseDuration > 0.0
			? FMath::Clamp(1.0 - State.PhaseTimeRemaining / PhaseDuration, 0.0, 1.0)
			: 1.0;
		const double Peak = EventSpec.bVertical
			? FMath::Max(PeakHeight, 0.30)
			: 0.55;
		State.HeightAboveGround = 4.0 * Peak * Progress * (1.0 - Progress);
		const bool bLastPhase = State.Phase >= EventSpec.PhaseCount;
		State.bPhaseWindowOpen = !bLastPhase && State.PhaseTimeRemaining <= Window;

		// Resolve as soon as the answer is known: on the tap if it came at
		// or after the landing, at the landing if it came before, and at
		// the far edge of the window if it never came at all.
		const bool bLanded = State.PhaseTimeRemaining <= 0.0;
		const bool bTapped = PhaseTapTime >= 0.0;
		if (bLastPhase)
		{
			if (bLanded)
			{
				ResolvePhase();
				return false;
			}
		}
		else if ((bLanded && bTapped) ||
			(bTapped && PhaseTapTime >= PhaseLandingTime) ||
			State.RaceTime >= PhaseLandingTime + Window)
		{
			ResolvePhase();
			if (State.bFinished)
			{
				return false;
			}
		}
		return State.RaceTime < SafetyCapSeconds;
	}

	// --- Cadence accuracy: the approach IS a sprint ---------------------
	State.TargetCadenceHz = TargetCadenceAt(State.Distance);
	double InstantAccuracy = BaselineAccuracy;
	const double SinceLastTap = State.RaceTime - LastTapTime;
	if (LastTapTime >= 0.0 && State.ActualCadenceHz > 0.0 &&
		SinceLastTap < 2.0 / FMath::Max(State.TargetCadenceHz, 0.1))
	{
		// Capped at the governing mean for the same reason every other
		// event caps: the server's ceiling is a function of that mean, so
		// no single attribute may buy a mark the mean does not allow.
		const double CappedStride =
			FMath::Min(Normalized(Attributes.StrideEfficiency), GoverningFraction);
		const double CappedTechnique =
			FMath::Min(Normalized(Attributes.Technique), GoverningFraction);
		const double ToleranceHz = 0.70 + 0.25 * CappedStride + 0.10 * CappedTechnique;
		const double Error = FMath::Abs(State.ActualCadenceHz - State.TargetCadenceHz);
		InstantAccuracy = FMath::Max(BaselineAccuracy, 1.0 - Error / ToleranceHz);
	}
	State.CadenceAccuracy +=
		(InstantAccuracy - State.CadenceAccuracy) * FMath::Min(1.0, 4.0 * StepDt);

	// --- Approach speed -------------------------------------------------
	const double CappedAccel =
		FMath::Min(Normalized(Attributes.Acceleration), GoverningFraction);
	const double Tau = TauAt0 - TauGain * CappedAccel;
	const double AccuracyFactor =
		AccuracyFloor + (1.0 - AccuracyFloor) * State.CadenceAccuracy;
	const double WindFactor = 1.0 + WindPerMs * Wind;
	const double VTarget = VMax * AccuracyFactor * WindFactor;
	State.Speed += (VTarget - State.Speed) / Tau * StepDt;
	State.Distance += State.Speed * StepDt;
	State.MetresToBoard = EventSpec.RunwayMetres - State.Distance;

	// Running through the pit without jumping is a foul. Ending the attempt
	// is the honest outcome; letting them jog on forever is not.
	if (State.Distance > EventSpec.RunwayMetres + OverrunMetres)
	{
		Outcome.bFoul = true;
		Outcome.bOverstepped = true;
		Outcome.DistanceMetres = 0.0;
		State.bFinished = true;
		Outcome.bFinished = true;
		return false;
	}

	return State.RaceTime < SafetyCapSeconds;
}

FWSJumpOutcome FWSJumpSimulation::RunTrace(const FWSSprintAttributes& Attributes,
	uint32 Seed, const TArray<FWSJumpInputEvent>& Trace, const FWSJumpEventSpec& EventSpec)
{
	FWSJumpSimulation Simulation(Attributes, Seed, EventSpec);
	for (const FWSJumpInputEvent& Event : Trace)
	{
		Simulation.AddInput(Event);
	}
	while (Simulation.Step())
	{
	}
	return Simulation.GetOutcome();
}

FString FWSJumpSimulation::DigestTrace(const TArray<FWSJumpInputEvent>& Trace)
{
	FString Payload;
	for (const FWSJumpInputEvent& Event : Trace)
	{
		Payload += FString::Printf(TEXT("%.4f:%d;"),
			Event.TimeSeconds, static_cast<int32>(Event.Type));
	}
	return FMD5::HashAnsiString(*Payload);
}

TArray<FWSJumpInputEvent> FWSJumpSimulation::GenerateAITrace(
	const FWSSprintAttributes& Attributes, uint32 RaceSeed, uint32 InputSeed,
	double Consistency, const FWSJumpEventSpec& EventSpec)
{
	// Closed loop, like the sprint's: the approach is planned against the
	// athlete's ACTUAL position, and the takeoff is called when the board
	// arrives rather than at a time decided in advance.
	FRandomStream Stream(static_cast<int32>(InputSeed ^ 0x9E3779B9u));
	auto Gaussish = [&Stream]()
	{
		return (Stream.FRand() + Stream.FRand() + Stream.FRand()) - 1.5;
	};

	const double Skill = FMath::Clamp(Consistency, 0.0, 1.0);
	const double Sloppiness = 1.0 - Skill;
	const double Jitter = Sloppiness * 0.35;
	// How far short of the board this jumper aims. A good one steps almost
	// onto it; a poor one leaves half a metre of it behind.
	//
	// The floor is NOT zero. Aiming at exactly the board means taking off
	// on the first step that reaches it, and a step covers ~9cm — so the
	// foot lands past the board and the jump is a foul. A flawless jumper
	// aims a few centimetres behind it, which is precisely what a real one
	// does: the board is hit by aiming just short of it, never at it.
	const double AimShort = FMath::Max(0.03,
		Sloppiness * 0.55 + Sloppiness * 0.45 * Gaussish());

	TArray<FWSJumpInputEvent> Trace;
	FWSJumpSimulation Shadow(Attributes, RaceSeed, EventSpec);

	double NextTapAt = 0.12;
	bool bJumped = false;
	int32 TappedPhase = 0;
	while (Shadow.Step())
	{
		const FWSJumpState& Live = Shadow.GetState();
		while (NextTapAt <= Live.RaceTime && !bJumped)
		{
			FWSJumpInputEvent Tap;
			Tap.TimeSeconds = NextTapAt;
			Tap.Type = EWSJumpInputType::Tap;
			Trace.Add(Tap);
			Shadow.AddInput(Tap);

			const double TargetHz =
				FMath::Max(Shadow.TargetCadenceAt(Live.Distance), 0.5);
			const double Interval = 1.0 / TargetHz * (1.0 + Jitter * Gaussish());
			NextTapAt += FMath::Max(Interval, 0.05);
		}
		// Fire while there is still board left. Aiming at a fixed few
		// centimetres is not enough on its own: a stride covers about 9cm
		// of runway per step, so the window can be stepped straight over
		// and the foot lands past the board — a foul from a flawless
		// approach. Looking one step ahead is what a real jumper does with
		// their check marks.
		if (!bJumped && Live.MetresToBoard <= AimShort + Live.Speed * StepDt)
		{
			FWSJumpInputEvent Takeoff;
			Takeoff.TimeSeconds = Live.RaceTime;
			Takeoff.Type = EWSJumpInputType::Takeoff;
			Trace.Add(Takeoff);
			Shadow.AddInput(Takeoff);
			bJumped = true;
		}

		// A hop and a step have to be taken OFF again, and the rhythm of
		// those two takeoffs is the triple jump. A good jumper leaves the
		// ground the instant they touch it; a poor one is early or late,
		// and the phase that follows is the one that is short.
		if (EventSpec.PhaseCount > 1 && Live.bPhaseWindowOpen &&
			Live.Phase != TappedPhase)
		{
			const double Window = FMath::Max(EventSpec.PhaseWindowSeconds, 0.01);
			// Either side of the landing, and never outside the window: a
			// tap the window has already closed on is not a tap at all.
			const double Offset = FMath::Clamp(
				Sloppiness * 0.9 * Window * Gaussish(), -Window * 0.95, Window * 0.95);
			if (Live.PhaseTimeRemaining <= -Offset)
			{
				FWSJumpInputEvent Phase;
				Phase.TimeSeconds = Live.RaceTime;
				Phase.Type = EWSJumpInputType::Takeoff;
				Trace.Add(Phase);
				Shadow.AddInput(Phase);
				TappedPhase = Live.Phase;
			}
		}
	}
	return Trace;
}
