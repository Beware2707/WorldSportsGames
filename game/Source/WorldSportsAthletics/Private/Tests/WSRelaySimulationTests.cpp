#include "Misc/AutomationTest.h"

#include "Simulation/WSRelaySimulation.h"
#include "Tests/WSTestAthlete.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
/**
 * The server's ceiling, mirrored exactly. If this drifts from
 * backend/app/services/career.py the client will show times the server
 * then refuses, which is the one failure mode the whole calibration
 * exists to prevent.
 */
double ServerCeiling(const FWSRelayEventSpec& Spec, double MeanAttr)
{
	return Spec.CeilingAtZero +
		(Spec.CeilingAtHundred - Spec.CeilingAtZero) * (MeanAttr / 100.0);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRelayCeilingTest,
	"WorldSports.Relay.PerfectRelayRespectsServerCeiling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRelayCeilingTest::RunTest(const FString&)
{
	// The invariant the whole game rests on, in the direction a timed event
	// runs: perfect play must approach the server's ceiling from ABOVE and
	// never dip below it, or the client shows a team a time the server then
	// refuses.
	for (const FWSRelayEventSpec& Spec : WSRelayEvents::All())
	{
		for (const float Level : {0.0f, 25.0f, 40.0f, 55.0f, 70.0f, 85.0f, 100.0f})
		{
			const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(Level);
			const double Ceiling = ServerCeiling(Spec, Level);

			double Fastest = TNumericLimits<double>::Max();
			double Slowest = 0.0;
			uint32 FastestSeed = 0;
			int32 Disqualified = 0;
			for (uint32 Seed = 1; Seed <= 24; ++Seed)
			{
				const TArray<FWSRelayInputEvent> Trace =
					FWSRelaySimulation::GenerateAITrace(Attributes, Seed, Seed,
						/*ReactionMeanMs=*/135.0, /*Spread=*/0.0, /*Consistency=*/1.0, Spec);
				const FWSRelayOutcome Outcome =
					FWSRelaySimulation::RunTrace(Attributes, Seed, Trace, Spec);
				if (Outcome.bBadExchange || Outcome.bFalseStart)
				{
					++Disqualified;
					continue;
				}
				if (Outcome.TimeSeconds < Fastest)
				{
					Fastest = Outcome.TimeSeconds;
					FastestSeed = Seed;
				}
				Slowest = FMath::Max(Slowest, Outcome.TimeSeconds);
			}

			// A perfectly executed relay never drops the baton. If it can,
			// the zone is too tight to be played rather than the team being
			// bad at it.
			TestEqual(FString::Printf(
					TEXT("%s attrs %.0f: a perfect team never misses the zone"),
					*Spec.Code, Level),
				Disqualified, 0);

			// Fastest == slowest across every seed, and that is CORRECT: a
			// relay models no per-race conditions. The sprints and the
			// jumps vary with wind, which the sport records for them; World
			// Athletics records none for a relay, so there is nothing left
			// for a seed to change once the execution is perfect. The sweep
			// is therefore a determinism check as much as a calibration
			// one, and a difference here would mean an unreported condition
			// had crept into the model.
			TestEqual(FString::Printf(
					TEXT("%s attrs %.0f: the same team runs the same time every "
						 "seed, because a relay has no conditions to vary"),
					*Spec.Code, Level),
				Slowest, Fastest, /*Tolerance=*/1.0e-9);

			AddInfo(FString::Printf(
				TEXT("RELAY %-12s attrs %3.0f  fastest %7.3fs (seed %2u)")
				TEXT("  ceiling %7.3fs  margin %+.3f  slowest %7.3f"),
				*Spec.Code, Level, Fastest, FastestSeed, Ceiling,
				Fastest - Ceiling, Slowest));

			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f seed %u: %.3fs must NOT beat ceiling %.3fs"),
					*Spec.Code, Level, FastestSeed, Fastest, Ceiling),
				Fastest >= Ceiling - 0.009);
			// And it has to be worth running: a ceiling nobody can approach
			// makes every attribute point meaningless. Proportional, because
			// a 4x400 is four times the clock of a 4x100.
			const double Reach = FMath::Max(0.35, Ceiling * 0.08);
			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: %.3fs should come within %.2fs of %.3fs"),
					*Spec.Code, Level, Fastest, Reach, Ceiling),
				Fastest <= Ceiling + Reach);
			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: %.3fs is inside the server's plausible band"),
					*Spec.Code, Level, Slowest),
				Fastest >= Spec.MinPlausibleSeconds &&
					Slowest <= Spec.MaxPlausibleSeconds);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRelayExchangeTest,
	"WorldSports.Relay.TheExchangeIsTheEvent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRelayExchangeTest::RunTest(const FString&)
{
	// A relay is decided at the handovers, and this is the check that the
	// model agrees: the same four runners, handing over on the mark or
	// scraping through the zone, must not run the same time.
	for (const FWSRelayEventSpec& Spec : WSRelayEvents::All())
	{
		const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(55.0f);
		for (uint32 Seed = 1; Seed <= 6; ++Seed)
		{
			const TArray<FWSRelayInputEvent> Sharp =
				FWSRelaySimulation::GenerateAITrace(Attributes, Seed, Seed,
					135.0, 0.0, /*Consistency=*/1.0, Spec);
			const FWSRelayOutcome OnTheMark =
				FWSRelaySimulation::RunTrace(Attributes, Seed, Sharp, Spec);

			// The same team handing over at the very START of the zone —
			// legal, and about as badly judged as legal gets. Built by
			// running the same simulation and calling the pass the instant
			// the zone opens, so nothing but the handover differs.
			TArray<FWSRelayInputEvent> Early;
			{
				FWSRelaySimulation Shadow(Attributes, Seed, Spec);
				double NextTapAt = -1.0;
				for (const FWSRelayInputEvent& Event : Sharp)
				{
					if (Event.Type == EWSRelayInputType::Release)
					{
						Early.Add(Event);
						Shadow.AddInput(Event);
						NextTapAt = Event.TimeSeconds + 0.10;
						break;
					}
				}
				int32 PassedLeg = 0;
				while (Shadow.Step())
				{
					const FWSRelayState& Live = Shadow.GetState();
					if (Live.RaceTime < 0.0)
					{
						continue;
					}
					while (NextTapAt > 0.0 && NextTapAt <= Live.RaceTime)
					{
						FWSRelayInputEvent Tap;
						Tap.TimeSeconds = NextTapAt;
						Tap.Type = EWSRelayInputType::Tap;
						Early.Add(Tap);
						Shadow.AddInput(Tap);
						const double LegStart = Spec.LegMetres * (Live.Leg - 1);
						NextTapAt += 1.0 / FMath::Max(
							Shadow.TargetCadenceAt(Live.Distance - LegStart), 0.5);
					}
					if (Live.bInTakeoverZone && Live.Leg != PassedLeg)
					{
						FWSRelayInputEvent Pass;
						Pass.TimeSeconds = Live.RaceTime;
						Pass.Type = EWSRelayInputType::Pass;
						Early.Add(Pass);
						Shadow.AddInput(Pass);
						PassedLeg = Live.Leg;
					}
				}
			}
			const FWSRelayOutcome AtTheLine =
				FWSRelaySimulation::RunTrace(Attributes, Seed, Early, Spec);

			if (OnTheMark.bBadExchange || AtTheLine.bBadExchange)
			{
				continue; // a DQ says nothing about how fast the pass was
			}
			TestTrue(FString::Printf(
					TEXT("%s seed %u: handing over on the mark (%.3fs) must beat "
						 "handing over as the zone opens (%.3fs)"),
					*Spec.Code, Seed, OnTheMark.TimeSeconds, AtTheLine.TimeSeconds),
				OnTheMark.TimeSeconds < AtTheLine.TimeSeconds - 0.05);
			TestEqual(FString::Printf(TEXT("%s: one exchange per handover"), *Spec.Code),
				OnTheMark.ExchangeQuality.Num(), Spec.LegCount - 1);
			TestEqual(FString::Printf(TEXT("%s: one split per leg"), *Spec.Code),
				OnTheMark.Splits.Num(), Spec.LegCount);
			if (Seed == 1)
			{
				AddInfo(FString::Printf(
					TEXT("EXCHANGE %-12s on the mark %7.3fs   zone opening %7.3fs")
					TEXT("   cost %.3fs"),
					*Spec.Code, OnTheMark.TimeSeconds, AtTheLine.TimeSeconds,
					AtTheLine.TimeSeconds - OnTheMark.TimeSeconds));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRelayZoneTest,
	"WorldSports.Relay.OutsideTheZoneIsADisqualification",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRelayZoneTest::RunTest(const FString&)
{
	// The rule that makes a relay a relay. A baton that changes hands
	// outside the zone is not a slow team; it is a team with no time.
	for (const FWSRelayEventSpec& Spec : WSRelayEvents::All())
	{
		const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(55.0f);

		// Never handing over at all: the runner leaves the zone still
		// holding the baton.
		TArray<FWSRelayInputEvent> NoPass;
		FWSRelayInputEvent Release;
		Release.Type = EWSRelayInputType::Release;
		Release.TimeSeconds = 0.150;
		NoPass.Add(Release);
		for (int32 Index = 0; Index < 400; ++Index)
		{
			FWSRelayInputEvent Tap;
			Tap.Type = EWSRelayInputType::Tap;
			Tap.TimeSeconds = 0.25 + Index / 4.6;
			NoPass.Add(Tap);
		}
		const FWSRelayOutcome Dropped =
			FWSRelaySimulation::RunTrace(Attributes, 7u, NoPass, Spec);
		TestTrue(FString::Printf(TEXT("%s: running through the zone is a DQ"), *Spec.Code),
			Dropped.bBadExchange);
		TestEqual(FString::Printf(TEXT("%s: and it was the FIRST handover"), *Spec.Code),
			Dropped.BadExchangeLeg, 1);
		TestEqual(FString::Printf(TEXT("%s: a disqualified team has no time"), *Spec.Code),
			Dropped.TimeSeconds, 0.0);

		// Handing over far too early — before the zone opens.
		TArray<FWSRelayInputEvent> TooEarly = NoPass;
		FWSRelayInputEvent Pass;
		Pass.Type = EWSRelayInputType::Pass;
		Pass.TimeSeconds = 1.0; // metres into the first leg, nowhere near the line
		TooEarly.Insert(Pass, 1);
		const FWSRelayOutcome Early =
			FWSRelaySimulation::RunTrace(Attributes, 7u, TooEarly, Spec);
		TestTrue(FString::Printf(TEXT("%s: passing before the zone is a DQ"), *Spec.Code),
			Early.bBadExchange);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRelaySplitContractTest,
	"WorldSports.Relay.LegsPlusReactionAreTheTime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRelaySplitContractTest::RunTest(const FString&)
{
	// The SERVER's rule, mirrored: sum(splits) + reaction == the recorded
	// time. The legs measure RUNNING time, so the first one starts when the
	// athlete leaves the blocks.
	//
	// Measuring leg 1 from the gun counted the reaction twice and the
	// server refused the result — "splits (74.530s) plus reaction do not
	// sum to the recorded time (74.530s)", two identical numbers. It only
	// escaped notice because the check's tolerance is 1% of the time and
	// the one submission ever tested had a 175ms reaction.
	for (const FWSRelayEventSpec& Spec : WSRelayEvents::All())
	{
		for (const float Level : {25.0f, 55.0f, 85.0f})
		{
			const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(Level);
			// Across the whole legal reaction band, including the automatic
			// release the simulation itself applies when the player misses
			// the start — which is the case that was failing.
			for (const double ReactionMs : {110.0, 175.0, 400.0, 1500.0, 1900.0})
			{
				const TArray<FWSRelayInputEvent> Trace =
					FWSRelaySimulation::GenerateAITrace(Attributes, 7u, 7u,
						ReactionMs, 0.0, /*Consistency=*/1.0, Spec);
				const FWSRelayOutcome Outcome =
					FWSRelaySimulation::RunTrace(Attributes, 7u, Trace, Spec);
				if (Outcome.bBadExchange || Outcome.bFalseStart || !Outcome.bFinished)
				{
					continue;
				}
				double Total = 0.0;
				for (const double Leg : Outcome.Splits)
				{
					Total += Leg;
				}
				const double WithReaction = Total + Outcome.ReactionMs / 1000.0;
				// The server's own tolerance, so this fails here before it
				// can fail there.
				const double Tolerance = FMath::Max(0.05, Outcome.TimeSeconds * 0.01);
				TestTrue(FString::Printf(
						TEXT("%s attrs %.0f reaction %.0fms: legs (%.3f) + reaction "
							 "(%.3f) = %.3f must equal the time %.3f within %.3f"),
						*Spec.Code, Level, ReactionMs, Total,
						Outcome.ReactionMs / 1000.0, WithReaction,
						Outcome.TimeSeconds, Tolerance),
					FMath::Abs(WithReaction - Outcome.TimeSeconds) <= Tolerance);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRelayDuplicatePassTest,
	"WorldSports.Relay.ADuplicatePassIsNotASecondHandover",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRelayDuplicatePassTest::RunTest(const FString&)
{
	// Two PASS presses landing in the same frame are drained by one Step().
	// The first hands over legally; the second used to be applied at the
	// SAME metre against the next leg's zone, a hundred metres away, and
	// disqualified the team. A two-thumb tap is not an illegal exchange.
	const FWSRelayEventSpec& Spec = WSRelayEvents::All()[0];
	const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(55.0f);

	for (uint32 Seed = 1; Seed <= 4; ++Seed)
	{
		const TArray<FWSRelayInputEvent> Clean =
			FWSRelaySimulation::GenerateAITrace(Attributes, Seed, Seed,
				175.0, 0.0, /*Consistency=*/1.0, Spec);
		const FWSRelayOutcome Expected =
			FWSRelaySimulation::RunTrace(Attributes, Seed, Clean, Spec);
		if (Expected.bBadExchange || !Expected.bFinished)
		{
			continue;
		}

		// The same trace with every pass pressed twice, a millisecond apart.
		TArray<FWSRelayInputEvent> Doubled;
		for (const FWSRelayInputEvent& Event : Clean)
		{
			Doubled.Add(Event);
			if (Event.Type == EWSRelayInputType::Pass)
			{
				FWSRelayInputEvent Again = Event;
				Again.TimeSeconds = Event.TimeSeconds + 0.001;
				Doubled.Add(Again);
			}
		}
		const FWSRelayOutcome Fumbled =
			FWSRelaySimulation::RunTrace(Attributes, Seed, Doubled, Spec);

		TestFalse(FString::Printf(
				TEXT("%s seed %u: a doubled press must not disqualify the team"),
				*Spec.Code, Seed),
			Fumbled.bBadExchange);
		TestEqual(FString::Printf(
				TEXT("%s seed %u: and must run exactly the same time"),
				*Spec.Code, Seed),
			Fumbled.TimeSeconds, Expected.TimeSeconds, /*Tolerance=*/1.0e-9);
		TestEqual(FString::Printf(TEXT("%s seed %u: with the same three handovers"),
				*Spec.Code, Seed),
			Fumbled.ExchangeQuality.Num(), Spec.LegCount - 1);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
