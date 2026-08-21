#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Online/WSOnlineSubsystem.h"
#include "Progression/WSProgressionSubsystem.h"
#include "Progression/WSTournamentSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Live smoke against a RUNNING backend (Phase 1 exit criterion: the client
 * authenticates for real). Deliberately named outside the "WorldSports."
 * prefix so the offline suite never depends on a server:
 *
 *   Automation RunTests LiveBackend
 *
 * Registers a fixed smoke account (409 "already exists" falls back to
 * login), which proves: form-encoded login, bearer round-trip on /auth/me,
 * and DTO parsing — the whole auth path end to end.
 */

namespace WSLiveTest
{
struct FState
{
	bool bDone = false;
	bool bOk = false;
	FString Error;
	FWSUserDto User;
};
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FWSWaitForAuth,
	TSharedPtr<WSLiveTest::FState>, State, UGameInstance*, GameInstance, FAutomationTestBase*, Test);

bool FWSWaitForAuth::Update()
{
	constexpr double TimeoutSeconds = 30.0;
	if (!State->bDone && GetCurrentRunTime() < TimeoutSeconds)
	{
		return false; // keep waiting
	}
	if (!State->bDone)
	{
		Test->AddError(TEXT("Timed out waiting for the backend — is uvicorn running on the configured BaseUrl?"));
	}
	else if (!State->bOk)
	{
		Test->AddError(FString::Printf(TEXT("Auth failed: %s"), *State->Error));
	}
	else
	{
		UWSOnlineSubsystem* Online = GameInstance->GetSubsystem<UWSOnlineSubsystem>();
		Test->TestTrue(TEXT("signed in"), Online && Online->IsSignedIn());
		Test->TestTrue(TEXT("server assigned a user id"), State->User.id > 0);
		Test->TestEqual(TEXT("display name round-tripped"),
			State->User.display_name, FString(TEXT("Unreal Smoke")));
	}
	GameInstance->Shutdown();
	GameInstance->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSLiveAuthRoundTripTest,
	"LiveBackend.AuthRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSLiveAuthRoundTripTest::RunTest(const FString&)
{
	// Subsystems live inside a GameInstance; a standalone one gives the test
	// the real initialization path instead of a hand-rolled object.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->AddToRoot(); // keep alive across latent ticks
	GameInstance->InitializeStandalone();
	UWSOnlineSubsystem* Online = GameInstance->GetSubsystem<UWSOnlineSubsystem>();
	if (!Online)
	{
		GameInstance->RemoveFromRoot();
		AddError(TEXT("Online subsystem missing from standalone GameInstance"));
		return false;
	}

	TSharedPtr<WSLiveTest::FState> State = MakeShared<WSLiveTest::FState>();
	const FString Email = TEXT("unreal-smoke@example.com");
	const FString Password = TEXT("unreal-smoke-pass-1");

	auto Finish = [State](bool bOk, const FWSUserDto& User, const FString& Error)
	{
		State->bOk = bOk;
		State->User = User;
		State->Error = Error;
		State->bDone = true;
	};

	Online->RegisterAccount(Email, Password, TEXT("Unreal Smoke"),
		[Online, State, Email, Password, Finish](bool bOk, const FWSUserDto& User, const FString& Error)
		{
			if (!bOk && Error.Contains(TEXT("already exists")))
			{
				Online->Login(Email, Password, Finish);
				return;
			}
			Finish(bOk, User, Error);
		});

	ADD_LATENT_AUTOMATION_COMMAND(FWSWaitForAuth(State, GameInstance, this));
	return true;
}

/**
 * Live career loop through the REAL client subsystems: sign in, create the
 * career athlete, train it, and confirm the server — not the client — moved
 * the attribute.
 *
 *   Automation RunTests LiveBackend
 */

namespace WSLiveTest
{
struct FCareerState
{
	bool bDone = false;
	bool bOk = false;
	FString Error;
	float AttributeBefore = -1.0f;
	float AttributeAfter = -1.0f;
	float ReportedGain = 0.0f;
	FString Stage;
	int32 TotalXp = 0;
};
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FWSWaitForCareer,
	TSharedPtr<WSLiveTest::FCareerState>, State, UGameInstance*, GameInstance,
	FAutomationTestBase*, Test);

bool FWSWaitForCareer::Update()
{
	constexpr double TimeoutSeconds = 60.0;
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
		Test->AddError(FString::Printf(TEXT("Career loop failed: %s"), *State->Error));
	}
	else
	{
		// The attribute moved by exactly what the SERVER reported — the
		// client never adds a gain of its own.
		Test->TestTrue(TEXT("attribute rose"), State->AttributeAfter > State->AttributeBefore);
		Test->TestEqual(TEXT("moved by the server's reported gain"),
			State->AttributeAfter, State->AttributeBefore + State->ReportedGain, 0.001f);
		// A single session must be small: training is slow by design so
		// execution, not accumulation, decides races.
		Test->TestTrue(FString::Printf(TEXT("gain %.3f is a slow increment"), State->ReportedGain),
			State->ReportedGain > 0.0f && State->ReportedGain < 1.0f);
		Test->TestFalse(TEXT("stage is known"), State->Stage.IsEmpty());
		Test->TestTrue(TEXT("XP was awarded"), State->TotalXp > 0);
	}
	GameInstance->Shutdown();
	GameInstance->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSLiveCareerLoopTest,
	"LiveBackend.CareerTrainingLoop",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSLiveCareerLoopTest::RunTest(const FString&)
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

	TSharedPtr<WSLiveTest::FCareerState> State = MakeShared<WSLiveTest::FCareerState>();
	const FString Email = TEXT("unreal-smoke@example.com");
	const FString Password = TEXT("unreal-smoke-pass-1");

	auto Fail = [State](const FString& Why)
	{
		State->Error = Why;
		State->bDone = true;
	};

	auto AfterSignIn = [State, Progression, Fail](bool bOk, const FWSUserDto&, const FString& Error)
	{
		if (!bOk)
		{
			Fail(Error);
			return;
		}
		// Create (or adopt) the career athlete, then train it.
		Progression->CreateCareerAthlete(TEXT("Live Tester"), TEXT("X"),
			[State, Progression, Fail](bool bCreated, const FString& CreateError)
			{
				if (!bCreated)
				{
					Fail(CreateError);
					return;
				}
				State->AttributeBefore =
					Progression->GetCareerAthlete().attributes.FindRef(TEXT("reaction"));

				Progression->SubmitTraining(TEXT("reaction-drill"), 155.0,
					[State, Progression, Fail](bool bTrained,
						const FWSTrainingResponse& Response, const FString& TrainError)
					{
						if (!bTrained)
						{
							Fail(TrainError);
							return;
						}
						if (!Response.accepted)
						{
							Fail(FString::Printf(TEXT("session rejected: %s"),
								*Response.rejection_reason));
							return;
						}
						State->ReportedGain = Response.attribute_gain;
						State->AttributeAfter = Response.attribute_after;
						State->Stage = Response.career_stage;
						State->TotalXp = Response.total_xp;
						State->bOk = true;
						State->bDone = true;
					});
			});
	};

	Online->RegisterAccount(Email, Password, TEXT("Unreal Smoke"),
		[Online, Email, Password, AfterSignIn](bool bOk, const FWSUserDto& User, const FString& Error)
		{
			if (!bOk && Error.Contains(TEXT("already exists")))
			{
				Online->Login(Email, Password, AfterSignIn);
				return;
			}
			AfterSignIn(bOk, User, Error);
		});

	ADD_LATENT_AUTOMATION_COMMAND(FWSWaitForCareer(State, GameInstance, this));
	return true;
}

