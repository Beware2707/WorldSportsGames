#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Online/WSOnlineSubsystem.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
