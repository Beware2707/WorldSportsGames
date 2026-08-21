#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Online/WSOnlineSubsystem.h"
#include "Progression/WSProgressionSubsystem.h"
#include "Simulation/WSJumpSimulation.h"
#include "Simulation/WSPaceSimulation.h"
#include "Simulation/WSThrowSimulation.h"
#include "Simulation/WSSprintEvents.h"
#include "Simulation/WSSprintSimulation.h"
#include "Sports/Results/WSEventResult.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The Phase 5 checkpoint against the REAL server:
 *
 *   Automation RunTests LiveBackend.LongerSprintsAccepted
 *
 * For every event in the client's table, run the actual simulation, submit
 * the actual outcome, and require the actual backend to accept it. Nothing
 * here is hand-written — the time, splits, reaction and wind all come out
 * of the simulation the player races.
 *
 * This is the check that the two event tables have not drifted apart. A
 * client ceiling below the server's, a split count off by one, a 400m the
 * server thinks is implausible: each of those produces a race a player runs
 * honestly and is then told does not count, and each is invisible to any
 * offline test, because offline the client only ever checks itself.
 */

namespace WSLiveEventTest
{
struct FState
{
	bool bDone = false;
	bool bOk = false;
	FString Error;
	/** One line per event: what was submitted and what came back. */
	TArray<FString> Report;
	int32 NextEvent = 0;
};

/** An honest, well-played run of one event by a default-attribute athlete. */
FWSEventResult SimulateRun(const FWSSprintEventSpec& Spec, uint32 Seed)
{
	// Default attributes: exactly what a fresh career athlete has on the
	// server, so the ceiling the validator applies is the one this run was
	// simulated against.
	const FWSSprintAttributes Attributes;
	const TArray<FWSSprintInputEvent> Trace = FWSSprintSimulation::GenerateAITrace(
		Attributes, Seed, Seed, 175.0, 20.0, 0.82, Spec);
	const FWSRaceOutcome Outcome =
		FWSSprintSimulation::RunTrace(Attributes, Seed, Trace, Spec);

	FWSEventResult Result;
	Result.EventCode = Spec.Code;
	Result.ValueNum = Outcome.TimeSeconds;
	Result.bHasReactionMs = true;
	Result.ReactionMs = Outcome.ReactionMs;
	Result.Splits = Outcome.Splits;
	Result.bHasWind = true;
	Result.Wind = Outcome.Wind;
	Result.RngSeed = FString::Printf(TEXT("%u"), Seed);
	Result.InputDigest = FWSSprintSimulation::DigestTrace(Trace);
	// A fresh ref per event per run: the server de-duplicates on this, and
	// reusing one would make the second event's answer the first's echo.
	Result.ClientRef = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	return Result;
}

/** An honest, well-paced run of one middle-distance event. */
FWSEventResult SimulatePacedRun(const FWSPaceEventSpec& Spec, uint32 Seed)
{
	const FWSSprintAttributes Attributes;
	const TArray<FWSPaceInputEvent> Trace =
		FWSMiddleDistanceSimulation::GenerateAITrace(Attributes, Seed, Seed, 0.85, Spec);
	const FWSPaceOutcome Outcome =
		FWSMiddleDistanceSimulation::RunTrace(Attributes, Seed, Trace, Spec);

	FWSEventResult Result;
	Result.EventCode = Spec.Code;
	Result.ValueNum = Outcome.TimeSeconds;
	// No reaction and no wind, deliberately. These events have no blocks,
	// and World Athletics records no wind beyond 200m — sending either
	// would be claiming a measurement that was never taken. The server's
	// row agrees (requires_reaction=False), so this must round-trip.
	Result.bHasReactionMs = false;
	Result.bHasWind = false;
	Result.Splits = Outcome.Splits;
	Result.RngSeed = FString::Printf(TEXT("%u"), Seed);
	Result.InputDigest = FWSMiddleDistanceSimulation::DigestTrace(Trace);
	Result.ClientRef = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	return Result;
}

/** An honest jump: the best legal mark of a simulated series. */
FWSEventResult SimulateJump(const FWSJumpEventSpec& Spec, uint32 Seed)
{
	const FWSSprintAttributes Attributes;
	const TArray<FWSJumpInputEvent> Trace =
		FWSJumpSimulation::GenerateAITrace(Attributes, Seed, Seed, 0.95, Spec);
	const FWSJumpOutcome Outcome =
		FWSJumpSimulation::RunTrace(Attributes, Seed, Trace, Spec);

	FWSEventResult Result;
	Result.EventCode = Spec.Code;
	// METRES. The server's row says value_kind=distance and that higher is
	// better; nothing here needs to know, because the number is the mark.
	Result.ValueNum = Outcome.DistanceMetres;
	Result.bHasReactionMs = false;   // no blocks on a runway
	Result.bHasWind = true;          // a jump IS a wind-affected mark
	Result.Wind = Outcome.Wind;
	Result.RngSeed = FString::Printf(TEXT("%u"), Seed);
	Result.InputDigest = FWSJumpSimulation::DigestTrace(Trace);
	Result.ClientRef = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	return Result;
}

/** An honest throw, released at the peak of the wind-up. */
FWSEventResult SimulateThrow(const FWSThrowEventSpec& Spec, uint32 Seed)
{
	const FWSSprintAttributes Attributes;
	const TArray<FWSThrowInputEvent> Trace =
		FWSThrowSimulation::GenerateAITrace(Attributes, Seed, Seed, 0.95, Spec);
	const FWSThrowOutcome Outcome =
		FWSThrowSimulation::RunTrace(Attributes, Seed, Trace, Spec);

	FWSEventResult Result;
	Result.EventCode = Spec.Code;
	Result.ValueNum = Outcome.DistanceMetres;
	Result.bHasReactionMs = false;
	// No wind for throws: the sport does not record it, so claiming one
	// would be inventing a measurement.
	Result.bHasWind = false;
	Result.RngSeed = FString::Printf(TEXT("%u"), Seed);
	Result.InputDigest = FWSThrowSimulation::DigestTrace(Trace);
	Result.ClientRef = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	return Result;
}
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FWSWaitForEventSubmits,
	TSharedPtr<WSLiveEventTest::FState>, State, UGameInstance*, GameInstance,
	FAutomationTestBase*, Test);

