#include "Simulation/WSThrowSimulation.h"

#include "Math/RandomStream.h"
#include "Misc/SecureHash.h"

namespace WSThrow
{
constexpr double GravityMs2 = 9.81;
/** Fraction of best speed available at the very start of the wind-up. */
constexpr double PowerFloor = 0.42;
/** How steeply power falls away either side of the peak. */
constexpr double RiseSharpness = 1.55;
constexpr double FallSharpness = 2.40;

double Normalized(float Attr)
{
	return FMath::Clamp(static_cast<double>(Attr), 0.0, 100.0) / 100.0;
}
}

namespace WSThrowEvents
{
const TArray<FWSThrowEventSpec>& All()
{
	static const TArray<FWSThrowEventSpec> Events = []
	{
		TArray<FWSThrowEventSpec> Table;

		FWSThrowEventSpec Shot;
		Shot.Code = TEXT("throw-shot");
		Shot.DisplayName = TEXT("Shot Put");
		Shot.Attempts = 3;
		Shot.CeilingAtZero = 4.50;
		Shot.CeilingAtHundred = 23.00;
		Shot.MinPlausibleMetres = 1.00;
		Shot.MaxPlausibleMetres = 24.00;
		Shot.WindUpSeconds = 2.30;
		Shot.PeakFraction = 0.72;
		Shot.SpeedAtZero = 5.15;
		Shot.SpeedAtHundred = 14.05;
		Shot.TopSpeedCurve = 0.72;
		Shot.AngleAtZeroTechnique = 28.0;
		Shot.AngleAtFullTechnique = 38.0;
		Shot.ReleaseHeightMetres = 2.10;
		Shot.GoverningAttributes = {
			TEXT("acceleration"), TEXT("max_speed"), TEXT("technique")};
		Table.Add(Shot);

		// Discus and javelin are ROWS, not code. Both are a wind-up and a
		// release from a circle or runway; what differs is how far the
		// implement carries, which is a handful of numbers. The effective
		// release speeds are higher than a shot's because both implements
		// fly rather than fall — a projectile model with no lift needs the
		// speed to stand in for the aerodynamics.
		FWSThrowEventSpec Discus;
		Discus.Code = TEXT("throw-discus");
		Discus.DisplayName = TEXT("Discus");
		Discus.Attempts = 3;
		Discus.CeilingAtZero = 12.00;
		Discus.CeilingAtHundred = 73.00;
		Discus.MinPlausibleMetres = 5.00;
		Discus.MaxPlausibleMetres = 75.00;
		Discus.WindUpSeconds = 2.60;   // a longer turn than the shot
		Discus.PeakFraction = 0.75;
		Discus.SpeedAtZero = 10.30;
		Discus.SpeedAtHundred = 26.70;
		Discus.TopSpeedCurve = 0.72;
		Discus.AngleAtZeroTechnique = 30.0;
		Discus.AngleAtFullTechnique = 37.0;
		Discus.ReleaseHeightMetres = 1.60;
		Discus.GoverningAttributes = {
			TEXT("acceleration"), TEXT("max_speed"), TEXT("technique")};
		Table.Add(Discus);

		FWSThrowEventSpec Javelin;
		Javelin.Code = TEXT("throw-javelin");
		Javelin.DisplayName = TEXT("Javelin");
		Javelin.Attempts = 3;
		Javelin.CeilingAtZero = 15.00;
		Javelin.CeilingAtHundred = 97.00;
		Javelin.MinPlausibleMetres = 5.00;
		Javelin.MaxPlausibleMetres = 100.00;
		Javelin.WindUpSeconds = 2.10;  // a run-up and a fast arm
		Javelin.PeakFraction = 0.78;
		Javelin.SpeedAtZero = 11.60;
		Javelin.SpeedAtHundred = 30.90;
		Javelin.TopSpeedCurve = 0.72;
		Javelin.AngleAtZeroTechnique = 31.0;
		Javelin.AngleAtFullTechnique = 36.0;
		Javelin.ReleaseHeightMetres = 1.90;
		Javelin.bFromCircle = false;   // a runway and an arc, not a circle
		Javelin.GoverningAttributes = {
			TEXT("acceleration"), TEXT("max_speed"), TEXT("technique")};
		Table.Add(Javelin);

		return Table;
	}();
	return Events;
}

const FWSThrowEventSpec& Find(const FString& Code)
{
	for (const FWSThrowEventSpec& Spec : All())
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

FWSThrowSimulation::FWSThrowSimulation(const FWSSprintAttributes& InAttributes,
	uint32 InSeed, const FWSThrowEventSpec& InEventSpec)
	: Attributes(InAttributes)
	, EventSpec(InEventSpec)
{
	FRandomStream Stream(static_cast<int32>(InSeed));
	// No wind is modelled: the sport does not record it for throws. The seed
	// still varies the attempt — where the power peaks drifts slightly, so
	// the athlete has to feel the throw rather than count to a number.
	PeakDriftPhase = Stream.FRand() * 2.0 * PI;

	GoverningFraction = FMath::Clamp(
		Attributes.GoverningMean(EventSpec.GoverningAttributes), 0.0, 100.0) / 100.0;
	BestSpeed = EventSpec.SpeedAtZero + (EventSpec.SpeedAtHundred - EventSpec.SpeedAtZero)
		* FMath::Pow(GoverningFraction, EventSpec.TopSpeedCurve);
}

void FWSThrowSimulation::AddInput(const FWSThrowInputEvent& Event)
{
	PendingEvents.Add(Event);
}

void FWSThrowSimulation::ApplyEvent(const FWSThrowInputEvent& Event)
{
	using namespace WSThrow;

	if (Event.Type != EWSThrowInputType::Release || State.bReleased)
	{
		return;
	}
	State.bReleased = true;

	// Speed at the moment of release, which is what the throw is made of.
	const double Speed = BestSpeed * State.Power;
	Outcome.ReleaseSpeed = Speed;

	// Technique buys the angle, as it does in the jump: a novice releases
	// too flat to convert the speed they generated into distance.
	const double CappedTechnique =
		FMath::Min(Normalized(Attributes.Technique), GoverningFraction);
	const double AngleRadians = FMath::DegreesToRadians(FMath::Lerp(
		EventSpec.AngleAtZeroTechnique, EventSpec.AngleAtFullTechnique, CappedTechnique));

	// Projectile range from a HEIGHT, which is why a shot carries further
	// than the flat-ground formula and why the best angle is under 45.
	const double SinA = FMath::Sin(AngleRadians);
	const double CosA = FMath::Cos(AngleRadians);
	const double Vertical = Speed * SinA;
	const double Root = FMath::Sqrt(
		FMath::Max(0.0, Vertical * Vertical + 2.0 * GravityMs2 * EventSpec.ReleaseHeightMetres));
	const double Range = Speed * CosA * (Vertical + Root) / GravityMs2;

	// A throw the sport would not record is no mark, not a tiny one — and
	// the floor is the SERVER's plausible minimum so the client never
	// measures a mark the server then refuses.
	if (Range < EventSpec.MinPlausibleMetres)
	{
		Outcome.bFoul = true;
		Outcome.bCarriedOut = false;
		Outcome.DistanceMetres = 0.0;
	}
	else
	{
		Outcome.DistanceMetres = FMath::RoundToDouble(Range * 100.0) / 100.0;
	}

	State.bFinished = true;
	Outcome.bFinished = true;
}

bool FWSThrowSimulation::Step()
{
	using namespace WSThrow;

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
	State.WindUp = EventSpec.WindUpSeconds > 0.0
		? State.RaceTime / EventSpec.WindUpSeconds
		: 1.0;

	// Power rises to a peak and then falls away: a throw held past the peak
	// is one the athlete has already begun to unwind from. The peak drifts
	// slightly with the seed so it has to be felt, not counted.
	const double Peak = FMath::Clamp(
		EventSpec.PeakFraction + 0.03 * FMath::Sin(PeakDriftPhase), 0.15, 0.95);
	const double Fraction = FMath::Clamp(State.WindUp, 0.0, 1.0);
	double Shape = 0.0;
	if (Fraction <= Peak)
	{
		const double Rise = Peak > 0.0 ? Fraction / Peak : 1.0;
		Shape = FMath::Pow(Rise, RiseSharpness);
	}
	else
	{
		const double Fall = (Fraction - Peak) / FMath::Max(1.0 - Peak, KINDA_SMALL_NUMBER);
		Shape = FMath::Max(0.0, 1.0 - FMath::Pow(Fall, FallSharpness));
	}
	State.Power = PowerFloor + (1.0 - PowerFloor) * Shape;
	Outcome.TimingError = FMath::Abs(Fraction - Peak);

	// Never letting go carries the throw out of the circle. That is a foul
	// and no mark, exactly like a jumper stepping past the board.
	if (State.RaceTime >= EventSpec.WindUpSeconds)
	{
		Outcome.bFoul = true;
		Outcome.bCarriedOut = true;
		Outcome.DistanceMetres = 0.0;
		State.bFinished = true;
		Outcome.bFinished = true;
		return false;
	}
	return true;
}

FWSThrowOutcome FWSThrowSimulation::RunTrace(const FWSSprintAttributes& Attributes,
	uint32 Seed, const TArray<FWSThrowInputEvent>& Trace, const FWSThrowEventSpec& EventSpec)
{
	FWSThrowSimulation Simulation(Attributes, Seed, EventSpec);
	for (const FWSThrowInputEvent& Event : Trace)
	{
		Simulation.AddInput(Event);
	}
	while (Simulation.Step())
	{
	}
	return Simulation.GetOutcome();
}

FString FWSThrowSimulation::DigestTrace(const TArray<FWSThrowInputEvent>& Trace)
{
	FString Payload;
	for (const FWSThrowInputEvent& Event : Trace)
	{
		Payload += FString::Printf(TEXT("%.4f:%d;"),
			Event.TimeSeconds, static_cast<int32>(Event.Type));
	}
	return FMD5::HashAnsiString(*Payload);
}

TArray<FWSThrowInputEvent> FWSThrowSimulation::GenerateAITrace(
	const FWSSprintAttributes& Attributes, uint32 RaceSeed, uint32 InputSeed,
	double Consistency, const FWSThrowEventSpec& EventSpec)
{
	// Closed loop: the thrower watches their own wind-up and lets go when it
	// peaks, which is what the player does. Planning a release time in
	// advance would ignore the drift the seed applies to the peak.
	FRandomStream Stream(static_cast<int32>(InputSeed ^ 0x9E3779B9u));
	auto Gaussish = [&Stream]()
	{
		return (Stream.FRand() + Stream.FRand() + Stream.FRand()) - 1.5;
	};

	const double Skill = FMath::Clamp(Consistency, 0.0, 1.0);
	const double Sloppiness = 1.0 - Skill;
	// A poor thrower is early or late on the peak, either side of it.
	const double Error = Sloppiness * 0.30 * Gaussish();

	TArray<FWSThrowInputEvent> Trace;
	FWSThrowSimulation Shadow(Attributes, RaceSeed, EventSpec);

	double BestPower = 0.0;
	double BestTime = -1.0;
	bool bFalling = false;
	while (Shadow.Step())
	{
		const FWSThrowState& Live = Shadow.GetState();
		if (Live.Power > BestPower)
		{
			BestPower = Live.Power;
			BestTime = Live.RaceTime;
			continue;
		}
		// Power has started to fall: the peak was the last sample.
		bFalling = true;
		break;
	}

	if (BestTime < 0.0)
	{
		return Trace; // never found a peak; no release is a foul, honestly
	}

	FWSThrowInputEvent Release;
	Release.TimeSeconds = FMath::Max(0.0,
		BestTime + Error * EventSpec.WindUpSeconds);
	Release.Type = EWSThrowInputType::Release;
	Trace.Add(Release);
	return Trace;
}
