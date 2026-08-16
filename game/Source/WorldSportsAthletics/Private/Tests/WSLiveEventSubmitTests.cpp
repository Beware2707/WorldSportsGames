#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Online/WSOnlineSubsystem.h"
#include "Progression/WSProgressionSubsystem.h"
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
	const FWSSprintOutcome Outcome =
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
					Progression->CreateCareerAthlete(TEXT("Unreal Smoke"), TEXT("female"),
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

#endif // WITH_DEV_AUTOMATION_TESTS
