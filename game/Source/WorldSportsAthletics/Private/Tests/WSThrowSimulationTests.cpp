#include "Misc/AutomationTest.h"
#include "Tests/WSTestAthlete.h"
#include "Simulation/WSThrowSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
/** The server's ceiling formula. A distance event's ceiling is a MAXIMUM. */
double ServerCeiling(const FWSThrowEventSpec& Spec, double MeanAttr)
{
	return Spec.CeilingAtZero + (Spec.CeilingAtHundred - Spec.CeilingAtZero)
		* FMath::Clamp(MeanAttr, 0.0, 100.0) / 100.0;
}

TArray<FWSThrowInputEvent> ReleaseAt(double Seconds)
{
	TArray<FWSThrowInputEvent> Trace;
	FWSThrowInputEvent Event;
	Event.TimeSeconds = Seconds;
	Event.Type = EWSThrowInputType::Release;
	Trace.Add(Event);
	return Trace;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSThrowDeterminismTest,
	"WorldSports.Throw.DeterministicReplay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSThrowDeterminismTest::RunTest(const FString&)
{
	const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(60.0f);
	const FWSThrowEventSpec& Spec = WSThrowEvents::Find(TEXT("throw-shot"));
	const TArray<FWSThrowInputEvent> Trace = ReleaseAt(1.6);

	const FWSThrowOutcome First =
		FWSThrowSimulation::RunTrace(Attributes, 42u, Trace, Spec);
	const FWSThrowOutcome Second =
		FWSThrowSimulation::RunTrace(Attributes, 42u, Trace, Spec);

	TestTrue(TEXT("finished"), First.bFinished && Second.bFinished);
	TestEqual(TEXT("identical mark"), First.DistanceMetres, Second.DistanceMetres);
	TestEqual(TEXT("identical release speed"), First.ReleaseSpeed, Second.ReleaseSpeed);

	// The peak drifts with the seed, so the same release time is a
	// different throw in a different attempt — otherwise the player could
	// memorise one number and stop watching.
	const FWSThrowOutcome Other =
		FWSThrowSimulation::RunTrace(Attributes, 43u, Trace, Spec);
	TestNotEqual(TEXT("a different attempt differs"),
		First.DistanceMetres, Other.DistanceMetres);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSThrowCeilingTest,
	"WorldSports.Throw.PerfectThrowRespectsServerCeiling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSThrowCeilingTest::RunTest(const FString&)
{
	// The integration guarantee for a distance event: the danger is
	// throwing too FAR. A mark above the ceiling is one the server refuses,
	// and the player is told their honest throw does not count.
	for (const FWSThrowEventSpec& Spec : WSThrowEvents::All())
	{
		for (const float Level : {0.0f, 25.0f, 40.0f, 55.0f, 70.0f, 85.0f, 100.0f})
		{
			const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(Level);
			const double Ceiling = ServerCeiling(Spec, Level);

			double BestMark = 0.0;
			uint32 BestSeed = 0;
			double ShortestMark = TNumericLimits<double>::Max();
			for (uint32 Seed = 1; Seed <= 32; ++Seed)
			{
				const TArray<FWSThrowInputEvent> Trace =
					FWSThrowSimulation::GenerateAITrace(Attributes, Seed, Seed, 1.0, Spec);
				const FWSThrowOutcome Outcome =
					FWSThrowSimulation::RunTrace(Attributes, Seed, Trace, Spec);
				TestTrue(FString::Printf(TEXT("%s attrs %.0f seed %u finished"),
						*Spec.Code, Level, Seed),
					Outcome.bFinished);
				TestFalse(FString::Printf(
						TEXT("%s attrs %.0f seed %u: a perfect release is not a foul"),
						*Spec.Code, Level, Seed),
					Outcome.bFoul);
				// Every mark the client reports must be one the server can
				// accept: at or under the ceiling, inside the plausible band.
				TestTrue(FString::Printf(
						TEXT("%s attrs %.0f seed %u: %.2f m is a mark the server accepts"),
						*Spec.Code, Level, Seed, Outcome.DistanceMetres),
					Outcome.DistanceMetres >= Spec.MinPlausibleMetres &&
						Outcome.DistanceMetres <= Spec.MaxPlausibleMetres);
				if (Outcome.DistanceMetres > BestMark)
				{
					BestMark = Outcome.DistanceMetres;
					BestSeed = Seed;
				}
				ShortestMark = FMath::Min(ShortestMark, Outcome.DistanceMetres);
			}

			AddInfo(FString::Printf(
				TEXT("THROW %-11s attrs %3.0f  best %6.2f m (seed %2u)")
				TEXT("  ceiling %6.2f m  margin %+.2f  shortest %6.2f"),
				*Spec.Code, Level, BestMark, BestSeed, Ceiling,
				Ceiling - BestMark, ShortestMark));

			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f seed %u: %.2f m must NOT exceed ceiling %.2f m"),
					*Spec.Code, Level, BestSeed, BestMark, Ceiling),
				BestMark <= Ceiling + 0.009);
			// And a ceiling nobody can approach makes attributes pointless.
			//
			// PROPORTIONAL, not a fixed number of metres: these events span
			// a shot put's 23m to a javelin's 97m, and 1.6m is seven per
			// cent of one and under two of the other. A fixed tolerance
			// tests the shot strictly and the javelin barely at all.
			const double NearCeiling = FMath::Max(0.50, Ceiling * 0.08);
			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: best %.2f m should approach the ceiling %.2f m"),
					*Spec.Code, Level, BestMark, Ceiling),
				BestMark >= Ceiling - NearCeiling);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSThrowTimingDecidesTest,
	"WorldSports.Throw.ReleaseTimingDecidesTheThrow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSThrowTimingDecidesTest::RunTest(const FString&)
{
	// Power peaks and then falls away, so the release is a judgement rather
	// than a button to hold. Both sides of the peak must cost real distance,
	// or the event is "press whenever".
	const FWSThrowEventSpec& Spec = WSThrowEvents::Find(TEXT("throw-shot"));
	const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(70.0f);

	for (const uint32 Seed : {5u, 66u, 707u})
	{
		const TArray<FWSThrowInputEvent> Peak =
			FWSThrowSimulation::GenerateAITrace(Attributes, Seed, Seed, 1.0, Spec);
		if (!TestTrue(TEXT("the thrower found a peak"), Peak.Num() > 0))
		{
			return false;
		}
		const double PeakTime = Peak[0].TimeSeconds;

		const FWSThrowOutcome OnPeak =
			FWSThrowSimulation::RunTrace(Attributes, Seed, Peak, Spec);
		const FWSThrowOutcome TooEarly = FWSThrowSimulation::RunTrace(
			Attributes, Seed, ReleaseAt(FMath::Max(0.05, PeakTime - 0.55)), Spec);
		const FWSThrowOutcome TooLate = FWSThrowSimulation::RunTrace(
			Attributes, Seed, ReleaseAt(PeakTime + 0.45), Spec);

		TestFalse(TEXT("a judged release is not a foul"), OnPeak.bFoul);
		TestTrue(FString::Printf(
				TEXT("seed %u: early (%.2f m) must cost against the peak (%.2f m)"),
				Seed, TooEarly.DistanceMetres, OnPeak.DistanceMetres),
			TooEarly.DistanceMetres < OnPeak.DistanceMetres - 0.30);
		TestTrue(FString::Printf(
				TEXT("seed %u: late (%.2f m) must cost against the peak (%.2f m)"),
				Seed, TooLate.DistanceMetres, OnPeak.DistanceMetres),
			TooLate.DistanceMetres < OnPeak.DistanceMetres - 0.30);

		AddInfo(FString::Printf(
			TEXT("RELEASE seed %4u  early %6.2f m   peak %6.2f m   late %6.2f m"),
			Seed, TooEarly.DistanceMetres, OnPeak.DistanceMetres, TooLate.DistanceMetres));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSThrowCarriedOutTest,
	"WorldSports.Throw.NeverReleasingIsNoMark",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSThrowCarriedOutTest::RunTest(const FString&)
{
	// Holding on carries the throw out of the circle: a foul and NO MARK,
	// not a throw of zero metres. Same rule as a jumper past the board.
	const FWSThrowEventSpec& Spec = WSThrowEvents::Find(TEXT("throw-shot"));
	const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(70.0f);

	const FWSThrowOutcome NeverReleased = FWSThrowSimulation::RunTrace(
		Attributes, 7u, TArray<FWSThrowInputEvent>(), Spec);
	TestTrue(TEXT("holding on is a foul"), NeverReleased.bFoul);
	TestTrue(TEXT("recorded as carried out of the circle"), NeverReleased.bCarriedOut);
	TestEqual(TEXT("a foul has no mark"), NeverReleased.DistanceMetres, 0.0);

	// Releasing after the wind-up has run out is the same thing.
	const FWSThrowOutcome TooLate = FWSThrowSimulation::RunTrace(
		Attributes, 7u, ReleaseAt(Spec.WindUpSeconds + 0.5), Spec);
	TestTrue(TEXT("releasing after the circle is a foul"), TooLate.bFoul);
	TestEqual(TEXT("and has no mark"), TooLate.DistanceMetres, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSThrowSkillOverAttributesTest,
	"WorldSports.Throw.ExecutionDecidesTheThrow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSThrowSkillOverAttributesTest::RunTest(const FString&)
{
	// Attributes raise the ceiling; execution decides the throw. A modest
	// thrower who finds the peak must out-throw a stronger one who does not.
	//
	// The gap here is 15 points, and that is deliberate rather than
	// convenient: measured over 24 throws, wild timing still returns about
	// three quarters of an athlete's ceiling, so execution closes an
	// attribute gap of roughly 15-20 points and no more. A 30-point gap
	// (55 against 85) goes the other way, and asserting otherwise would be
	// claiming an anti-pay-to-win guarantee this model does not provide.
	// The honest guarantee is that training buys a ceiling, not a result.
	const FWSThrowEventSpec& Spec = WSThrowEvents::Find(TEXT("throw-shot"));
	const FWSSprintAttributes Modest = WSTestAthlete::Uniform(55.0f);
	const FWSSprintAttributes Strong = WSTestAthlete::Uniform(70.0f);

	// Compared over a SERIES rather than a single throw. One attempt is not
	// the claim: a wild thrower who happens to let go near the peak will
	// out-throw a judged one that day, and pretending otherwise would be a
	// worse model of the sport than the one being tested. What must hold is
	// that judgement wins when it is not a coin toss.
	double JudgedTotal = 0.0;
	double SloppyTotal = 0.0;
	int32 Attempts = 0;
	for (uint32 Seed = 1; Seed <= 24; ++Seed)
	{
		const FWSThrowOutcome ModestJudged = FWSThrowSimulation::RunTrace(
			Modest, Seed,
			FWSThrowSimulation::GenerateAITrace(Modest, Seed, Seed, 1.0, Spec), Spec);
		const FWSThrowOutcome StrongSloppy = FWSThrowSimulation::RunTrace(
			Strong, Seed,
			FWSThrowSimulation::GenerateAITrace(Strong, Seed, Seed, 0.1, Spec), Spec);

		TestFalse(TEXT("the judged throw stands"), ModestJudged.bFoul);
		JudgedTotal += ModestJudged.DistanceMetres;
		SloppyTotal += StrongSloppy.DistanceMetres;
		++Attempts;
	}
	const double JudgedMean = JudgedTotal / Attempts;
	const double SloppyMean = SloppyTotal / Attempts;
	AddInfo(FString::Printf(
		TEXT("EXECUTION judged (attrs 55) %.2f m   sloppy (attrs 70) %.2f m over %d throws"),
		JudgedMean, SloppyMean, Attempts));
	TestTrue(FString::Printf(
			TEXT("a judged thrower (%.2f m) must out-throw a wild stronger one (%.2f m)"),
			JudgedMean, SloppyMean),
		JudgedMean > SloppyMean);

	// And the converse: with the release judged equally well, strength wins.
	for (const uint32 Seed : {4u, 55u, 616u})
	{
		const FWSThrowOutcome ModestRun = FWSThrowSimulation::RunTrace(
			Modest, Seed,
			FWSThrowSimulation::GenerateAITrace(Modest, Seed, Seed, 1.0, Spec), Spec);
		const FWSThrowOutcome StrongRun = FWSThrowSimulation::RunTrace(
			Strong, Seed,
			FWSThrowSimulation::GenerateAITrace(Strong, Seed, Seed, 1.0, Spec), Spec);
		TestTrue(TEXT("equal execution: attributes decide"),
			StrongRun.DistanceMetres > ModestRun.DistanceMetres);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
