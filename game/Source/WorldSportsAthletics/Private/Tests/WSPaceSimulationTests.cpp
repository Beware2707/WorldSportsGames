#include "Misc/AutomationTest.h"
#include "Simulation/WSPaceSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FWSSprintAttributes UniformAttributes(float Level)
{
	FWSSprintAttributes Attributes;
	Attributes.Reaction = Level;
	Attributes.Acceleration = Level;
	Attributes.MaxSpeed = Level;
	Attributes.StrideEfficiency = Level;
	Attributes.Stamina = Level;
	Attributes.Recovery = Level;
	Attributes.Technique = Level;
	return Attributes;
}

/** The server's exact ceiling formula (backend/app/services/career.py). */
double ServerCeiling(const FWSPaceEventSpec& Spec, double MeanAttr)
{
	return Spec.CeilingAtZero + (Spec.CeilingAtHundred - Spec.CeilingAtZero)
		* FMath::Clamp(MeanAttr, 0.0, 100.0) / 100.0;
}

TArray<FWSPaceInputEvent> EvenEffort(double Effort)
{
	TArray<FWSPaceInputEvent> Trace;
	FWSPaceInputEvent Event;
	Event.TimeSeconds = 0.0;
	Event.Type = EWSPaceInputType::SetEffort;
	Event.Value = Effort;
	Trace.Add(Event);
	return Trace;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSPaceDeterminismTest,
	"WorldSports.Pace.DeterministicReplay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSPaceDeterminismTest::RunTest(const FString&)
{
	// Same athlete + seed + trace must reproduce the identical race, or
	// server-side replay and the finish replay are both fiction.
	const FWSSprintAttributes Attributes = UniformAttributes(55.0f);
	const FWSPaceEventSpec& Spec = WSPaceEvents::Find(TEXT("middle-800m"));
	const TArray<FWSPaceInputEvent> Trace = EvenEffort(0.72);

	const FWSPaceOutcome First =
		FWSMiddleDistanceSimulation::RunTrace(Attributes, 42u, Trace, Spec);
	const FWSPaceOutcome Second =
		FWSMiddleDistanceSimulation::RunTrace(Attributes, 42u, Trace, Spec);

	TestTrue(TEXT("finished"), First.bFinished && Second.bFinished);
	TestEqual(TEXT("identical time"), First.TimeSeconds, Second.TimeSeconds);
	TestEqual(TEXT("identical split count"), First.Splits.Num(), Second.Splits.Num());
	for (int32 Index = 0; Index < First.Splits.Num(); ++Index)
	{
		TestEqual(TEXT("identical split"), First.Splits[Index], Second.Splits[Index]);
	}

	// A different seed drifts the sustainable band differently, so it is a
	// different race — otherwise the seed would be decoration.
	const FWSPaceOutcome Other =
		FWSMiddleDistanceSimulation::RunTrace(Attributes, 43u, Trace, Spec);
	TestNotEqual(TEXT("different seed differs"), First.TimeSeconds, Other.TimeSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSPaceCeilingTest,
	"WorldSports.Pace.PerfectPacingRespectsServerCeiling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSPaceCeilingTest::RunTest(const FString&)
{
	// The integration guarantee, event by event and across a wide seed
	// sweep: the best race this athlete can pace must never beat the
	// ceiling the server will hold them to.
	for (const FWSPaceEventSpec& Spec : WSPaceEvents::All())
	{
		const double NearBand = 0.30 * Spec.CeilingAtZero;
		for (const float Level : {0.0f, 25.0f, 40.0f, 55.0f, 70.0f, 85.0f, 100.0f})
		{
			const FWSSprintAttributes Attributes = UniformAttributes(Level);
			const double Ceiling = ServerCeiling(Spec, Level);

			double WorstMargin = TNumericLimits<double>::Max();
			double WorstTime = 0.0;
			uint32 WorstSeed = 0;
			double SlowestTime = 0.0;
			for (uint32 Seed = 1; Seed <= 32; ++Seed)
			{
				const TArray<FWSPaceInputEvent> Trace =
					FWSMiddleDistanceSimulation::GeneratePerfectTrace(Attributes, Seed, Spec);
				const FWSPaceOutcome Outcome =
					FWSMiddleDistanceSimulation::RunTrace(Attributes, Seed, Trace, Spec);
				TestTrue(FString::Printf(TEXT("%s attrs %.0f seed %u finished"),
						*Spec.Code, Level, Seed),
					Outcome.bFinished);
				TestEqual(FString::Printf(TEXT("%s split count"), *Spec.Code),
					Outcome.Splits.Num(), Spec.SplitCount);
				if (Outcome.TimeSeconds - Ceiling < WorstMargin)
				{
					WorstMargin = Outcome.TimeSeconds - Ceiling;
					WorstTime = Outcome.TimeSeconds;
					WorstSeed = Seed;
				}
				SlowestTime = FMath::Max(SlowestTime, Outcome.TimeSeconds);
			}

			AddInfo(FString::Printf(
				TEXT("PACE %-13s attrs %3.0f  worst %7.3f (seed %2u)")
				TEXT("  ceiling %7.3f  margin %+.3f  slowest %7.3f"),
				*Spec.Code, Level, WorstTime, WorstSeed, Ceiling, WorstMargin, SlowestTime));

			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f seed %u: %.3f must be >= ceiling %.3f"),
					*Spec.Code, Level, WorstSeed, WorstTime, Ceiling),
				WorstMargin >= -0.009);
			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: slowest perfect run %.3f should stay near ceiling %.3f"),
					*Spec.Code, Level, SlowestTime, Ceiling),
				SlowestTime <= Ceiling + NearBand);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSPaceJudgementDecidesTest,
	"WorldSports.Pace.PaceJudgementDecidesTheRace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSPaceJudgementDecidesTest::RunTest(const FString&)
{
	// The anti-pay-to-win claim, stated as a test: attributes raise the
	// ceiling, execution decides the race. A well-paced run by a MODEST
	// athlete must beat a badly-paced run by a much better one.
	const FWSPaceEventSpec& Spec = WSPaceEvents::Find(TEXT("middle-800m"));
	const FWSSprintAttributes Modest = UniformAttributes(55.0f);
	const FWSSprintAttributes Strong = UniformAttributes(75.0f);

	for (const uint32 Seed : {5u, 61u, 909u})
	{
		const FWSPaceOutcome WellPaced = FWSMiddleDistanceSimulation::RunTrace(
			Modest, Seed,
			FWSMiddleDistanceSimulation::GeneratePerfectTrace(Modest, Seed, Spec), Spec);
		// Suicide pace: flat out from the gun, which empties the tank and
		// leaves the athlete dying down the home straight.
		const FWSPaceOutcome Reckless = FWSMiddleDistanceSimulation::RunTrace(
			Strong, Seed, EvenEffort(1.0), Spec);

		TestTrue(TEXT("both finished"), WellPaced.bFinished && Reckless.bFinished);
		// Where the tank ran dry is the whole distinction. Flat-out from the
		// gun comes apart with most of the race left; a judged pace, if it
		// empties at all, does so on the closing stretch after the kick —
		// which is what finishing an 800m looks like.
		TestTrue(TEXT("going out flat out empties the tank"), Reckless.bWalled);
		TestTrue(FString::Printf(TEXT("reckless collapses early (at %.2f of the race)"),
				Reckless.WallAtFraction),
			Reckless.WallAtFraction >= 0.0 && Reckless.WallAtFraction < 0.6);
		TestTrue(FString::Printf(TEXT("a judged pace lasts (empty at %.2f)"),
				WellPaced.WallAtFraction),
			WellPaced.WallAtFraction < 0.0 || WellPaced.WallAtFraction > 0.75);
		TestTrue(FString::Printf(
				TEXT("seed %u: judged %.2f must beat reckless %.2f"),
				Seed, WellPaced.TimeSeconds, Reckless.TimeSeconds),
			WellPaced.TimeSeconds < Reckless.TimeSeconds);
	}

	// And the converse, so this is not just a test that bad play is bad:
	// with pace judgement equal, the better athlete wins.
	for (const uint32 Seed : {5u, 61u, 909u})
	{
		const FWSPaceOutcome ModestRun = FWSMiddleDistanceSimulation::RunTrace(
			Modest, Seed,
			FWSMiddleDistanceSimulation::GeneratePerfectTrace(Modest, Seed, Spec), Spec);
		const FWSPaceOutcome StrongRun = FWSMiddleDistanceSimulation::RunTrace(
			Strong, Seed,
			FWSMiddleDistanceSimulation::GeneratePerfectTrace(Strong, Seed, Spec), Spec);
		TestTrue(TEXT("equal judgement: attributes decide"),
			StrongRun.TimeSeconds < ModestRun.TimeSeconds);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSPaceSplitsTest,
	"WorldSports.Pace.SplitsSatisfyServerValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSPaceSplitsTest::RunTest(const FString&)
{
	// The server's checks, per event: the expected split count, each split
	// above that event's floor, and the splits summing to the official time.
	// There is no reaction to add here, which is the point — these events
	// have no blocks and the server row says requires_reaction=False.
	for (const FWSPaceEventSpec& Spec : WSPaceEvents::All())
	{
		for (const uint32 Seed : {11u, 222u, 3333u})
		{
			const FWSSprintAttributes Attributes = UniformAttributes(60.0f);
			const FWSPaceOutcome Outcome = FWSMiddleDistanceSimulation::RunTrace(
				Attributes, Seed,
				FWSMiddleDistanceSimulation::GenerateAITrace(
					Attributes, Seed, Seed, 0.8, Spec), Spec);

			TestTrue(TEXT("finished"), Outcome.bFinished);
			TestEqual(FString::Printf(TEXT("%s split count"), *Spec.Code),
				Outcome.Splits.Num(), Spec.SplitCount);

			double Sum = 0.0;
			for (const double Split : Outcome.Splits)
			{
				TestTrue(FString::Printf(TEXT("%s split %.3f >= server floor %.2f"),
						*Spec.Code, Split, Spec.MinSplitSeconds),
					Split >= Spec.MinSplitSeconds);
				Sum += Split;
			}
			const double Tolerance = FMath::Max(0.025, 0.005 * Outcome.TimeSeconds);
			TestTrue(FString::Printf(TEXT("%s splits (%.3f) sum to the time (%.3f)"),
					*Spec.Code, Sum, Outcome.TimeSeconds),
				FMath::Abs(Sum - Outcome.TimeSeconds) <= Tolerance);
			TestTrue(FString::Printf(
					TEXT("%s time %.3f inside the plausible band [%.1f, %.1f]"),
					*Spec.Code, Outcome.TimeSeconds,
					Spec.MinPlausibleSeconds, Spec.MaxPlausibleSeconds),
				Outcome.TimeSeconds > Spec.MinPlausibleSeconds &&
					Outcome.TimeSeconds < Spec.MaxPlausibleSeconds);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSPaceKickTest,
	"WorldSports.Pace.KickOnlyPaysInsideItsWindow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSPaceKickTest::RunTest(const FString&)
{
	// The kick is a decision with a cost, not a free button. Spending it
	// with most of the race left must be worse than saving it.
	const FWSPaceEventSpec& Spec = WSPaceEvents::Find(TEXT("middle-800m"));
	const FWSSprintAttributes Attributes = UniformAttributes(60.0f);

	for (const uint32 Seed : {3u, 44u, 555u})
	{
		const TArray<FWSPaceInputEvent> Perfect =
			FWSMiddleDistanceSimulation::GeneratePerfectTrace(Attributes, Seed, Spec);
		const FWSPaceOutcome Timed =
			FWSMiddleDistanceSimulation::RunTrace(Attributes, Seed, Perfect, Spec);

		// The same race, kicking almost from the gun.
		TArray<FWSPaceInputEvent> Early;
		Early.Add(Perfect[0]);
		FWSPaceInputEvent Kick;
		Kick.TimeSeconds = 4.0;
		Kick.Type = EWSPaceInputType::Kick;
		Early.Add(Kick);
		const FWSPaceOutcome TooSoon =
			FWSMiddleDistanceSimulation::RunTrace(Attributes, Seed, Early, Spec);

		TestTrue(TEXT("both finished"), Timed.bFinished && TooSoon.bFinished);
		TestTrue(FString::Printf(
				TEXT("seed %u: a timed kick (%.2f) must beat an early one (%.2f)"),
				Seed, Timed.TimeSeconds, TooSoon.TimeSeconds),
			Timed.TimeSeconds < TooSoon.TimeSeconds);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSPaceStrategySweepTest,
	"WorldSports.Pace.NoPacingStrategyBeatsTheCeiling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSPaceStrategySweepTest::RunTest(const FString&)
{
	// Calibrating against ONE reference strategy is not enough, and the live
	// backend proved it: a two-phase pace — go out a little hot, correct
	// mid-race, kick — beat the best even-effort race by half a second, and
	// the server rejected the run as beyond the athlete's ceiling.
	//
	// So this sweeps a FAMILY of pacing strategies and asserts the claim
	// directly: whatever the player does, the result stays inside the limit
	// the server enforces. A player will always find a line the designer did
	// not model; the guarantee has to hold for lines nobody modelled.
	for (const FWSPaceEventSpec& Spec : WSPaceEvents::All())
	{
		for (const float Level : {25.0f, 55.0f, 85.0f})
		{
			const FWSSprintAttributes Attributes = UniformAttributes(Level);
			const double Ceiling = ServerCeiling(Spec, Level);
			double BestFound = TNumericLimits<double>::Max();
			FString BestDescription;

			for (const uint32 Seed : {3u, 17u, 29u})
			{
				// The even-effort optimum, as the centre of the search.
				const TArray<FWSPaceInputEvent> Reference =
					FWSMiddleDistanceSimulation::GeneratePerfectTrace(Attributes, Seed, Spec);
				const double Centre = Reference.Num() > 0 ? Reference[0].Value : 0.6;
				const double KickTime = Reference.Num() > 1 ? Reference.Last().TimeSeconds : -1.0;

				for (int32 OpenStep = -2; OpenStep <= 2; ++OpenStep)
				{
					for (int32 CorrectStep = -2; CorrectStep <= 2; ++CorrectStep)
					{
						TArray<FWSPaceInputEvent> Trace;
						FWSPaceInputEvent Opening;
						Opening.TimeSeconds = 0.0;
						Opening.Type = EWSPaceInputType::SetEffort;
						Opening.Value = Centre + 0.035 * OpenStep;
						Trace.Add(Opening);

						FWSPaceInputEvent Correction;
						// Correct at a third of the way in, which is where a
						// runner who went out wrong actually feels it.
						Correction.TimeSeconds = 0.33 * Ceiling;
						Correction.Type = EWSPaceInputType::SetEffort;
						Correction.Value = Centre + 0.035 * CorrectStep;
						Trace.Add(Correction);

						if (KickTime >= 0.0)
						{
							FWSPaceInputEvent Kick;
							Kick.TimeSeconds = KickTime;
							Kick.Type = EWSPaceInputType::Kick;
							Trace.Add(Kick);
						}

						const FWSPaceOutcome Outcome = FWSMiddleDistanceSimulation::RunTrace(
							Attributes, Seed, Trace, Spec);
						if (!Outcome.bFinished)
						{
							continue;
						}
						if (Outcome.TimeSeconds < BestFound)
						{
							BestFound = Outcome.TimeSeconds;
							BestDescription = FString::Printf(
								TEXT("seed %u open %.3f correct %.3f"),
								Seed, Opening.Value, Correction.Value);
						}
					}
				}
			}

			AddInfo(FString::Printf(
				TEXT("SWEEP %-13s attrs %3.0f  best of 75 strategies %7.3f")
				TEXT("  ceiling %7.3f  margin %+.3f  (%s)"),
				*Spec.Code, Level, BestFound, Ceiling, BestFound - Ceiling,
				*BestDescription));

			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: best strategy %.3f (%s) must not beat ceiling %.3f"),
					*Spec.Code, Level, BestFound, *BestDescription, Ceiling),
				BestFound >= Ceiling - 0.009);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