/**
 * Live tournament: enter a bracket and race rounds until the server says
 * the tournament is over. Proves the CLIENT never decides advancement —
 * every position and every "advanced" flag comes back from the server,
 * scored against the field it stored before the round was run.
 */

namespace WSLiveTest
{
struct FBracketState
{
	bool bDone = false;
	bool bOk = false;
	FString Error;
	int32 RoundsRun = 0;
	TArray<FString> RoundNames;
	TArray<int32> FieldSizes;
	FString FinalStatus;
	int32 FinalPosition = 0;
};
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FWSWaitForBracket,
	TSharedPtr<WSLiveTest::FBracketState>, State, UGameInstance*, GameInstance,
	FAutomationTestBase*, Test);

bool FWSWaitForBracket::Update()
{
	constexpr double TimeoutSeconds = 90.0;
	if (!State->bDone && GetCurrentRunTime() < TimeoutSeconds)
	{
		return false;
	}
	if (!State->bDone)
	{
		Test->AddError(FString::Printf(
			TEXT("Timed out after %d round(s) — backend reachable?"), State->RoundsRun));
	}
	else if (!State->bOk)
	{
		Test->AddError(FString::Printf(TEXT("Bracket failed: %s"), *State->Error));
	}
	else
	{
		Test->TestTrue(TEXT("at least one round was run"), State->RoundsRun >= 1);
		// Every round the server accepted came with a stored field.
		for (int32 Index = 0; Index < State->FieldSizes.Num(); ++Index)
		{
			Test->TestTrue(FString::Printf(TEXT("round %s had a field"),
					*State->RoundNames[Index]),
				State->FieldSizes[Index] > 0);
		}
		// The bracket ends in exactly one of the server's terminal states.
		Test->TestTrue(FString::Printf(TEXT("terminal status '%s'"), *State->FinalStatus),
			State->FinalStatus == TEXT("completed") ||
			State->FinalStatus == TEXT("eliminated"));
		// A final position exists ONLY for a completed final. Being knocked
		// out in the semifinal has no final placing, and asserting one was
		// this test inventing a rule the sport does not have.
		if (State->FinalStatus == TEXT("completed"))
		{
			Test->TestTrue(TEXT("a completed final has a position"),
				State->FinalPosition >= 1 && State->FinalPosition <= 8);
		}
		else
		{
			Test->TestEqual(TEXT("elimination has no final position"),
				State->FinalPosition, 0);
		}
		// Rounds must run in the server's order, never skipped — but the
		// bracket may legitimately be RESUMED part-way, because a
		// tournament survives the app closing and this test may meet one
		// already under way from an earlier run. What has to hold is that
		// the rounds seen are consecutive and in order from wherever they
		// started, not that they always start at qualification.
		static const TCHAR* Order[] = {TEXT("qualification"), TEXT("heat"),
			TEXT("semifinal"), TEXT("final")};
		int32 Start = 0;
		if (State->RoundNames.Num() > 0)
		{
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Order); ++Index)
			{
				if (State->RoundNames[0] == FString(Order[Index]))
				{
					Start = Index;
					break;
				}
			}
		}
		for (int32 Index = 0; Index < State->RoundNames.Num(); ++Index)
		{
			const int32 Expected = Start + Index;
			if (!Test->TestTrue(TEXT("the bracket does not run past the final"),
					Expected < UE_ARRAY_COUNT(Order)))
			{
				break;
			}
			Test->TestEqual(TEXT("rounds run in bracket order"),
				State->RoundNames[Index], FString(Order[Expected]));
		}
	}
	GameInstance->Shutdown();
	GameInstance->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSLiveTournamentTest,
	"LiveBackend.TournamentBracket",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSLiveTournamentTest::RunTest(const FString&)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->AddToRoot();
	GameInstance->InitializeStandalone();

	UWSOnlineSubsystem* Online = GameInstance->GetSubsystem<UWSOnlineSubsystem>();
	UWSTournamentSubsystem* Tournaments =
		GameInstance->GetSubsystem<UWSTournamentSubsystem>();
	if (!Online || !Tournaments)
	{
		GameInstance->RemoveFromRoot();
		AddError(TEXT("Subsystems missing"));
		return false;
	}

	TSharedPtr<WSLiveTest::FBracketState> State = MakeShared<WSLiveTest::FBracketState>();
	const FString Email = TEXT("unreal-smoke@example.com");
	const FString Password = TEXT("unreal-smoke-pass-1");

	// One honest run, reused for each round: a mid-pack 12.4s that the
	// server's validation accepts for a default-attribute athlete.
	auto MakeRun = []
	{
		FWSEventResult Result;
		Result.EventCode = TEXT("sprint-100m");
		Result.ValueNum = 12.4;
		Result.bHasReactionMs = true;
		Result.ReactionMs = 165.0;
		Result.bHasWind = true;
		Result.Wind = 0.4;
		const double Split = (12.4 - 0.165) / 10.0;
		for (int32 Index = 0; Index < 10; ++Index)
		{
			Result.Splits.Add(FMath::RoundToDouble(Split * 10000.0) / 10000.0);
		}
		return Result;
	};

	// Declared first so the lambda can recurse through the shared pointer.
	TSharedPtr<TFunction<void()>> RaceNext = MakeShared<TFunction<void()>>();
	*RaceNext = [State, Tournaments, MakeRun, RaceNext]()
	{
		if (!Tournaments->HasActiveTournament())
		{
			State->FinalStatus = Tournaments->GetActive().Status;
			State->FinalPosition = Tournaments->GetActive().FinalPosition;
			State->bOk = true;
			State->bDone = true;
			return;
		}
		if (State->RoundsRun >= 6) // ROUND_ORDER is 4 long; 6 is a safety net
		{
			State->Error = TEXT("bracket did not terminate");
			State->bDone = true;
			return;
		}
		State->RoundNames.Add(Tournaments->GetActive().CurrentRound);
		State->FieldSizes.Add(Tournaments->GetCurrentField().Num());

		Tournaments->SubmitRound(MakeRun(),
			[State, Tournaments, RaceNext](bool bOk, const FWSTournamentResultDto& Response,
				const FString& Error)
			{
				if (!bOk)
				{
					State->Error = Error;
					State->bDone = true;
					return;
				}
				if (!Response.accepted)
				{
					State->Error = FString::Printf(TEXT("round rejected: %s"),
						*Response.rejection_reason);
					State->bDone = true;
					return;
				}
				++State->RoundsRun;
				if (Response.tournament_status == TEXT("complete") ||
					Response.tournament_status == TEXT("eliminated"))
				{
					State->FinalStatus = Response.tournament_status;
					State->FinalPosition = Response.final_position;
					State->bOk = true;
					State->bDone = true;
					return;
				}
				// The next round's field is the server's, so re-read before
				// racing it.
				Tournaments->Refresh([RaceNext](bool, const FString&)
				{
					(*RaceNext)();
				});
			});
	};

	Online->RegisterAccount(Email, Password, TEXT("Unreal Smoke"),
		[Online, Tournaments, State, Email, Password, RaceNext](
			bool bOk, const FWSUserDto&, const FString& Error)
		{
			auto AfterAuth = [Tournaments, State, RaceNext](bool bAuthOk,
				const FWSUserDto&, const FString& AuthError)
			{
				if (!bAuthOk)
				{
					State->Error = AuthError;
					State->bDone = true;
					return;
				}
				Tournaments->EnterOrResume(TEXT("sprint-100m"),
					[State, RaceNext](bool bEntered, const FString& EnterError)
					{
						if (!bEntered)
						{
							State->Error = EnterError;
							State->bDone = true;
							return;
						}
						(*RaceNext)();
					});
			};
			if (!bOk && Error.Contains(TEXT("already exists")))
			{
				Online->Login(Email, Password, AfterAuth);
				return;
			}
			AfterAuth(bOk, FWSUserDto(), Error);
		});

	ADD_LATENT_AUTOMATION_COMMAND(FWSWaitForBracket(State, GameInstance, this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
