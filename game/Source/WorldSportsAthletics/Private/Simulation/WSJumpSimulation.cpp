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
			break;
		}
		State.bAirborne = true;
		Outcome.TakeoffSpeed = State.Speed;
		// The mark is measured from the BOARD. Taking off past it is a foul
		// and no mark at all; taking off short of it simply costs that gap,
		// because the tape starts at the board either way.
		if (State.Distance > EventSpec.RunwayMetres)
		{
			Outcome.bFoul = true;
			Outcome.bOverstepped = true;
			Outcome.DistanceMetres = 0.0;
			State.bFinished = true;
			Outcome.bFinished = true;
			break;
		}
		Outcome.BoardGapMetres = EventSpec.RunwayMetres - State.Distance;
		Land();
		break;
	}
}

void FWSJumpSimulation::Land()
{
	using namespace WSJump;

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
	const double Raw = EventSpec.FlightScale * Range + EventSpec.LandingBonusMetres;

	// Every centimetre short of the board is a centimetre off the mark.
	const double Measured = Raw - Outcome.BoardGapMetres;

	// A jump that does not reach the pit is NOT a jump of zero metres — it
	// is a failed attempt, exactly as it is on a real runway. Recording it
	// as 0.00m claimed a measurement that was never taken, and the server
	// would refuse it anyway for being below the plausible minimum.
	if (Measured <= 0.0)
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
	}
	return Trace;
}
