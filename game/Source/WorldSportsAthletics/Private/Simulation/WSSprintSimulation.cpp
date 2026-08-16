#include "Simulation/WSSprintSimulation.h"

#include "Math/RandomStream.h"
#include "Misc/SecureHash.h"

namespace WSSprint
{
// Calibrated so PERFECT play lands just above the server's attribute
// ceiling (13.5s @ mean 0 -> 9.55s @ mean 100). The calibration automation
// test asserts this against the exact server formula; retune here, never
// by widening the test.
constexpr double VMaxAt0 = 8.35;       // m/s top speed, 0 attributes
constexpr double VMaxAt100 = 11.63;    // m/s top speed, maxed
// Slightly concave in the attribute: the server ceiling is linear in the
// attribute MEAN while race time is ~1/V, so a linear V dips under the
// ceiling chord mid-range. The exponent keeps a safety margin everywhere.
constexpr double MaxSpeedCurve = 1.22;
constexpr double TauAt0 = 1.35;        // s, acceleration time constant
constexpr double TauGain = 0.45;       // tau shrinks with acceleration attr
constexpr double AccuracyFloor = 0.66; // v_target factor at accuracy 0
constexpr double FatiguePenalty = 0.13;
constexpr double FatigueRate = 0.055;
constexpr double WindPerMs = 0.004;    // v_target factor per m/s of wind
constexpr double BaselineAccuracy = 0.25; // "jogging" — no rhythm at all
constexpr double LeanWindowStart = 95.0;
constexpr double LeanBonus = 0.04;     // metres
constexpr double LeanStumbleFactor = 0.985;
constexpr double AutoReleaseSeconds = 1.5;
constexpr double PreGunSeconds = 3.0;
constexpr double SafetyCapSeconds = 90.0;

double Normalized(float Attr)
{
	return FMath::Clamp(static_cast<double>(Attr), 0.0, 100.0) / 100.0;
}
}

FWSSprintSimulation::FWSSprintSimulation(const FWSSprintAttributes& InAttributes, uint32 InSeed)
	: Attributes(InAttributes)
{
	State.RaceTime = -WSSprint::PreGunSeconds;

	// Every random element of a race comes from this stream, so the seed
	// string submitted with the result replays the race exactly.
	FRandomStream Stream(static_cast<int32>(InSeed));
	// Mostly legal wind, occasionally an illegal tailwind the server will
	// (correctly) refuse to rank: honesty over convenience.
	Wind = -1.5 + 3.5 * Stream.FRand();
	BandDriftPhase = Stream.FRand() * 2.0 * PI;
	Outcome.Wind = FMath::RoundToDouble(Wind * 10.0) / 10.0;
}

double FWSSprintSimulation::TargetCadenceAt(double DistanceMetres) const
{
	// Drive: ramp up. Transition/top: hold. Fatigue zone: slightly lower
	// and DRIFTING — holding rhythm here is the skill ceiling.
	if (DistanceMetres < 30.0)
	{
		return 3.2 + (4.6 - 3.2) * (DistanceMetres / 30.0);
	}
	if (DistanceMetres < 85.0)
	{
		return 4.6;
	}
	const double Drift =
		0.25 * FMath::Sin(2.0 * PI * 0.4 * State.RaceTime + BandDriftPhase);
	return 4.45 + Drift;
}

void FWSSprintSimulation::AddInput(const FWSSprintInputEvent& Event)
{
	PendingEvents.Add(Event);
}

void FWSSprintSimulation::ApplyEvent(const FWSSprintInputEvent& Event)
{
	switch (Event.Type)
	{
	case EWSSprintInputType::HoldStart:
		bHeld = true;
		break;

	case EWSSprintInputType::Release:
		if (State.bReleased)
		{
			break;
		}
		if (Event.TimeSeconds < 0.0)
		{
			// Left the blocks before the gun.
			State.bFalseStart = true;
			Outcome.bFalseStart = true;
			Outcome.ReactionMs = Event.TimeSeconds * 1000.0;
			break;
		}
		ReactionSeconds = Event.TimeSeconds;
		Outcome.ReactionMs = FMath::RoundToDouble(ReactionSeconds * 1000.0);
		if (Outcome.ReactionMs < FalseStartFloorMs)
		{
			// Rule-accurate: sub-100ms cannot be a reaction to the gun.
			State.bFalseStart = true;
			Outcome.bFalseStart = true;
			break;
		}
		State.bReleased = true;
		LastSplitTime = ReactionSeconds;
		break;

	case EWSSprintInputType::Tap:
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
				// Cadence over the last two strides — resistant to a single
				// early/late tap, still responsive.
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

	case EWSSprintInputType::Lean:
		if (!State.bReleased || bLeanUsed)
		{
			break;
		}
		bLeanUsed = true;
		if (State.Distance >= WSSprint::LeanWindowStart)
		{
			LeanBonusMetres = WSSprint::LeanBonus;
		}
		else
		{
			// An early dip is a stumble, not a dip.
			State.Speed *= WSSprint::LeanStumbleFactor;
		}
		break;
	}
}

