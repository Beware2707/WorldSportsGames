#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Online/WSOnlineSubsystem.h"
#include "Progression/WSProgressionSubsystem.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