bool FWSWaitForEventSubmits::Update()
{
	constexpr double TimeoutSeconds = 90.0;
	if (!State->bDone && GetCurrentRunTime() < TimeoutSeconds)
	{
		return false;
	}
	if (!State->bDone)
	{
		Test->AddError(TEXT("Timed out — is uvicorn running on the configured BaseUrl?"));
	}
	else if (!State->bOk)
	{
		Test->AddError(State->Error);
	}
	for (const FString& Line : State->Report)
	{
		Test->AddInfo(Line);
	}
	GameInstance->Shutdown();
	GameInstance->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSLiveLongerSprintsTest,
	"LiveBackend.LongerSprintsAccepted",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSLiveLongerSprintsTest::RunTest(const FString&)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->AddToRoot();
	GameInstance->InitializeStandalone();

	UWSOnlineSubsystem* Online = GameInstance->GetSubsystem<UWSOnlineSubsystem>();
	UWSProgressionSubsystem* Progression =
		GameInstance->GetSubsystem<UWSProgressionSubsystem>();
	if (!Online || !Progression)
	{
		GameInstance->RemoveFromRoot();
		AddError(TEXT("Subsystems missing"));
		return false;
	}

	TSharedPtr<WSLiveEventTest::FState> State = MakeShared<WSLiveEventTest::FState>();

	// Declared first so the submit chain can recurse through it.
	TSharedPtr<TFunction<void()>> SubmitNext = MakeShared<TFunction<void()>>();
	*SubmitNext = [State, Online, SubmitNext]()
	{
		const TArray<FWSSprintEventSpec>& Table = WSSprintEvents::All();
		if (State->NextEvent >= Table.Num())
		{
			State->bOk = true;
			State->bDone = true;
			return;
		}
		const FWSSprintEventSpec& Spec = Table[State->NextEvent];
		const FWSEventResult Run = WSLiveEventTest::SimulateRun(Spec, 20260817u);

		Online->SubmitResult(Run,
			[State, Spec, Run, SubmitNext](EWSSubmitOutcome Outcome,
				const FWSResultResponse& Response, const FString& Error)
			{
				if (Outcome != EWSSubmitOutcome::Accepted)
				{
					// Report the server's own words. "Rejected" with the
					// reason is the finding; a generic failure would hide
					// which of the two tables is wrong.
					State->Error = FString::Printf(
						TEXT("%s: %.3fs with %d splits was not accepted — %s%s"),
						*Spec.Code, Run.ValueNum, Run.Splits.Num(),
						*Response.rejection_reason, *Error);
					State->bDone = true;
					return;
				}
				State->Report.Add(FString::Printf(
					TEXT("LIVE %-12s %.3fs (reaction %.0fms, %d splits, wind %+.1f) -> %s%s"),
					*Spec.Code, Run.ValueNum, Run.ReactionMs, Run.Splits.Num(), Run.Wind,
					*Response.value_text,
					Response.is_personal_best ? TEXT(" [PB]") : TEXT("")));
				++State->NextEvent;
				(*SubmitNext)();
			});
	};

	// Sign in, guarantee a career athlete, then submit one run per event.
	Online->Login(TEXT("unreal-smoke@example.com"), TEXT("unreal-smoke-pass-1"),
		[State, Progression, SubmitNext](bool bOk, const FWSUserDto&, const FString& Error)
		{
			if (!bOk)
			{
				State->Error = FString::Printf(TEXT("login failed: %s"), *Error);
				State->bDone = true;
				return;
			}
			Progression->RefreshCareerAthlete(
				[State, Progression, SubmitNext](bool bRefreshed, const FString& RefreshError)
				{
					if (bRefreshed && Progression->HasCareerAthlete())
					{
						(*SubmitNext)();
						return;
					}
					// No athlete yet on a fresh database: create one rather
					// than failing a test about event submission.
					Progression->CreateCareerAthlete(TEXT("Unreal Smoke"), TEXT("F"),
						[State, SubmitNext](bool bCreated, const FString& CreateError)
						{
							if (!bCreated)
							{
								State->Error = FString::Printf(
									TEXT("no career athlete: %s"), *CreateError);
								State->bDone = true;
								return;
							}
							(*SubmitNext)();
						});
				});
		});

	ADD_LATENT_AUTOMATION_COMMAND(FWSWaitForEventSubmits(State, GameInstance, this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSLiveMiddleDistanceTest,
	"LiveBackend.MiddleDistanceAccepted",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSLiveMiddleDistanceTest::RunTest(const FString&)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->AddToRoot();
	GameInstance->InitializeStandalone();

	UWSOnlineSubsystem* Online = GameInstance->GetSubsystem<UWSOnlineSubsystem>();
	UWSProgressionSubsystem* Progression =
		GameInstance->GetSubsystem<UWSProgressionSubsystem>();
	if (!Online || !Progression)
	{
		GameInstance->RemoveFromRoot();
		AddError(TEXT("Subsystems missing"));
		return false;
	}

	TSharedPtr<WSLiveEventTest::FState> State = MakeShared<WSLiveEventTest::FState>();

	TSharedPtr<TFunction<void()>> SubmitNext = MakeShared<TFunction<void()>>();
	*SubmitNext = [State, Online, SubmitNext]()
	{
		const TArray<FWSPaceEventSpec>& Table = WSPaceEvents::All();
		if (State->NextEvent >= Table.Num())
		{
			State->bOk = true;
			State->bDone = true;
			return;
		}
		const FWSPaceEventSpec& Spec = Table[State->NextEvent];
		const FWSEventResult Run = WSLiveEventTest::SimulatePacedRun(Spec, 20260817u);

		Online->SubmitResult(Run,
			[State, Spec, Run, SubmitNext](EWSSubmitOutcome Outcome,
				const FWSResultResponse& Response, const FString& Error)
			{
				if (Outcome != EWSSubmitOutcome::Accepted)
				{
					State->Error = FString::Printf(
						TEXT("%s: %.3fs with %d splits was not accepted — %s%s"),
						*Spec.Code, Run.ValueNum, Run.Splits.Num(),
						*Response.rejection_reason, *Error);
					State->bDone = true;
					return;
				}
				State->Report.Add(FString::Printf(
					TEXT("LIVE %-13s %.3fs (%d splits, no reaction, no wind) -> %s%s"),
					*Spec.Code, Run.ValueNum, Run.Splits.Num(), *Response.value_text,
					Response.is_personal_best ? TEXT(" [PB]") : TEXT("")));
				++State->NextEvent;
				(*SubmitNext)();
			});
	};

	Online->Login(TEXT("unreal-smoke@example.com"), TEXT("unreal-smoke-pass-1"),
		[State, Progression, SubmitNext](bool bOk, const FWSUserDto&, const FString& Error)
		{
			if (!bOk)
			{
				State->Error = FString::Printf(TEXT("login failed: %s"), *Error);
				State->bDone = true;
				return;
			}
			Progression->RefreshCareerAthlete(
				[State, Progression, SubmitNext](bool bRefreshed, const FString&)
				{
					if (bRefreshed && Progression->HasCareerAthlete())
					{
						(*SubmitNext)();
						return;
					}
					Progression->CreateCareerAthlete(TEXT("Unreal Smoke"), TEXT("F"),
						[State, SubmitNext](bool bCreated, const FString& CreateError)
						{
							if (!bCreated)
							{
								State->Error = FString::Printf(
									TEXT("no career athlete: %s"), *CreateError);
								State->bDone = true;
								return;
							}
							(*SubmitNext)();
						});
				});
		});

	ADD_LATENT_AUTOMATION_COMMAND(FWSWaitForEventSubmits(State, GameInstance, this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSLiveFieldEventsTest,
	"LiveBackend.FieldEventMarksAccepted",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSLiveFieldEventsTest::RunTest(const FString&)
{
	// The field events against the REAL server. These are the first events
	// measured in metres and the first where higher is better, so this is
	// the check that the distance contract holds end to end: the mark, the
	// absent reaction, the wind a jump has and a throw does not, and the
	// plausible band the client must never step outside.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->AddToRoot();
	GameInstance->InitializeStandalone();

	UWSOnlineSubsystem* Online = GameInstance->GetSubsystem<UWSOnlineSubsystem>();
	UWSProgressionSubsystem* Progression =
		GameInstance->GetSubsystem<UWSProgressionSubsystem>();
	if (!Online || !Progression)
	{
		GameInstance->RemoveFromRoot();
		AddError(TEXT("Subsystems missing"));
		return false;
	}

	TSharedPtr<WSLiveEventTest::FState> State = MakeShared<WSLiveEventTest::FState>();

	// Every field event, jumps then throws, one honest mark each.
	TArray<FWSEventResult> Marks;
	for (const FWSJumpEventSpec& Spec : WSJumpEvents::All())
	{
		Marks.Add(WSLiveEventTest::SimulateJump(Spec, 20260822u));
	}
	for (const FWSThrowEventSpec& Spec : WSThrowEvents::All())
	{
		Marks.Add(WSLiveEventTest::SimulateThrow(Spec, 20260822u));
	}

	TSharedPtr<TFunction<void()>> SubmitNext = MakeShared<TFunction<void()>>();
	*SubmitNext = [State, Online, Marks, SubmitNext]()
	{
		if (State->NextEvent >= Marks.Num())
		{
			State->bOk = true;
			State->bDone = true;
			return;
		}
		const FWSEventResult Run = Marks[State->NextEvent];
		Online->SubmitResult(Run,
			[State, Run, SubmitNext](EWSSubmitOutcome Outcome,
				const FWSResultResponse& Response, const FString& Error)
			{
				if (Outcome != EWSSubmitOutcome::Accepted)
				{
					State->Error = FString::Printf(
						TEXT("%s: %.2f m was not accepted — %s%s"),
						*Run.EventCode, Run.ValueNum,
						*Response.rejection_reason, *Error);
					State->bDone = true;
					return;
				}
				State->Report.Add(FString::Printf(
					TEXT("LIVE %-12s %.2f m -> %s%s"),
					*Run.EventCode, Run.ValueNum, *Response.value_text,
					Response.is_personal_best ? TEXT(" [PB]") : TEXT("")));
				++State->NextEvent;
				(*SubmitNext)();
			});
	};

	// A SEPARATE athlete from the running events.
	//
	// The server caps results per athlete per minute, which is exactly the
	// rule that stops a script farming marks — and this suite grew past it:
	// three sprints, two hurdles, two middle-distance runs, two field marks
	// and four tournament rounds all land on one athlete inside a minute,
	// and the server rightly refused the tail of them. Spreading the load
	// is the fix; raising the cap would be removing an anti-cheat rule to
	// suit a test.
	auto Continue = [State, Progression, SubmitNext]()
	{
		Progression->RefreshCareerAthlete(
			[State, Progression, SubmitNext](bool bRefreshed, const FString&)
			{
				if (bRefreshed && Progression->HasCareerAthlete())
				{
					(*SubmitNext)();
					return;
				}
				Progression->CreateCareerAthlete(TEXT("Unreal Field"), TEXT("M"),
					[State, SubmitNext](bool bCreated, const FString& CreateError)
					{
						if (!bCreated)
						{
							State->Error = FString::Printf(
								TEXT("no career athlete: %s"), *CreateError);
							State->bDone = true;
							return;
						}
						(*SubmitNext)();
					});
			});
	};

	const FString Email = TEXT("unreal-field@example.com");
	const FString Password = TEXT("unreal-field-pass-1");
	Online->RegisterAccount(Email, Password, TEXT("Unreal Field"),
		[State, Online, Email, Password, Continue](bool bOk, const FWSUserDto&, const FString&)
		{
			if (bOk)
			{
				Continue();
				return;
			}
			// Already registered from a previous run: sign in instead.
			Online->Login(Email, Password,
				[State, Continue](bool bSignedIn, const FWSUserDto&, const FString& Error)
				{
					if (!bSignedIn)
					{
						State->Error = FString::Printf(TEXT("login failed: %s"), *Error);
						State->bDone = true;
						return;
					}
					Continue();
				});
		});

	ADD_LATENT_AUTOMATION_COMMAND(FWSWaitForEventSubmits(State, GameInstance, this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