bool FWSSprintSimulation::Step()
{
	using namespace WSSprint;

	if (State.bFinished || State.bFalseStart)
	{
		return false;
	}

	// Apply every input due by the end of this step, in arrival order.
	while (NextEventIndex < PendingEvents.Num() &&
		PendingEvents[NextEventIndex].TimeSeconds <= State.RaceTime + StepDt)
	{
		ApplyEvent(PendingEvents[NextEventIndex]);
		++NextEventIndex;
		if (State.bFalseStart)
		{
			return false;
		}
	}

	const double PrevDistance = State.Distance;
	State.RaceTime += StepDt;

	// Blocks: nothing moves until the athlete releases. A statue in the
	// blocks eventually gets an automatic (terrible) start so the race
	// always completes.
	if (!State.bReleased)
	{
		if (State.RaceTime >= AutoReleaseSeconds)
		{
			FWSSprintInputEvent Auto;
			Auto.Type = EWSSprintInputType::Release;
			Auto.TimeSeconds = AutoReleaseSeconds;
			ApplyEvent(Auto);
			if (State.bFalseStart)
			{
				return false;
			}
		}
		if (!State.bReleased)
		{
			return State.RaceTime < SafetyCapSeconds;
		}
	}

	// --- Cadence accuracy (the skill signal) ---------------------------
	State.TargetCadenceHz = TargetCadenceAt(State.Distance);
	double InstantAccuracy = BaselineAccuracy;
	const double SinceLastTap = State.RaceTime - LastTapTime;
	if (LastTapTime >= 0.0 && State.ActualCadenceHz > 0.0 &&
		SinceLastTap < 2.0 / FMath::Max(State.TargetCadenceHz, 0.1))
	{
		// A wide-ish base band keeps casual play readable; the attribute
		// bonus is deliberately small so high attributes cannot shield bad
		// rhythm — attributes raise the ceiling, execution decides.
		const double ToleranceHz = 0.70 +
			0.25 * Normalized(Attributes.StrideEfficiency) +
			0.10 * Normalized(Attributes.Technique);
		const double Error =
			FMath::Abs(State.ActualCadenceHz - State.TargetCadenceHz);
		InstantAccuracy = FMath::Max(
			BaselineAccuracy, 1.0 - Error / ToleranceHz);
	}
	// Smooth so one tap can't spike the whole model.
	State.CadenceAccuracy +=
		(InstantAccuracy - State.CadenceAccuracy) * FMath::Min(1.0, 4.0 * StepDt);

	// --- Speed ---------------------------------------------------------
	// Top speed follows the SERVER's ceiling input (the mean of the governing
	// attributes), not MaxSpeed alone — see FWSSprintAttributes::GoverningMean.
	// Individual attributes still shape the race through acceleration (tau),
	// stamina (fatigue) and stride efficiency/technique (the tolerance band);
	// they cost or save time within the ceiling rather than beating it.
	const double GoverningFraction =
		FMath::Clamp(Attributes.GoverningMean(), 0.0, 100.0) / 100.0;
	const double VMax = VMaxAt0 + (VMaxAt100 - VMaxAt0) *
		FMath::Pow(GoverningFraction, MaxSpeedCurve);
	const double Tau = TauAt0 - TauGain * Normalized(Attributes.Acceleration);
	const double AccuracyFactor =
		AccuracyFloor + (1.0 - AccuracyFloor) * State.CadenceAccuracy;
	const double FatigueFactor = 1.0 - FatiguePenalty * State.Fatigue;
	const double WindFactor = 1.0 + WindPerMs * Wind;
	const double VTarget = VMax * AccuracyFactor * FatigueFactor * WindFactor;

	State.Speed += (VTarget - State.Speed) / Tau * StepDt;
	State.Distance += State.Speed * StepDt + LeanBonusMetres;
	LeanBonusMetres = 0.0;

	// --- Fatigue -------------------------------------------------------
	const double Intensity = VMax > 0.0 ? State.Speed / VMax : 0.0;
	State.Fatigue += FatigueRate *
		Intensity * Intensity * Intensity *
		(1.35 - 0.65 * Normalized(Attributes.Stamina)) *
		(1.45 - 0.65 * State.CadenceAccuracy) * StepDt;
	State.Fatigue = FMath::Min(State.Fatigue, 1.0);

	// --- 10m marks and the finish --------------------------------------
	while (NextSplitMark <= 10 &&
		State.Distance >= NextSplitMark * 10.0 &&
		PrevDistance < State.Distance)
	{
		const double Mark = NextSplitMark * 10.0;
		const double Alpha = (Mark - PrevDistance) / (State.Distance - PrevDistance);
		const double CrossTime = (State.RaceTime - StepDt) + Alpha * StepDt;
		Outcome.Splits.Add(CrossTime - LastSplitTime);
		LastSplitTime = CrossTime;
		if (NextSplitMark == 10)
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
	return State.RaceTime < SafetyCapSeconds;
}

FWSSprintOutcome FWSSprintSimulation::RunTrace(const FWSSprintAttributes& Attributes,
	uint32 Seed, const TArray<FWSSprintInputEvent>& Trace)
{
	FWSSprintSimulation Simulation(Attributes, Seed);
	for (const FWSSprintInputEvent& Event : Trace)
	{
		Simulation.AddInput(Event);
	}
	while (Simulation.Step())
	{
	}
	return Simulation.GetOutcome();
}

FString FWSSprintSimulation::DigestTrace(const TArray<FWSSprintInputEvent>& Trace)
{
	FString Serial;
	Serial.Reserve(Trace.Num() * 16);
	for (const FWSSprintInputEvent& Event : Trace)
	{
		Serial += FString::Printf(TEXT("%.4f:%d;"),
			Event.TimeSeconds, static_cast<int32>(Event.Type));
	}
	const FTCHARToUTF8 Utf8(*Serial);
	FSHA1 Sha;
	Sha.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	Sha.Final();
	uint8 Hash[FSHA1::DigestSize];
	Sha.GetHash(Hash);
	return BytesToHex(Hash, FSHA1::DigestSize).Left(40).ToLower();
}

TArray<FWSSprintInputEvent> FWSSprintSimulation::GenerateAITrace(
	const FWSSprintAttributes& Attributes, uint32 RaceSeed, uint32 InputSeed,
	double ReactionMeanMs, double ReactionSpreadMs, double Consistency)
{
	// The AI plays the game: it produces INPUT — a reaction and a tap
	// rhythm of profile-dependent quality — and its time emerges from the
	// same simulation the player runs. It cannot be given a finish time.
	//
	// Generation is CLOSED-LOOP: a simulation runs while the trace is
	// generated, and each tap targets the band at the athlete's actual
	// distance — exactly what a human tracking the HUD band does. An
	// open-loop time schedule would silently punish slow athletes for
	// being behind the curve, which is an artifact, not a skill.
	FRandomStream Stream(static_cast<int32>(InputSeed ^ 0x9E3779B9u));
	auto Gaussish = [&Stream]() // sum of 3 uniforms ~ bell curve in [-1.5, 1.5]
	{
		return (Stream.FRand() + Stream.FRand() + Stream.FRand()) - 1.5;
	};

	TArray<FWSSprintInputEvent> Trace;
	Trace.Add({-2.0, EWSSprintInputType::HoldStart});

	const double ReactionMs = FMath::Clamp(
		ReactionMeanMs + ReactionSpreadMs * Gaussish(), 110.0, 500.0);
	const double ReleaseTime = ReactionMs / 1000.0;
	Trace.Add({ReleaseTime, EWSSprintInputType::Release});

	const double Sloppiness = 1.0 - FMath::Clamp(Consistency, 0.0, 1.0);
	const double Jitter = Sloppiness * 0.35;
	const double MissChance = Sloppiness * 0.08;
	// White jitter averages out over the two-tap cadence window; what
	// actually separates skill tiers is DRIFT — sustained stretches off the
	// band ("can't find the rhythm"). Slow sinusoid, seeded phase/rate.
	const double BiasAmplitude = Sloppiness * 0.26;
	const double BiasPhase = Stream.FRand() * 2.0 * PI;
	const double BiasRateHz = 0.18 + 0.14 * Stream.FRand();

	// Plan against the ACTUAL race conditions, not a different draw.
	FWSSprintSimulation Shadow(Attributes, RaceSeed);
	for (const FWSSprintInputEvent& Event : Trace)
	{
		Shadow.AddInput(Event);
	}

	double NextTapAt = ReleaseTime + 0.12; // first stride off the blocks
	while (Shadow.Step())
	{
		const FWSSprintState& Live = Shadow.GetState();
		if (!Live.bReleased)
		{
			continue;
		}
		while (NextTapAt <= Live.RaceTime)
		{
			const double TargetHz =
				FMath::Max(Shadow.TargetCadenceAt(Live.Distance), 0.5);
			// Tanh-shaped: the drift SITS off-band for whole stretches
			// instead of brushing through zero — that sustained error is
			// what actually distinguishes a lost rhythm from a wobble.
			const double Bias = BiasAmplitude * FMath::Tanh(
				2.5 * FMath::Sin(2.0 * PI * BiasRateHz * NextTapAt + BiasPhase));
			const double Interval =
				(1.0 / TargetHz) * (1.0 + Jitter * Gaussish() * 0.5 + Bias);
			const bool bMissed = Stream.FRand() < MissChance;
			if (!bMissed)
			{
				const FWSSprintInputEvent Tap{NextTapAt, EWSSprintInputType::Tap};
				Trace.Add(Tap);
				Shadow.AddInput(Tap);
			}
			NextTapAt += FMath::Max(Interval, 0.05);
		}
	}
	return Trace;
}
