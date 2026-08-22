#include "Race/WSSprintGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Core/WSLog.h"
#include "Framework/WSGameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Online/WSOnlineSubsystem.h"
#include "Progression/WSProgressionSubsystem.h"
#include "Progression/WSTournamentSubsystem.h"
#include "Race/WSSprintAudio.h"
#include "Race/WSSprintHud.h"
#include "Race/WSSprintPlayerController.h"
#include "Race/WSSprintRunner.h"
#include "Math/RandomStream.h"
#include "Race/WSSprintTrack.h"
#include "Simulation/WSSprintDifficulty.h"
#include "Simulation/WSJumpSimulation.h"
#include "Simulation/WSThrowSimulation.h"
#include "Simulation/WSSprintEvents.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sports/Results/WSEventResult.h"

namespace
{
// The starter holds the field for a variable pause before the gun.
constexpr double MinSetSeconds = 2.2;
constexpr double MaxSetSeconds = 4.6;
constexpr float ResultDwellSeconds = 1.2f;
const TCHAR* DefaultEventCode = TEXT("sprint-100m");
// How far ahead a barrier arms the takeoff action, and prompts for it.
// Wide enough to press early and still be judged on the timing, narrow
// enough that it never swallows the dip at the line.
constexpr double HurdlePromptMetres = 7.0;

const TCHAR* OpponentNames[] = {
	TEXT("A. Mensah"), TEXT("K. Ito"), TEXT("L. Duarte"), TEXT("R. Novak"),
	TEXT("T. Bekele"), TEXT("M. Sørensen"), TEXT("J. Okafor"),
};
}

AWSSprintGameMode::AWSSprintGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerControllerClass = AWSSprintPlayerController::StaticClass();
	DefaultPawnClass = nullptr; // the runner actors are the visuals
	SprintHudClass = UWSSprintHud::StaticClass();
}

void AWSSprintGameMode::BeginPlay()
{
	Super::BeginPlay();

	Track = GetWorld()->SpawnActor<AWSSprintTrack>(
		AWSSprintTrack::StaticClass(), FTransform::Identity);

	RaceCamera = GetWorld()->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), FTransform::Identity);
	APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	if (Controller)
	{
		Controller->SetViewTarget(RaceCamera);
	}

	// A headless race (automation, server-side replay) has no local player
	// to show a HUD to; the race itself must run identically without one.
	if (Controller && GetGameInstance())
	{
		Hud = CreateWidget<UWSSprintHud>(Controller, SprintHudClass);
		if (Hud)
		{
			Hud->BindGameMode(this);
			Hud->AddToViewport();
		}
		InitAudio();
	}

	// The app opens on the menu, not mid-race: a player must be able to sign
	// in and read a leaderboard without racing first.
	AppState = EWSAppState::Menu;
}

void AWSSprintGameMode::ShowScreen(EWSAppState NewState)
{
	AppState = NewState;
	// Screens that show server state refresh on entry, wherever entry came
	// from. Refreshing only in the menu button's handler left the career
	// screen showing "Sign in to start a career" to a signed-in player who
	// arrived any other way.
	if (NewState == EWSAppState::Leaderboard)
	{
		RefreshLeaderboard();
	}
	else if (NewState == EWSAppState::Career)
	{
		RefreshCareer();
	}
}

void AWSSprintGameMode::StartQuickPlay()
{
	// Quick Play is explicitly NOT a tournament round.
	bTournamentRace = false;
	AppState = EWSAppState::Racing;
	bPaused = false;
	StartRace();
}

void AWSSprintGameMode::ReturnToMenu()
{
	// Abandoning mid-race simply ends it. Nothing is submitted, because
	// nothing was finished — an unfinished run has no time to claim.
	bRaceRunning = false;
	bPaused = false;
	bTournamentRace = false;
	++RaceGeneration; // any in-flight submit answer is now stale
	AppState = EWSAppState::Menu;
	SetPhase(EWSEventPhase::Load);
}

void AWSSprintGameMode::SetPaused(bool bPause)
{
	// Pausing freezes the race clock. It cannot buy speed: the simulation is
	// driven by that clock, and cadence accuracy is measured against it.
	bPaused = bPause && bRaceRunning;
}

bool AWSSprintGameMode::IsSignedIn() const
{
	UWSOnlineSubsystem* Online =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr;
	return Online && Online->IsSignedIn();
}

FString AWSSprintGameMode::GetSignedInName() const
{
	UWSOnlineSubsystem* Online =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr;
	return Online ? Online->GetSignedInUser().display_name : FString();
}

void AWSSprintGameMode::SubmitCredentials(const FString& Email, const FString& Password,
	const FString& DisplayName, bool bRegister)
{
	UWSOnlineSubsystem* Online =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr;
	if (!Online)
	{
		AccountStatus = TEXT("Online service unavailable");
		return;
	}
	if (Email.IsEmpty() || Password.IsEmpty())
	{
		AccountStatus = TEXT("Enter an email and password");
		return;
	}

	AccountStatus = bRegister ? TEXT("Creating your account…") : TEXT("Signing in…");
	TWeakObjectPtr<AWSSprintGameMode> WeakThis(this);
	auto OnDone = [WeakThis](bool bOk, const FWSUserDto& User, const FString& Error)
	{
		if (AWSSprintGameMode* Self = WeakThis.Get())
		{
			// The server's own words on failure — never a generic "error".
			Self->AccountStatus = bOk
				? FString::Printf(TEXT("Signed in as %s"), *User.display_name)
				: Error;
			// Only leave the sign-in screen if that is still where the
			// player is. Slamming AppState to Menu buried a live race the
			// player could then neither see, pause, nor quit.
			if (bOk && Self->AppState == EWSAppState::SignIn)
			{
				Self->AppState = EWSAppState::Menu;
			}
		}
	};

	if (bRegister)
	{
		Online->RegisterAccount(Email, Password,
			DisplayName.IsEmpty() ? TEXT("Athlete") : DisplayName, OnDone);
	}
	else
	{
		Online->Login(Email, Password, OnDone);
	}
}

void AWSSprintGameMode::SignOut()
{
	if (UWSOnlineSubsystem* Online =
			GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr)
	{
		Online->Logout();
	}
	AccountStatus = TEXT("Signed out");
}

void AWSSprintGameMode::RefreshLeaderboard()
{
	UWSOnlineSubsystem* Online =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr;
	if (!Online)
	{
		LeaderboardStatus = TEXT("Online service unavailable");
		return;
	}
	if (bLeaderboardInFlight)
	{
		return; // re-entering the screen must not duplicate every row
	}
	bLeaderboardInFlight = true;
	LeaderboardRows.Reset();
	LeaderboardStatus = TEXT("Loading…");

	TWeakObjectPtr<AWSSprintGameMode> WeakThis(this);
	Online->Request(TEXT("GET"),
		FString::Printf(
			TEXT("/api/v1/career/leaderboard?event=%s&scope=global&period=all_time"),
			*SelectedEventCodeOrDefault()),
		nullptr,
		[WeakThis](const FWSHttpResult& Result)
		{
			AWSSprintGameMode* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bLeaderboardInFlight = false;
			if (!Result.IsSuccess() || !Result.Json.IsValid())
			{
				Self->LeaderboardStatus = Result.bTransportOk
					? FString::Printf(TEXT("Leaderboard unavailable (%d)"), Result.StatusCode)
					: TEXT("No connection to the server");
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
			if (!Result.Json->TryGetArrayField(TEXT("rows"), Rows))
			{
				Self->LeaderboardStatus = TEXT("Leaderboard response was malformed");
				return;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Rows)
			{
				const TSharedPtr<FJsonObject> Row = Value->AsObject();
				if (!Row.IsValid())
				{
					continue;
				}
				FWSLeaderboardRow Entry;
				Row->TryGetNumberField(TEXT("rank"), Entry.Rank);
				Row->TryGetStringField(TEXT("athlete_name"), Entry.AthleteName);
				Row->TryGetStringField(TEXT("value_text"), Entry.ValueText);
				const TSharedPtr<FJsonObject>* Country = nullptr;
				if (Row->TryGetObjectField(TEXT("country"), Country) && Country)
				{
					(*Country)->TryGetStringField(TEXT("code"), Entry.Country);
				}
				Self->LeaderboardRows.Add(MoveTemp(Entry));
			}
			// An empty board is stated as empty, never dressed up with
			// placeholder names.
			Self->LeaderboardStatus = Self->LeaderboardRows.Num() > 0
				? FString()
				: TEXT("No verified times yet — run one and be first.");
		});
}

// -- Career -------------------------------------------------------------

UWSProgressionSubsystem* AWSSprintGameMode::Progression() const
{
	return GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWSProgressionSubsystem>()
		: nullptr;
}

bool AWSSprintGameMode::HasCareerAthlete() const
{
	const UWSProgressionSubsystem* P = Progression();
	return P && P->HasCareerAthlete();
}

void AWSSprintGameMode::RefreshCareer()
{
	UWSProgressionSubsystem* P = Progression();
	if (!P || !IsSignedIn())
	{
		CareerStatus = TEXT("Sign in to start a career");
		return;
	}
	CareerStatus = TEXT("Loading…");
	RecordsText.Reset();

	TWeakObjectPtr<AWSSprintGameMode> WeakThis(this);
	P->RefreshCareerAthlete([WeakThis](bool bOk, const FString& Error)
	{
		AWSSprintGameMode* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->CareerStatus = bOk ? FString() : Error;
		if (!bOk)
		{
			return;
		}
		// Records come from the server's audit trail; the client never
		// computes a personal best of its own.
		if (UWSOnlineSubsystem* Online = Self->GetGameInstance()
				? Self->GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr)
		{
			Online->Request(TEXT("GET"), TEXT("/api/v1/career/records"), nullptr,
				[WeakThis](const FWSHttpResult& Result)
				{
					AWSSprintGameMode* Inner = WeakThis.Get();
					if (!Inner || !Result.IsSuccess() || !Result.Json.IsValid())
					{
						return;
					}
					const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
					if (!Result.Json->TryGetArrayField(TEXT(""), Rows))
					{
						// A bare JSON array parses into the value, not an
						// object field; read it from the raw body instead.
					}
					FString Text;
					TArray<TSharedPtr<FJsonValue>> Parsed;
					const TSharedRef<TJsonReader<>> Reader =
						TJsonReaderFactory<>::Create(Result.Body);
					if (FJsonSerializer::Deserialize(Reader, Parsed))
					{
						for (const TSharedPtr<FJsonValue>& Value : Parsed)
						{
							const TSharedPtr<FJsonObject> Row = Value->AsObject();
							if (!Row.IsValid())
							{
								continue;
							}
							FString Event, Pb, Wb, Holder;
							Row->TryGetStringField(TEXT("event"), Event);
							Row->TryGetStringField(TEXT("personal_best_text"), Pb);
							Row->TryGetStringField(TEXT("world_best_text"), Wb);
							Row->TryGetStringField(TEXT("world_best_holder"), Holder);
							Text += FString::Printf(TEXT("%s\n  PB %s    Best %s%s\n"),
								*Event,
								Pb.IsEmpty() ? TEXT("—") : *Pb,
								Wb.IsEmpty() ? TEXT("—") : *Wb,
								Holder.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Holder));
						}
					}
					Inner->RecordsText = Text;
				});
		}
	});
}

void AWSSprintGameMode::CreateAthlete(const FString& Name, const FString& Gender)
{
	UWSProgressionSubsystem* P = Progression();
	if (!P)
	{
		CareerStatus = TEXT("Online service unavailable");
		return;
	}
	if (Name.TrimStartAndEnd().IsEmpty())
	{
		CareerStatus = TEXT("Give your athlete a name");
		return;
	}
	CareerStatus = TEXT("Creating your athlete…");

	TWeakObjectPtr<AWSSprintGameMode> WeakThis(this);
	P->CreateCareerAthlete(Name.TrimStartAndEnd(), Gender,
		[WeakThis](bool bOk, const FString& Error)
		{
			if (AWSSprintGameMode* Self = WeakThis.Get())
			{
				Self->CareerStatus = bOk ? FString() : Error;
				if (bOk && Self->AppState == EWSAppState::CreateAthlete)
				{
					Self->ShowScreen(EWSAppState::Career);
				}
			}
		});
}

FString AWSSprintGameMode::GetCareerSummary() const
{
	const UWSProgressionSubsystem* P = Progression();
	if (!P || !P->HasCareerAthlete())
	{
		return FString();
	}
	const FWSCareerAthleteDto& A = P->GetCareerAthlete();

	// Attributes in the order the design lists them, with the two that do
	// not yet affect any event marked as such rather than quietly implying
	// they do.
	static const TCHAR* Keys[] = {TEXT("reaction"), TEXT("acceleration"),
		TEXT("max_speed"), TEXT("stride_efficiency"), TEXT("stamina"),
		TEXT("recovery"), TEXT("technique")};
	static const TCHAR* Labels[] = {TEXT("Reaction"), TEXT("Acceleration"),
		TEXT("Max speed"), TEXT("Stride efficiency"), TEXT("Stamina"),
		TEXT("Recovery"), TEXT("Technique")};

	FString Text = FString::Printf(TEXT("%s   %s   %d XP\n\n"),
		*A.name, *A.career_stage.Replace(TEXT("_"), TEXT(" ")), A.total_xp);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Keys); ++Index)
	{
		const float Value = A.attributes.FindRef(Keys[Index]);
		const int32 Filled = FMath::Clamp(FMath::RoundToInt(Value / 5.0f), 0, 20);
		Text += FString::Printf(TEXT("%-18s %5.1f  %s%s\n"),
			Labels[Index], Value,
			*FString::ChrN(Filled, TEXT('|')),
			*FString::ChrN(20 - Filled, TEXT('.')));
	}
	// Honesty: recovery governs no event yet, so training it changes nothing
	// mechanical. Saying so beats letting a player grind it for nothing.
	Text += TEXT("\nRecovery does not affect the 100m yet.\n");
	return Text;
}

// -- Tournament ----------------------------------------------------------

const FWSSprintEventSpec& AWSSprintGameMode::CurrentEvent() const
{
	return WSSprintEvents::Find(SelectedEventCode.IsEmpty()
		? FString(DefaultEventCode) : SelectedEventCode);
}

void AWSSprintGameMode::CycleEvent(int32 Delta)
{
	// Changing event mid-race would leave the runners simulating one
	// distance while the finish line moved to another.
	if (bRaceRunning)
	{
		return;
	}
	const TArray<FString> Codes = AllEventCodes();
	if (Codes.Num() == 0)
	{
		return;
	}
	const int32 Current = FMath::Max(0, Codes.IndexOfByKey(SelectedEventCodeOrDefault()));
	const int32 Index = (Current + Delta % Codes.Num() + Codes.Num()) % Codes.Num();
	SelectedEventCode = Codes[Index];
	// The board on screen is for the event you were looking at, not the one
	// you just switched to.
	LeaderboardRows.Reset();
	LeaderboardStatus = FString();
}

const FWSPaceEventSpec& AWSSprintGameMode::CurrentPaceEvent() const
{
	return WSPaceEvents::Find(SelectedEventCode);
}

const FWSRelayEventSpec& AWSSprintGameMode::CurrentRelayEvent() const
{
	return WSRelayEvents::Find(SelectedEventCode);
}

bool AWSSprintGameMode::IsRelayEvent() const
{
	// Membership, not a naming convention — the same rule every other kind
	// is decided by.
	for (const FWSRelayEventSpec& Spec : WSRelayEvents::All())
	{
		if (Spec.Code == SelectedEventCode)
		{
			return true;
		}
	}
	return false;
}

const FWSJumpEventSpec& AWSSprintGameMode::CurrentJumpEvent() const
{
	return WSJumpEvents::Find(SelectedEventCode);
}

const FWSThrowEventSpec& AWSSprintGameMode::CurrentThrowEvent() const
{
	return WSThrowEvents::Find(SelectedEventCode);
}

bool AWSSprintGameMode::IsThrowEvent() const
{
	for (const FWSThrowEventSpec& Spec : WSThrowEvents::All())
	{
		if (Spec.Code == SelectedEventCode)
		{
			return true;
		}
	}
	return false;
}

float AWSSprintGameMode::GetThrowPower() const
{
	return ThrowSim.IsValid() ? static_cast<float>(ThrowSim->GetState().Power) : 0.0f;
}

float AWSSprintGameMode::GetThrowWindUp() const
{
	return ThrowSim.IsValid()
		? FMath::Clamp(static_cast<float>(ThrowSim->GetState().WindUp), 0.0f, 1.0f)
		: 0.0f;
}

void AWSSprintGameMode::PlayerThrowRelease()
{
	if (!ThrowSim.IsValid() || !bRaceRunning || bPaused || RaceClock < 0.0)
	{
		return;
	}
	const FWSThrowInputEvent Release{RaceClock, EWSThrowInputType::Release};
	PlayerThrowTrace.Add(Release);
	ThrowSim->AddInput(Release);
}

bool AWSSprintGameMode::FieldResultHasWind() const
{
	return IsJumpEvent() && !IsVerticalEvent();
}

int32 AWSSprintGameMode::GetFailuresAllowed() const
{
	return IsJumpEvent() ? CurrentJumpEvent().FailuresAllowed : 0;
}

int32 AWSSprintGameMode::GetJumpPhaseCount() const
{
	return IsJumpEvent() ? CurrentJumpEvent().PhaseCount : 1;
}

int32 AWSSprintGameMode::GetJumpPhase() const
{
	// A finished attempt is in no phase at all: reporting the last one
	// would leave "JUMP" on screen while the athlete walks back.
	return IsFieldAttemptLive() ? JumpSim->GetState().Phase : 0;
}

bool AWSSprintGameMode::IsJumpPhaseWindowOpen() const
{
	return JumpSim.IsValid() && JumpSim->GetState().bPhaseWindowOpen;
}

bool AWSSprintGameMode::IsFieldAttemptLive() const
{
	if (AttemptRestSeconds > 0.0f)
	{
		return false;
	}
	if (JumpSim.IsValid())
	{
		return !JumpSim->GetState().bFinished;
	}
	if (ThrowSim.IsValid())
	{
		return !ThrowSim->GetState().bFinished;
	}
	return false;
}

double AWSSprintGameMode::GetJumpPhaseTimeRemaining() const
{
	return JumpSim.IsValid() ? JumpSim->GetState().PhaseTimeRemaining : 0.0;
}

bool AWSSprintGameMode::IsVerticalEvent() const
{
	return IsJumpEvent() && CurrentJumpEvent().bVertical;
}

int32 AWSSprintGameMode::FieldAttemptCount() const
{
	if (IsThrowEvent())
	{
		return CurrentThrowEvent().Attempts;
	}
	return IsJumpEvent() ? CurrentJumpEvent().Attempts : 0;
}

bool AWSSprintGameMode::IsJumpEvent() const
{
	for (const FWSJumpEventSpec& Spec : WSJumpEvents::All())
	{
		if (Spec.Code == SelectedEventCode)
		{
			return true;
		}
	}
	return false;
}

int32 AWSSprintGameMode::GetJumpAttempt() const
{
	if (!IsJumpEvent())
	{
		return 0;
	}
	return FMath::Min(AttemptIndex + 1, CurrentJumpEvent().Attempts);
}

float AWSSprintGameMode::GetMetresToBoard() const
{
	return JumpSim.IsValid() ? static_cast<float>(JumpSim->GetState().MetresToBoard) : 0.0f;
}

FString AWSSprintGameMode::GetAttemptSummary() const
{
	FString Text;
	for (int32 Index = 0; Index < Attempts.Num(); ++Index)
	{
		// A foul is not a short attempt, and writing it as 0.00 m would say
		// it was. The scoreboard says X and WHY, because the two failures
		// need opposite corrections.
		if (Attempts[Index].bVertical)
		{
			// A high jump card shows the BAR and whether it survived, which
			// is what the athlete was actually attempting.
			Text += FString::Printf(TEXT("%.2f m  %s") LINE_TERMINATOR,
				Attempts[Index].BarMetres,
				Attempts[Index].bFoul ? TEXT("X") : TEXT("O"));
			continue;
		}
		Text += Attempts[Index].bFoul
			? FString::Printf(TEXT("%d. X  %s") LINE_TERMINATOR, Index + 1,
				*Attempts[Index].FoulReason)
			: FString::Printf(TEXT("%d. %.2f m") LINE_TERMINATOR, Index + 1,
				Attempts[Index].Metres);
	}
	return Text;
}

bool AWSSprintGameMode::IsPaceEvent() const
{
	// Membership, not a naming convention: an event is paced because it is
	// in the paced table, not because its code happens to start with a word.
	for (const FWSPaceEventSpec& Spec : WSPaceEvents::All())
	{
		if (Spec.Code == SelectedEventCode)
		{
			return true;
		}
	}
	return false;
}

FString AWSSprintGameMode::SelectedEventCodeOrDefault() const
{
	return SelectedEventCode.IsEmpty() ? FString(DefaultEventCode) : SelectedEventCode;
}

double AWSSprintGameMode::SelectedDistanceMetres() const
{
	if (IsRelayEvent())
	{
		return CurrentRelayEvent().TotalMetres();
	}
	return IsPaceEvent() ? CurrentPaceEvent().DistanceMetres : CurrentEvent().DistanceMetres;
}

int32 AWSSprintGameMode::SelectedSplitCount() const
{
	if (IsRelayEvent())
	{
		// A relay's splits are its LEGS. Marking a 4x400 every 50m would
		// label the handovers as something they are not.
		return CurrentRelayEvent().LegCount;
	}
	return IsPaceEvent() ? CurrentPaceEvent().SplitCount : CurrentEvent().SplitCount;
}

FString AWSSprintGameMode::GetSelectedEventName() const
{
	if (IsThrowEvent())
	{
		return CurrentThrowEvent().DisplayName;
	}
	if (IsJumpEvent())
	{
		return CurrentJumpEvent().DisplayName;
	}
	if (IsRelayEvent())
	{
		return CurrentRelayEvent().DisplayName;
	}
	return IsPaceEvent() ? CurrentPaceEvent().DisplayName : CurrentEvent().DisplayName;
}

void AWSSprintGameMode::SelectEvent(const FString& EventCode)
{
	// Only events a table knows: an unknown code would race a distance the
	// server cannot validate a time for.
	for (const FWSPaceEventSpec& Spec : WSPaceEvents::All())
	{
		if (Spec.Code == EventCode)
		{
			SelectedEventCode = Spec.Code;
			return;
		}
	}
	for (const FWSJumpEventSpec& Spec : WSJumpEvents::All())
	{
		if (Spec.Code == EventCode)
		{
			SelectedEventCode = Spec.Code;
			return;
		}
	}
	for (const FWSThrowEventSpec& Spec : WSThrowEvents::All())
	{
		if (Spec.Code == EventCode)
		{
			SelectedEventCode = Spec.Code;
			return;
		}
	}
	for (const FWSRelayEventSpec& Spec : WSRelayEvents::All())
	{
		if (Spec.Code == EventCode)
		{
			SelectedEventCode = Spec.Code;
			return;
		}
	}
	SelectedEventCode = WSSprintEvents::Find(EventCode).Code;
}

TArray<FString> AWSSprintGameMode::AllEventCodes()
{
	// The menu offers every running event there is, sprints first, in the
	// order the tables declare them.
	TArray<FString> Codes;
	for (const FWSSprintEventSpec& Spec : WSSprintEvents::All())
	{
		Codes.Add(Spec.Code);
	}
	for (const FWSPaceEventSpec& Spec : WSPaceEvents::All())
	{
		Codes.Add(Spec.Code);
	}
	for (const FWSRelayEventSpec& Spec : WSRelayEvents::All())
	{
		Codes.Add(Spec.Code);
	}
	for (const FWSJumpEventSpec& Spec : WSJumpEvents::All())
	{
		Codes.Add(Spec.Code);
	}
	for (const FWSThrowEventSpec& Spec : WSThrowEvents::All())
	{
		Codes.Add(Spec.Code);
	}
	return Codes;
}

UWSTournamentSubsystem* AWSSprintGameMode::Tournaments() const
{
	return GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWSTournamentSubsystem>()
		: nullptr;
}

void AWSSprintGameMode::EnterTournament()
{
	UWSTournamentSubsystem* T = Tournaments();
	if (!T)
	{
		TournamentStatus = TEXT("Online service unavailable");
		return;
	}
	TournamentStatus = TEXT("Entering…");
	TWeakObjectPtr<AWSSprintGameMode> WeakThis(this);
	T->EnterOrResume(SelectedEventCodeOrDefault(), [WeakThis](bool bOk, const FString& Error)
	{
		if (AWSSprintGameMode* Self = WeakThis.Get())
		{
			Self->TournamentStatus = bOk ? FString() : Error;
		}
	});
}

void AWSSprintGameMode::RaceTournamentRound()
{
	UWSTournamentSubsystem* T = Tournaments();
	if (!T || !T->HasActiveTournament())
	{
		TournamentStatus = TEXT("No tournament in progress");
		return;
	}
	bTournamentRace = true;
	AppState = EWSAppState::Racing;
	bPaused = false;
	StartRace();
}

FString AWSSprintGameMode::GetTournamentSummary() const
{
	const UWSTournamentSubsystem* T = Tournaments();
	if (!T || !T->GetActive().IsValid())
	{
		return TEXT("No tournament yet. Enter one to race a bracket:\n"
			"qualification, heat, semifinal, final.");
	}
	const FWSTournamentDto& Bracket = T->GetActive();

	FString Text = FString::Printf(TEXT("%s   %s\n\n"),
		*Bracket.Event, *Bracket.Status.Replace(TEXT("_"), TEXT(" ")));
	for (const FWSTournamentRound& Round : Bracket.Rounds)
	{
		const bool bCurrent = Round.Round == Bracket.CurrentRound;
		Text += FString::Printf(TEXT("%s%s"),
			bCurrent ? TEXT("> ") : TEXT("  "),
			*Round.Round.Replace(TEXT("_"), TEXT(" ")).ToUpper());
		if (Round.bRun)
		{
			Text += FString::Printf(TEXT("   %d%s   %s\n"),
				Round.Position,
				Round.Position == 1 ? TEXT("st") : Round.Position == 2 ? TEXT("nd")
					: Round.Position == 3 ? TEXT("rd") : TEXT("th"),
				Round.bAdvanced ? TEXT("advanced") : TEXT("eliminated"));
		}
		else
		{
			// The draw is visible BEFORE the race, because the server
			// generated it before the race — showing it afterwards would
			// imply the opponents were chosen to fit the player's time.
			Text += FString::Printf(TEXT("   %d rivals to beat\n"), Round.Field.Num());
			if (bCurrent)
			{
				for (const FWSTournamentRival& Rival : Round.Field)
				{
					Text += FString::Printf(TEXT("      %-16s %s  %.2f\n"),
						*Rival.Name, *Rival.Country, Rival.TimeSeconds);
				}
			}
		}
	}

	if (Bracket.IsComplete())
	{
		Text += Bracket.FinalPosition > 0 && Bracket.FinalPosition <= 3
			? FString::Printf(TEXT("\n%s MEDAL — finished %d%s\n"),
				Bracket.FinalPosition == 1 ? TEXT("GOLD") :
				Bracket.FinalPosition == 2 ? TEXT("SILVER") : TEXT("BRONZE"),
				Bracket.FinalPosition,
				Bracket.FinalPosition == 1 ? TEXT("st") :
				Bracket.FinalPosition == 2 ? TEXT("nd") : TEXT("rd"))
			: FString::Printf(TEXT("\nTournament over — finished %d%s\n"),
				Bracket.FinalPosition,
				Bracket.FinalPosition == 1 ? TEXT("st") : TEXT("th"));
	}
	return Text;
}

void AWSSprintGameMode::SubmitTournamentRound()
{
	UWSTournamentSubsystem* T = Tournaments();
	if (!PlayerRunner || !T)
	{
		return;
	}
	const FWSRaceOutcome Outcome = PlayerRunner->GetOutcome();
	if (!Outcome.bFinished)
	{
		// A false start in a tournament still consumes nothing: the server
		// only advances a round it accepted, so saying so plainly is both
		// accurate and the same rule a free run follows.
		ServerVerdict = TEXT("False start — the round was not run");
		return;
	}

	FWSEventResult Result;
	Result.EventCode = SelectedEventCodeOrDefault();
	Result.ValueNum = Outcome.TimeSeconds;
	// Events without blocks have no reaction to report, and reporting one
	// would be a measurement the sport never took. A relay DOES start from
	// blocks off a gun, so it reports one.
	Result.bHasReactionMs = !IsPaceEvent();
	Result.ReactionMs = Outcome.ReactionMs;
	Result.Splits = Outcome.Splits;
	// No wind is recorded beyond 200m, and none for a relay either, so
	// none is claimed for them.
	Result.bHasWind = !IsPaceEvent() && !IsRelayEvent();
	Result.Wind = Outcome.Wind;
	Result.RngSeed = FString::Printf(TEXT("%u"), RaceSeed);
	Result.InputDigest = IsRelayEvent()
		? FWSRelaySimulation::DigestTrace(PlayerRelayTrace)
		: (IsPaceEvent()
			? FWSMiddleDistanceSimulation::DigestTrace(PlayerPaceTrace)
			: FWSSprintSimulation::DigestTrace(PlayerTrace));

	bAwaitingServer = true;
	SetPhase(EWSEventPhase::Submit);

	TWeakObjectPtr<AWSSprintGameMode> WeakThis(this);
	const uint32 Generation = RaceGeneration;
	T->SubmitRound(Result,
		[WeakThis, Generation](bool bOk, const FWSTournamentResultDto& Response,
			const FString& Error)
		{
			AWSSprintGameMode* Self = WeakThis.Get();
			if (!Self || Self->RaceGeneration != Generation)
			{
				return; // the player already started another race
			}
			Self->bAwaitingServer = false;
			if (!bOk)
			{
				Self->ServerVerdict = Error;
			}
			else if (!Response.accepted)
			{
				Self->ServerVerdict = FString::Printf(
					TEXT("Round not counted: %s"), *Response.rejection_reason);
			}
			else if (Response.tournament_status == TEXT("completed"))
			{
				// Only a completed FINAL has a final position, and only the
				// top three are medals.
				const int32 Place = Response.final_position;
				Self->ServerVerdict = Place >= 1 && Place <= 3
					? FString::Printf(TEXT("%s MEDAL — %d%s in the final · +%d XP"),
						Place == 1 ? TEXT("GOLD") : Place == 2 ? TEXT("SILVER") : TEXT("BRONZE"),
						Place, Place == 1 ? TEXT("st") : Place == 2 ? TEXT("nd") : TEXT("rd"),
						Response.xp_awarded)
					: FString::Printf(TEXT("%d%s in the final · +%d XP"),
						Place, Place == 4 ? TEXT("th") : TEXT("th"), Response.xp_awarded);
			}
			else if (Response.tournament_status == TEXT("eliminated"))
			{
				// An eliminated athlete has NO final position — the server
				// leaves it null. Printing it anyway said "0th in the
				// semifinal", which is not a placing anyone ran.
				Self->ServerVerdict = FString::Printf(
					TEXT("%d%s in the %s — eliminated · +%d XP"),
					Response.position,
					Response.position == 1 ? TEXT("st") : Response.position == 2 ? TEXT("nd")
						: Response.position == 3 ? TEXT("rd") : TEXT("th"),
					*Response.round, Response.xp_awarded);
			}
			else
			{
				Self->ServerVerdict = FString::Printf(
					TEXT("%d%s in the %s — %s · +%d XP"),
					Response.position,
					Response.position == 1 ? TEXT("st") : Response.position == 2 ? TEXT("nd")
						: Response.position == 3 ? TEXT("rd") : TEXT("th"),
					*Response.round,
					Response.advanced ? TEXT("through to the next round") : TEXT("eliminated"),
					Response.xp_awarded);
			}
			Self->SetPhase(EWSEventPhase::Reward);
		});
}

// -- Reaction drill ------------------------------------------------------

void AWSSprintGameMode::StartReactionDrill()
{
	AppState = EWSAppState::Training;
	bDrillArmed = false;
	bDrillToneFired = false;
	DrillResultText.Reset();
	DrillClock = 0.0;
	// Unpredictable wait, like the starter's pause: a fixed delay would be
	// trainable by a metronome rather than by reacting.
	DrillWaitSeconds = 1.2 + FMath::FRand() * 2.6;
}

void AWSSprintGameMode::DrillPress()
{
	if (AppState != EWSAppState::Training || bDrillArmed || bDrillToneFired)
	{
		return;
	}
	bDrillArmed = true;
	DrillClock = -DrillWaitSeconds;
}

void AWSSprintGameMode::DrillRelease()
{
	if (AppState != EWSAppState::Training || !bDrillArmed)
	{
		return;
	}
	bDrillArmed = false;

	if (DrillClock < 0.0)
	{
		// Released before the tone. Reported honestly and NOT submitted:
		// there is no reaction time to claim.
		DrillResultText = TEXT("Too early — wait for the tone.");
		bDrillToneFired = false;
		return;
	}

	const double ReactionMs = DrillClock * 1000.0;
	DrillResultText = FString::Printf(TEXT("%.0f ms — submitting…"), ReactionMs);

	TWeakObjectPtr<AWSSprintGameMode> WeakThis(this);
	if (UWSProgressionSubsystem* P = Progression())
	{
		P->SubmitTraining(TEXT("reaction-drill"), ReactionMs,
			[WeakThis, ReactionMs](bool bOk, const FWSTrainingResponse& Response,
				const FString& Error)
			{
				AWSSprintGameMode* Self = WeakThis.Get();
				if (!Self)
				{
					return;
				}
				if (!bOk)
				{
					Self->DrillResultText = FString::Printf(
						TEXT("%.0f ms — %s"), ReactionMs, *Error);
					return;
				}
				// The server's numbers, verbatim: the gain is never the
				// client's to compute or to round in its own favour.
				Self->DrillResultText = Response.accepted
					? FString::Printf(
						TEXT("%.0f ms · %s +%.2f (now %.1f) · +%d XP · %.2f left today"),
						ReactionMs, *Response.attribute, Response.attribute_gain,
						Response.attribute_after, Response.xp_awarded,
						Response.daily_remaining)
					: FString::Printf(TEXT("%.0f ms — not counted: %s"),
						ReactionMs, *Response.rejection_reason);
			});
	}
}

FString AWSSprintGameMode::GetDrillPrompt() const
{
	if (bDrillArmed)
	{
		return DrillClock < 0.0
			? TEXT("Hold…")
			: TEXT("GO!");
	}
	return TEXT("Press and hold, release the instant you hear the tone");
}

void AWSSprintGameMode::StartFieldAttempt()
{
	// One seed per ATTEMPT, so the three are genuinely different: a jumper
	// meets a different wind and a thrower a differently drifted peak.
	const uint32 AttemptSeed = RaceSeed + AttemptIndex;
	if (IsThrowEvent())
	{
		ThrowSim = MakeShared<FWSThrowSimulation>(
			ResolvePlayerAttributes(), AttemptSeed, CurrentThrowEvent());
		PlayerThrowTrace.Reset();
	}
	else
	{
		// The bar moves as the day goes on, so the attempt runs against a
		// COPY of the event carrying the height currently being attempted.
		ActiveJumpSpec = CurrentJumpEvent();
		ActiveJumpSpec.BarMetres = ActiveJumpSpec.bVertical ? CurrentBar : 0.0;
		if (Track && ActiveJumpSpec.bVertical)
		{
			// Raise the bar in the world to the height being attempted, so
			// what the player is aiming at is a thing they can see rather
			// than a number on the HUD.
			Track->SetHighJumpBar(static_cast<float>(ActiveJumpSpec.RunwayMetres),
				static_cast<float>(CurrentBar));
		}
		JumpSim = MakeShared<FWSJumpSimulation>(
			ResolvePlayerAttributes(), AttemptSeed, ActiveJumpSpec);
		PlayerJumpTrace.Reset();
	}
	RaceClock = -MinSetSeconds;   // a moment to gather before the attempt
	bRaceRunning = true;
	SetPhase(EWSEventPhase::Ready);
}

void AWSSprintGameMode::RecordFieldAttempt(const FWSFieldAttempt& Attempt)
{
	Attempts.Add(Attempt);
	if (!Attempt.bFoul)
	{
		BestMark = FMath::Max(BestMark, Attempt.Metres);
	}

	++AttemptIndex;

	if (Attempt.bVertical)
	{
		// A ladder, not a best-of. Clearing raises the bar and clears the
		// slate; failing three times at one height ends the day, which is
		// why a high jumper's competition has no fixed length.
		if (Attempt.bFoul)
		{
			++FailuresAtHeight;
			if (FailuresAtHeight >= ActiveJumpSpec.FailuresAllowed)
			{
				bRaceRunning = false;
				BuildStandings();
				SetPhase(EWSEventPhase::Finishing);
				return;
			}
		}
		else
		{
			FailuresAtHeight = 0;
			CurrentBar += ActiveJumpSpec.BarIncrementMetres;
		}
		AttemptRestSeconds = 1.6f;
		return;
	}

	if (AttemptIndex < FieldAttemptCount())
	{
		// Another attempt: the series IS the competition, and a foul costs
		// the attempt rather than the whole event.
		AttemptRestSeconds = 1.6f;
		return;
	}

	bRaceRunning = false;
	BuildStandings();
	SetPhase(EWSEventPhase::Finishing);
}

void AWSSprintGameMode::SubmitFieldResult()
{
	// The result of a jumping event is the BEST LEGAL mark of the series,
	// which is how the sport scores it. Three fouls is no mark at all, and
	// there is then nothing honest to send: a zero would be recorded as a
	// jump of zero metres rather than as a competition without a result.
	if (BestMark <= 0.0)
	{
		// The headline above this already says "No height"; repeating it
		// here says the same thing twice and tells the athlete nothing.
		ServerVerdict = IsVerticalEvent()
			? FString::Printf(TEXT("%.2f m was never cleared"), CurrentBar)
			: FString::Printf(TEXT("All %d attempts fouled"), FieldAttemptCount());
		return;
	}

	UWSOnlineSubsystem* Online =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr;
	if (!Online || !Online->IsSignedIn())
	{
		ServerVerdict = TEXT("Sign in to record results and enter leaderboards");
		return;
	}

	// The wind that stood for the best attempt, not the last one.
	double BestWind = 0.0;
	for (const FWSFieldAttempt& Attempt : Attempts)
	{
		if (!Attempt.bFoul && FMath::IsNearlyEqual(Attempt.Metres, BestMark))
		{
			BestWind = Attempt.Wind;
			break;
		}
	}

	FWSEventResult Result;
	Result.EventCode = SelectedEventCodeOrDefault();
	// METRES, not seconds. The server's row says value_kind=distance and
	// that higher is better; nothing here has to know that, because the
	// number is simply the mark.
	Result.ValueNum = BestMark;
	// Nothing to react to in a field event, so no reaction is reported.
	Result.bHasReactionMs = false;
	// A HORIZONTAL jump is a wind-affected mark and the +2.0 limit applies
	// to it. A throw is not, and neither is a high jump or a pole vault:
	// the sport records no wind for any of them, so claiming one would be
	// inventing a measurement.
	Result.bHasWind = FieldResultHasWind();
	Result.Wind = BestWind;
	Result.RngSeed = FString::Printf(TEXT("%u"), RaceSeed);
	Result.InputDigest = IsThrowEvent()
		? FWSThrowSimulation::DigestTrace(PlayerThrowTrace)
		: FWSJumpSimulation::DigestTrace(PlayerJumpTrace);
	Result.ClientRef = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);

	bAwaitingServer = true;
	SetPhase(EWSEventPhase::Submit);

	TWeakObjectPtr<AWSSprintGameMode> WeakThis(this);
	const uint32 Generation = RaceGeneration;
	Online->SubmitResult(Result,
		[WeakThis, Generation](EWSSubmitOutcome SubmitOutcome,
			const FWSResultResponse& Response, const FString& Error)
		{
			if (AWSSprintGameMode* Self = WeakThis.Get())
			{
				// The same verdict path every event uses: one place that
				// turns the server's answer into what the player is told.
				Self->HandleSubmitOutcome(Generation, SubmitOutcome, Response, Error);
			}
		});
}

void AWSSprintGameMode::TickThrow(float DeltaSeconds)
{
	if (AttemptRestSeconds > 0.0f)
	{
		AttemptRestSeconds -= DeltaSeconds;
		if (AttemptRestSeconds <= 0.0f)
		{
			AttemptRestSeconds = 0.0f;
			StartFieldAttempt();
		}
		return;
	}
	if (!ThrowSim.IsValid())
	{
		return;
	}

	RaceClock += DeltaSeconds;
	if (GetPhase() == EWSEventPhase::Ready && RaceClock >= 0.0)
	{
		SetPhase(EWSEventPhase::Active);
	}
	if (RaceClock < 0.0)
	{
		return; // still gathering in the circle
	}

	// Fixed-step catch-up, as every event uses: a stutter cannot lengthen
	// a throw, because the simulation never sees a frame delta.
	int32 Steps = 0;
	while (ThrowSim->GetState().RaceTime < RaceClock && Steps < 128)
	{
		if (!ThrowSim->Step())
		{
			break;
		}
		++Steps;
	}

	if (PlayerRunner)
	{
		// The thrower stays in the circle: the shot travels, not the
		// athlete, so there is no distance to drive the visual with.
		PlayerRunner->DriveVisual(3, TEXT("You"), 0.0, 0.0, /*bAirborne=*/false);
	}

	if (ThrowSim->GetState().bFinished)
	{
		const FWSThrowOutcome Outcome = ThrowSim->GetOutcome();
		FWSFieldAttempt Attempt;
		Attempt.Metres = Outcome.DistanceMetres;
		Attempt.bFoul = Outcome.bFoul;
		Attempt.Wind = 0.0; // no wind is recorded for throws
		// A javelin thrower who never lets go has crossed the ARC. They
		// were never standing in a circle to be carried out of.
		Attempt.FoulReason = Outcome.bCarriedOut
			? (CurrentThrowEvent().bFromCircle
				? TEXT("carried out of the circle")
				: TEXT("over the arc"))
			: TEXT("no mark");
		RecordFieldAttempt(Attempt);
	}
}

void AWSSprintGameMode::TickJump(float DeltaSeconds)
{
	if (AttemptRestSeconds > 0.0f)
	{
		AttemptRestSeconds -= DeltaSeconds;
		if (AttemptRestSeconds <= 0.0f)
		{
			AttemptRestSeconds = 0.0f;
			StartFieldAttempt();
		}
		return;
	}
	if (!JumpSim.IsValid())
	{
		return;
	}

	RaceClock += DeltaSeconds;
	if (GetPhase() == EWSEventPhase::Ready && RaceClock >= 0.0)
	{
		SetPhase(EWSEventPhase::Active);
	}
	if (RaceClock < 0.0)
	{
		return; // still gathering at the top of the runway
	}

	// Fixed-step catch-up, exactly as the races do: the simulation never
	// sees a frame delta, so a stutter cannot lengthen a jump.
	int32 Steps = 0;
	while (JumpSim->GetState().RaceTime < RaceClock && Steps < 128)
	{
		if (!JumpSim->Step())
		{
			break;
		}
		++Steps;
	}

	const FWSJumpState& State = JumpSim->GetState();
	if (PlayerRunner)
	{
		PlayerRunner->DriveVisual(3, TEXT("You"),
			State.Distance, State.Speed, State.bAirborne, State.HeightAboveGround);
	}

	if (State.bFinished)
	{
		FWSFieldAttempt Attempt;
		const FWSJumpOutcome Outcome = JumpSim->GetOutcome();
		Attempt.Metres = Outcome.DistanceMetres;
		Attempt.bFoul = Outcome.bFoul;
		Attempt.Wind = Outcome.Wind;
		Attempt.bVertical = ActiveJumpSpec.bVertical;
		Attempt.BarMetres = ActiveJumpSpec.BarMetres;
		Attempt.FoulReason = ActiveJumpSpec.bVertical
			// A high jumper who misses has not fouled — they failed to
			// clear, and that is a different word for a different thing.
			? FString::Printf(TEXT("%.2f m not cleared"), ActiveJumpSpec.BarMetres)
			: (Outcome.bOverstepped ? TEXT("over the board") : TEXT("short of the pit"));
		RecordFieldAttempt(Attempt);
	}
}

void AWSSprintGameMode::StartRace()
{
	// The finish line and the infield markers belong to THIS event.
	if (Track)
	{
		Track->SetRaceDistance(
			static_cast<float>(SelectedDistanceMetres()), SelectedSplitCount());
		// A jumping event dresses the straight as a runway; anything else
		// puts the board and pit away.
		Track->SetJumpPit(
			IsJumpEvent() ? static_cast<float>(CurrentJumpEvent().RunwayMetres) : 0.0f,
			// A vertical jumper lands on a mat, so there is no pit to lay.
			// Otherwise the sand runs as far as the event can reach: a ten
			// metre pit is right for a long jump and eight metres short of
			// where a triple jumper comes down.
			IsVerticalEvent() ? 0.0f
				: static_cast<float>(CurrentJumpEvent().MaxPlausibleMetres + 1.0));
		// The crossbar starts the competition at the opening height.
		Track->SetHighJumpBar(
			IsJumpEvent() ? static_cast<float>(CurrentJumpEvent().RunwayMetres) : 0.0f,
			IsVerticalEvent() ? static_cast<float>(CurrentBar) : 0.0f);
		// The throwing circle — or, for a javelin, the arc it is thrown
		// over, because a javelin thrower never stands in a circle.
		Track->SetThrowCircle(IsThrowEvent(),
			!IsThrowEvent() || CurrentThrowEvent().bFromCircle);
		// The takeover zones belong to the relays, and a flat race takes
		// them away.
		Track->SetTakeoverZones(
			IsRelayEvent() ? CurrentRelayEvent().LegCount : 0,
			IsRelayEvent() ? static_cast<float>(CurrentRelayEvent().LegMetres) : 0.0f,
			IsRelayEvent() ? static_cast<float>(CurrentRelayEvent().TakeoverZoneMetres) : 0.0f);
		// Barriers belong to the event, so a flat race takes them away and a
		// hurdles race stands exactly its own up.
		const bool bHurdles = !IsPaceEvent() && CurrentEvent().HasHurdles();
		Track->SetHurdles(
			bHurdles ? CurrentEvent().HurdleCount : 0,
			bHurdles ? static_cast<float>(CurrentEvent().FirstHurdleMetres) : 0.0f,
			bHurdles ? static_cast<float>(CurrentEvent().HurdleSpacingMetres) : 0.0f);
	}

	for (AWSSprintRunner* Runner : Runners)
	{
		if (Runner)
		{
			Runner->Destroy();
		}
	}
	Runners.Reset();
	PlayerRunner = nullptr;
	Standings.Reset();
	PlayerTrace.Reset();
	PlayerPaceTrace.Reset();
	PlayerEffort = 0.0;
	ServerVerdict.Reset();
	ReplayFrames.Reset();
	ReplayCursor = INDEX_NONE;
	bPlayedMarks = false;
	bPlayedSet = false;
	bPlayedGun = false;
	bPlayedFinish = false;
	NextFootfallDistance = 0.0;
	bAwaitingServer = false;
	bHolding = false;
	ResultDwell = 0.0f;

	++RaceGeneration;

	// One seed per race, shared by every runner: same wind, same drift.
	RaceSeed = static_cast<uint32>(FMath::Rand()) ^ 0x5F3759DFu;

	// The starter's pause varies, as it does on a real track. A fixed delay
	// would let a constant-offset macro score a perfect reaction every race,
	// which would make the start mechanic decorative.
	FRandomStream StartStream(static_cast<int32>(RaceSeed ^ 0x7F4A7C15u));
	SetDurationSeconds = MinSetSeconds +
		StartStream.FRand() * (MaxSetSeconds - MinSetSeconds);
	// Independently drawn: a fixed set-to-gun interval would let a macro
	// time the start off the "set" call and ignore the randomised pause.
	SetCallOffsetSeconds = FMath::Min(
		0.85 + StartStream.FRand() * 1.45, SetDurationSeconds - 0.5);
	RaceClock = -SetDurationSeconds;

	TournamentField.Reset();
	if (bTournamentRace)
	{
		if (const UWSTournamentSubsystem* T = Tournaments())
		{
			TournamentField = T->GetCurrentField();
		}
	}

	if (IsFieldEvent())
	{
		// A field event is not a race: one athlete, a series of attempts,
		// and a mark in metres. There is no eight-lane field to spawn and
		// no gun to react to.
		Attempts.Reset();
		AttemptIndex = 0;
		BestMark = 0.0;
		AttemptRestSeconds = 0.0f;
		FailuresAtHeight = 0;
		CurrentBar = IsVerticalEvent() ? CurrentJumpEvent().StartBarMetres : 0.0;

		AWSSprintRunner* Runner = GetWorld()->SpawnActor<AWSSprintRunner>(
			AWSSprintRunner::StaticClass(), FTransform::Identity);
		if (Runner)
		{
			Runner->DriveVisual(3, TEXT("You"), 0.0, 0.0, /*bAirborne=*/false);
			Runners.Add(Runner);
			PlayerRunner = Runner;
		}
		StartFieldAttempt();
		return;
	}

	SpawnField();
	bRaceRunning = true;
	SetPhase(EWSEventPhase::Ready);
}

void AWSSprintGameMode::SpawnField()
{
	const TArray<FWSSprintDifficultyLevel>& Levels = WSSprintDifficulty::Levels();

	// Player in lane 4 (index 3) — the broadcast lane, and it keeps the
	// tracking camera centred with rivals visible on both sides.
	constexpr int32 PlayerLane = 3;
	int32 OpponentIndex = 0;

	for (int32 Lane = 0; Lane < AWSSprintTrack::LaneCount; ++Lane)
	{
		AWSSprintRunner* Runner = GetWorld()->SpawnActor<AWSSprintRunner>(
			AWSSprintRunner::StaticClass(), FTransform::Identity);
		if (!Runner)
		{
			continue;
		}
		if (Lane == PlayerLane)
		{
			if (IsRelayEvent())
			{
				Runner->InitializeRelayRace(ResolvePlayerAttributes(), RaceSeed, Lane,
					TEXT("You"), /*bIsPlayer=*/true, CurrentRelayEvent());
				PlayerRelayTrace.Reset();
			}
			else if (IsPaceEvent())
			{
				Runner->InitializePaceRace(ResolvePlayerAttributes(), RaceSeed, Lane,
					TEXT("You"), /*bIsPlayer=*/true, CurrentPaceEvent());
				// A paced race opens at a settled effort rather than at a
				// standstill; the player then decides what to do with it.
				PlayerEffort = 0.55;
				const FWSPaceInputEvent Opening{0.0, EWSPaceInputType::SetEffort, PlayerEffort};
				PlayerPaceTrace.Add(Opening);
				Runner->PushPaceInput(Opening);
			}
			else
			{
				Runner->InitializeRace(ResolvePlayerAttributes(), RaceSeed, Lane,
					TEXT("You"), /*bIsPlayer=*/true, CurrentEvent());
			}
			PlayerRunner = Runner;
		}
		else
		{
			// Spread the field across tiers so a race has a plausible
			// spectrum rather than seven clones.
			const FWSSprintDifficultyLevel& Level =
				Levels[OpponentIndex % Levels.Num()];
			const FWSSprintAttributes Attributes = Level.MakeAttributes();
			// Distinct per-opponent seed for their INPUT only; the race seed
			// still governs shared conditions.
			const uint32 InputSeed = RaceSeed + 1013u * (OpponentIndex + 1);
			const FString RivalName =
				OpponentNames[OpponentIndex % UE_ARRAY_COUNT(OpponentNames)];
			if (IsRelayEvent())
			{
				Runner->InitializeRelayRace(Attributes, RaceSeed, Lane, RivalName,
					/*bIsPlayer=*/false, CurrentRelayEvent());
				// The rival team runs the SAME simulation from a trace of
				// its own — never a scripted finish. What a difficulty tier
				// changes is how well they hold the cadence and judge the
				// handovers, which is what it changes in every other event.
				Runner->PushRelayTrace(FWSRelaySimulation::GenerateAITrace(
					Attributes, RaceSeed, InputSeed, Level.ReactionMeanMs,
					Level.ReactionSpreadMs, Level.Consistency, CurrentRelayEvent()));
			}
			else if (IsPaceEvent())
			{
				Runner->InitializePaceRace(Attributes, RaceSeed, Lane, RivalName,
					/*bIsPlayer=*/false, CurrentPaceEvent());
				// The rival runs the SAME simulation from a pace plan of its
				// own — never a scripted finish time. Consistency is what a
				// difficulty tier is allowed to change, here as in the sprint.
				Runner->PushPaceTrace(FWSMiddleDistanceSimulation::GenerateAITrace(
					Attributes, RaceSeed, InputSeed, Level.Consistency,
					CurrentPaceEvent()));
			}
			else
			{
				Runner->InitializeRace(Attributes, RaceSeed, Lane, RivalName,
					/*bIsPlayer=*/false, CurrentEvent());
				Runner->PushTrace(FWSSprintSimulation::GenerateAITrace(
					Attributes, RaceSeed, InputSeed, Level.ReactionMeanMs,
					Level.ReactionSpreadMs, Level.Consistency, CurrentEvent()));
			}
			++OpponentIndex;
		}
		Runners.Add(Runner);
	}
}

FWSSprintAttributes AWSSprintGameMode::ResolvePlayerAttributes() const
{
	// Defaults match a fresh career athlete (the backend seeds every
	// attribute at 40), so an unsigned-in race is submittable as-is and the
	// ceiling the player feels is the ceiling the server enforces.
	FWSSprintAttributes Attributes;
	Attributes.Reaction = 40.0f;
	Attributes.Acceleration = 40.0f;
	Attributes.MaxSpeed = 40.0f;
	Attributes.StrideEfficiency = 40.0f;
	Attributes.Stamina = 40.0f;
	Attributes.Recovery = 40.0f;
	Attributes.Technique = 40.0f;

	// Signed in with a career athlete: race with the SERVER's attributes.
	// Simulating against numbers the server did not issue is how an honest
	// run comes back rejected — the client would be racing to a ceiling the
	// validator has never heard of.
	const UWSProgressionSubsystem* Progression =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSProgressionSubsystem>() : nullptr;
	if (!Progression || !Progression->HasCareerAthlete())
	{
		return Attributes;
	}

	const TMap<FString, float>& Server = Progression->GetCareerAthlete().attributes;
	auto Read = [&Server](const TCHAR* Key, float& Out)
	{
		if (const float* Found = Server.Find(Key))
		{
			Out = *Found;
		}
	};
	// Keys are the backend's ATTRIBUTE_KEYS. A key the server did not send
	// keeps its default rather than silently becoming zero.
	Read(TEXT("reaction"), Attributes.Reaction);
	Read(TEXT("acceleration"), Attributes.Acceleration);
	Read(TEXT("max_speed"), Attributes.MaxSpeed);
	Read(TEXT("stride_efficiency"), Attributes.StrideEfficiency);
	Read(TEXT("stamina"), Attributes.Stamina);
	// recovery governs the 400m on the SERVER, so it must reach the client
	// simulation or the two disagree about that event's ceiling.
	Read(TEXT("recovery"), Attributes.Recovery);
	Read(TEXT("technique"), Attributes.Technique);
	return Attributes;
}

void AWSSprintGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsThrowEvent() && bRaceRunning && !bPaused)
	{
		TickThrow(DeltaSeconds);
	}
	else if (IsJumpEvent() && bRaceRunning && !bPaused)
	{
		TickJump(DeltaSeconds);
	}
	else if (bRaceRunning && !bPaused)
	{
		RaceClock += DeltaSeconds;

		if (GetPhase() == EWSEventPhase::Ready && RaceClock >= 0.0)
		{
			SetPhase(EWSEventPhase::Active); // the gun
		}

		bool bAllDone = true;
		for (AWSSprintRunner* Runner : Runners)
		{
			Runner->AdvanceTo(RaceClock);
			if (!Runner->HasFinished() && !Runner->HasFalseStarted())
			{
				bAllDone = false;
			}
		}

		RecordReplayFrame();

		// A false start stops the race immediately, exactly as it would on
		// a real track — no "keep running and see".
		if (PlayerRunner && PlayerRunner->HasFalseStarted())
		{
			bRaceRunning = false;
			BuildStandings();
			SetPhase(EWSEventPhase::Finishing);
		}
		else if (bAllDone)
		{
			bRaceRunning = false;
			BuildStandings();
			SetPhase(EWSEventPhase::Finishing);
		}
	}
	else if (GetPhase() == EWSEventPhase::Finishing)
	{
		// Let the finish read on screen before the result panel takes over.
		ResultDwell += DeltaSeconds;
		if (ResultDwell >= ResultDwellSeconds)
		{
			SetPhase(EWSEventPhase::Result);
		}
	}

	if (AWSGameStateBase* State = WSGameState())
	{
		State->SetEventClockSeconds(static_cast<float>(FMath::Max(RaceClock, 0.0)));
	}
	// The drill runs outside a race, so its clock ticks here rather than in
	// the race branch above.
	if (AppState == EWSAppState::Training && bDrillArmed)
	{
		const double Previous = DrillClock;
		DrillClock += DeltaSeconds;
		if (Previous < 0.0 && DrillClock >= 0.0 && !bDrillToneFired)
		{
			bDrillToneFired = true;
			// The tone IS the stimulus being measured, so it fires from the
			// clock the reaction is measured against.
			WSSprintAudio::Play(GetWorld(), GunSound);
		}
	}

	TickAudio(DeltaSeconds);
	TickReplay(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
}

bool AWSSprintGameMode::CanLeavePhase(EWSEventPhase Phase) const
{
	// Active only ends when the race actually ends; Submit only when the
	// server (or the offline queue) has answered.
	if (Phase == EWSEventPhase::Active)
	{
		return !bRaceRunning;
	}
	if (Phase == EWSEventPhase::Submit)
	{
		return !bAwaitingServer;
	}
	return true;
}

void AWSSprintGameMode::OnPhaseEntered(EWSEventPhase NewPhase)
{
	Super::OnPhaseEntered(NewPhase);
	if (NewPhase != EWSEventPhase::Result)
	{
		return;
	}
	// A tournament round is scored against the field the server stored
	// before the race, so it goes to the bracket endpoint — never to the
	// free-run path, which would award XP for a round nobody ran.
	if (bTournamentRace)
	{
		SubmitTournamentRound();
	}
	else
	{
		SubmitPlayerResult();
	}
}

void AWSSprintGameMode::UpdateCamera(float DeltaSeconds)
{
	if (!RaceCamera || !PlayerRunner)
	{
		return;
	}
	const FWSRaceState& State = PlayerRunner->GetState();
	const float RunnerX = static_cast<float>(State.Distance) * 100.0f;
	const float LaneY = PlayerRunner->GetActorLocation().Y;

	FVector Desired;
	FRotator DesiredRotation;
	switch (GetPhase())
	{
	case EWSEventPhase::Ready:
		// Low and close on the blocks: tension before the gun.
		Desired = FVector(RunnerX - 420.0f, LaneY - 260.0f, 145.0f);
		DesiredRotation = FRotator(-6.0f, 28.0f, 0.0f);
		break;
	case EWSEventPhase::Finishing:
	case EWSEventPhase::Result:
	case EWSEventPhase::Submit:
	case EWSEventPhase::Reward:
		// Square onto the line from the side — where a photo finish reads.
		// The line is where THIS event finishes, not where the 100m did.
		Desired = FVector(
			static_cast<float>(SelectedDistanceMetres()) * 100.0f + 260.0f,
			LaneY - 900.0f, 320.0f);
		DesiredRotation = FRotator(-9.0f, 108.0f, 0.0f);
		break;
	default:
		// Tracking chase, drifting back as speed rises so the sense of
		// pace grows through the race.
		Desired = FVector(RunnerX - 620.0f - static_cast<float>(State.Speed) * 22.0f,
			LaneY - 420.0f, 235.0f);
		DesiredRotation = FRotator(-7.0f, 24.0f, 0.0f);
		break;
	}

	const float Blend = FMath::Clamp(DeltaSeconds * 4.5f, 0.0f, 1.0f);
	RaceCamera->SetActorLocation(
		FMath::Lerp(RaceCamera->GetActorLocation(), Desired, Blend));
	RaceCamera->SetActorRotation(
		FMath::Lerp(RaceCamera->GetActorRotation(), DesiredRotation, Blend));
}

void AWSSprintGameMode::InitAudio()
{
	// Only a race with a real player has audio. A headless race (automation,
	// future server-side replay validation) must run identically without it,
	// so the cues are never even created there.
	if (!GEngine || !GEngine->UseSound())
	{
		return;
	}
	// Generated once and reused; each cue is a few KB of PCM.
	GunSound = WSSprintAudio::MakeGunshot();
	MarksSound = WSSprintAudio::MakeTone(330.0f, 0.35f);
	SetSound = WSSprintAudio::MakeTone(494.0f, 0.30f);
	FootfallSound = WSSprintAudio::MakeFootfall();
	FinishSound = WSSprintAudio::MakeFinishChime();
}

void AWSSprintGameMode::TickAudio(float DeltaSeconds)
{
	if (!PlayerRunner)
	{
		return;
	}
	// Checked before the bRaceRunning gate: when the player is the LAST to
	// cross, the same tick that finishes them ends the race, so a gated
	// check would never see the finish.
	if (!bPlayedFinish && PlayerRunner->GetState().bFinished)
	{
		bPlayedFinish = true;
		WSSprintAudio::Play(GetWorld(), FinishSound);
	}
	if (!bRaceRunning || bPaused)
	{
		return;
	}
	// The calls and the gun are the timing signal a player reacts to, so
	// they fire from the race clock, not from an animation or a widget.
	if (!bPlayedMarks && RaceClock >= -SetDurationSeconds + 0.35)
	{
		bPlayedMarks = true;
		WSSprintAudio::Play(GetWorld(), MarksSound);
	}
	if (!bPlayedSet && RaceClock >= -SetCallOffsetSeconds)
	{
		bPlayedSet = true;
		WSSprintAudio::Play(GetWorld(), SetSound);
	}
	if (!bPlayedGun && RaceClock >= 0.0)
	{
		bPlayedGun = true;
		WSSprintAudio::Play(GetWorld(), GunSound, 1.0f);
	}

	// Footfalls track the athlete's actual stride, so the sound IS the
	// rhythm the player is trying to match — an audio version of the band
	// for players who cannot rely on the visual one.
	const FWSRaceState& State = PlayerRunner->GetState();
	if (State.bReleased && !State.bFinished && State.Distance >= NextFootfallDistance)
	{
		const double StrideMetres = FMath::Max(State.Speed / FMath::Max(State.TargetCadenceHz, 0.5), 0.8);
		NextFootfallDistance = State.Distance + StrideMetres;
		WSSprintAudio::Play(GetWorld(), FootfallSound,
			0.5f + 0.5f * static_cast<float>(State.CadenceAccuracy));
	}
}

void AWSSprintGameMode::RecordReplayFrame()
{
	// A rolling window of the closing seconds; enough for the finish, and
	// bounded so a long race cannot grow it without limit.
	constexpr double ReplayWindowSeconds = 4.0;

	FReplayFrame Frame;
	Frame.Clock = RaceClock;
	Frame.Positions.Reserve(Runners.Num());
	for (const AWSSprintRunner* Runner : Runners)
	{
		Frame.Positions.Add(Runner->GetActorLocation());
	}
	ReplayFrames.Add(MoveTemp(Frame));

	int32 Drop = 0;
	while (Drop < ReplayFrames.Num() &&
		RaceClock - ReplayFrames[Drop].Clock > ReplayWindowSeconds)
	{
		++Drop;
	}
	if (Drop > 0)
	{
		ReplayFrames.RemoveAt(0, Drop);
	}
}

void AWSSprintGameMode::PlayFinishReplay()
{
	if (ReplayFrames.Num() < 2)
	{
		return;
	}
	ReplayCursor = 0;
	ReplayTime = static_cast<float>(ReplayFrames[0].Clock);
}

void AWSSprintGameMode::TickReplay(float DeltaSeconds)
{
	if (ReplayCursor == INDEX_NONE || ReplayFrames.Num() < 2)
	{
		return;
	}
	// Half speed: the whole point of a finish replay is to see the order
	// that the live camera swept past.
	ReplayTime += DeltaSeconds * 0.5f;

	while (ReplayCursor < ReplayFrames.Num() - 1 &&
		ReplayFrames[ReplayCursor + 1].Clock <= ReplayTime)
	{
		++ReplayCursor;
	}
	if (ReplayCursor >= ReplayFrames.Num() - 1)
	{
		ReplayCursor = INDEX_NONE; // finished; runners stay where they ended
		return;
	}

	const FReplayFrame& From = ReplayFrames[ReplayCursor];
	const FReplayFrame& To = ReplayFrames[ReplayCursor + 1];
	const double Span = FMath::Max(To.Clock - From.Clock, KINDA_SMALL_NUMBER);
	const float Alpha = FMath::Clamp(
		static_cast<float>((ReplayTime - From.Clock) / Span), 0.0f, 1.0f);

	for (int32 Index = 0; Index < Runners.Num(); ++Index)
	{
		if (From.Positions.IsValidIndex(Index) && To.Positions.IsValidIndex(Index))
		{
			Runners[Index]->SetActorLocation(
				FMath::Lerp(From.Positions[Index], To.Positions[Index], Alpha));
		}
	}
}

void AWSSprintGameMode::BuildStandings()
{
	if (IsFieldEvent())
	{
		// A field event has no finishing order to build: the result is a
		// series of marks, and the best legal one is what counts. The
		// result screen reads the series directly.
		Standings.Reset();
		return;
	}
	Standings.Reset();

	// A false start recalls the race: nobody else has a result, and listing
	// seven athletes still standing in their blocks as a finished field
	// would be a fabricated classification.
	if (PlayerRunner && PlayerRunner->HasFalseStarted())
	{
		FWSRaceStanding Dq;
		Dq.Position = 0;
		Dq.Name = PlayerRunner->GetDisplayName();
		Dq.bIsPlayer = true;
		Dq.bFalseStart = true;
		Standings.Add(Dq);
		return;
	}

	TArray<AWSSprintRunner*> Sorted;
	for (AWSSprintRunner* Runner : Runners)
	{
		Sorted.Add(Runner);
	}
	// Finishers by time; false starts last — they have no time at all.
	Sorted.Sort([](const AWSSprintRunner& A, const AWSSprintRunner& B)
	{
		const FWSRaceOutcome OutA = A.GetOutcome();
		const FWSRaceOutcome OutB = B.GetOutcome();
		if (OutA.bFinished != OutB.bFinished)
		{
			return OutA.bFinished;
		}
		return OutA.TimeSeconds < OutB.TimeSeconds;
	});

	int32 Position = 1;
	for (const AWSSprintRunner* Runner : Sorted)
	{
		const FWSRaceOutcome Outcome = Runner->GetOutcome();
		FWSRaceStanding Standing;
		Standing.Position = Outcome.bFinished ? Position++ : 0;
		Standing.Name = Runner->GetDisplayName();
		Standing.TimeSeconds = Outcome.TimeSeconds;
		Standing.ReactionMs = Outcome.ReactionMs;
		Standing.bIsPlayer = Runner->IsPlayer();
		Standing.bFalseStart = Outcome.bFalseStart;
		Standings.Add(Standing);
	}
}

int32 AWSSprintGameMode::GetPlayerPosition() const
{
	for (const FWSRaceStanding& Standing : Standings)
	{
		if (Standing.bIsPlayer)
		{
			return Standing.Position;
		}
	}
	return 0;
}

void AWSSprintGameMode::SubmitPlayerResult()
{
	if (IsFieldEvent())
	{
		SubmitFieldResult();
		return;
	}
	if (!PlayerRunner)
	{
		return;
	}
	const FWSRaceOutcome Outcome = PlayerRunner->GetOutcome();
	if (!Outcome.bFinished)
	{
		// Neither a false start nor a dropped baton has a time to submit,
		// and they are DIFFERENT disqualifications. Calling a missed
		// handover a false start told a team that had started cleanly they
		// had jumped the gun.
		ServerVerdict = Outcome.bBadExchange
			? TEXT("The baton left the takeover zone — no time to submit")
			: TEXT("False start — no time to submit");
		return;
	}

	UWSOnlineSubsystem* Online =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr;
	if (!Online || !Online->IsSignedIn())
	{
		ServerVerdict = TEXT("Sign in to record results and enter leaderboards");
		return;
	}

	FWSEventResult Result;
	Result.EventCode = SelectedEventCodeOrDefault();
	Result.ValueNum = Outcome.TimeSeconds;
	// Events without blocks have no reaction to report, and reporting one
	// would be a measurement the sport never took. A relay DOES start from
	// blocks off a gun, so it reports one.
	Result.bHasReactionMs = !IsPaceEvent();
	Result.ReactionMs = Outcome.ReactionMs;
	Result.Splits = Outcome.Splits;
	// No wind is recorded beyond 200m, and none for a relay either, so
	// none is claimed for them.
	Result.bHasWind = !IsPaceEvent() && !IsRelayEvent();
	Result.Wind = Outcome.Wind;
	Result.RngSeed = FString::Printf(TEXT("%u"), RaceSeed);
	Result.InputDigest = IsRelayEvent()
		? FWSRelaySimulation::DigestTrace(PlayerRelayTrace)
		: (IsPaceEvent()
			? FWSMiddleDistanceSimulation::DigestTrace(PlayerPaceTrace)
			: FWSSprintSimulation::DigestTrace(PlayerTrace));

	bAwaitingServer = true;
	SetPhase(EWSEventPhase::Submit);

	TWeakObjectPtr<AWSSprintGameMode> WeakThis(this);
	const uint32 Generation = RaceGeneration;
	Online->SubmitResult(Result,
		[WeakThis, Generation](EWSSubmitOutcome SubmitOutcome, const FWSResultResponse& Response,
			const FString& Error)
		{
			if (AWSSprintGameMode* Self = WeakThis.Get())
			{
				Self->HandleSubmitOutcome(Generation, SubmitOutcome, Response, Error);
			}
		});
}

void AWSSprintGameMode::HandleSubmitOutcome(uint32 Generation, EWSSubmitOutcome Outcome,
	const FWSResultResponse& Response, const FString& Error)
{
	if (RaceGeneration != Generation)
	{
		// The player already started another race. Applying this answer
		// would stamp the previous run's verdict on the new one and shove
		// it straight into Reward, leaving it unplayable. Nothing is lost:
		// the online subsystem still broadcasts the result.
		return;
	}
	bAwaitingServer = false;
	switch (Outcome)
	{
	case EWSSubmitOutcome::Accepted:
		ServerVerdict = FString::Printf(
			TEXT("Verified · +%d XP%s"), Response.xp_awarded,
			Response.is_personal_best ? TEXT(" · Personal best!") : TEXT(""));
		break;
	case EWSSubmitOutcome::Rejected:
		// The server's exact reason, verbatim: a rejected run must never
		// look like a network problem.
		ServerVerdict = FString::Printf(
			TEXT("Not counted: %s"), *Response.rejection_reason);
		break;
	case EWSSubmitOutcome::Queued:
		ServerVerdict = TEXT("Saved — will submit when you're back online");
		break;
	default:
		ServerVerdict = FString::Printf(TEXT("Submission failed: %s"), *Error);
		break;
	}
	SetPhase(EWSEventPhase::Reward);
}

void AWSSprintGameMode::WSStatus()
{
	const FWSRaceState* State = PlayerRunner ? &PlayerRunner->GetState() : nullptr;
	UE_LOG(LogWorldSports, Display,
		TEXT("WSStatus app=%d phase=%d clock=%.2f paused=%d signedIn=%d "
			 "dist=%.1f speed=%.2f board=%d/%s verdict='%s'"),
		static_cast<int32>(AppState), static_cast<int32>(GetPhase()), RaceClock,
		bPaused ? 1 : 0, IsSignedIn() ? 1 : 0,
		State ? State->Distance : 0.0, State ? State->Speed : 0.0,
		LeaderboardRows.Num(), *LeaderboardStatus, *ServerVerdict);

	// A field event's live state is nowhere in the line above: none of
	// distance, speed or a leaderboard says where the mark is, how far the
	// board is, or which try at which height this is. Driving a sub-second
	// takeoff window through adb is guesswork without them.
	if (IsFieldEvent())
	{
		const FWSJumpState* Jump = JumpSim.IsValid() ? &JumpSim->GetState() : nullptr;
		// A ladder has no fixed number of attempts, so reporting "4 of 3"
		// would be the same lie the HUD used to tell.
		UE_LOG(LogWorldSports, Display,
			TEXT("WSField event=%s attempt=%d/%s best=%.2f bar=%.2f fails=%d "
				 "board=%.2f airborne=%d phase=%d phaseT=%.3f window=%d power=%.2f"),
			*SelectedEventCodeOrDefault(), AttemptIndex + 1,
			IsVerticalEvent() ? TEXT("ladder")
				: *FString::FromInt(FieldAttemptCount()),
			BestMark, CurrentBar, FailuresAtHeight,
			Jump ? Jump->MetresToBoard : 0.0,
			Jump && Jump->bAirborne ? 1 : 0,
			Jump ? Jump->Phase : 0,
			Jump ? Jump->PhaseTimeRemaining : 0.0,
			Jump && Jump->bPhaseWindowOpen ? 1 : 0,
			ThrowSim.IsValid() ? ThrowSim->GetState().Power : 0.0);
	}
}

void AWSSprintGameMode::DebugDeliverStaleSubmit()
{
	FWSResultResponse Response;
	Response.accepted = true;
	Response.xp_awarded = 999;
	HandleSubmitOutcome(RaceGeneration - 1, EWSSubmitOutcome::Accepted, Response, FString());
}

// -- Player input -------------------------------------------------------

void AWSSprintGameMode::PlayerPress()
{
	// Paused input is discarded, not deferred: lifting the finger while
	// paused used to push a pre-gun Release and disqualify the player, and
	// taps landing at the frozen clock polluted the submitted input trace.
	if (!PlayerRunner || !bRaceRunning || bPaused)
	{
		return;
	}
	if (IsThrowEvent())
	{
		// A throw has no rhythm to keep: the wind-up runs on its own and
		// the only decision is when to let go. Tapping does nothing, which
		// is honest — there is nothing for a tap to mean here.
		return;
	}
	if (IsJumpEvent())
	{
		// The approach IS a sprint, so the same rhythm skill drives it.
		if (JumpSim.IsValid() && RaceClock >= 0.0)
		{
			const FWSJumpInputEvent Tap{RaceClock, EWSJumpInputType::Tap};
			PlayerJumpTrace.Add(Tap);
			JumpSim->AddInput(Tap);
		}
		return;
	}
	if (IsPaceEvent())
	{
		// Holding asks for more effort. There are no blocks to leave and no
		// rhythm to match: the decision is how hard to run, and when.
		PushPlayerEffort(FMath::Min(1.0, PlayerEffort + 0.12));
		return;
	}
	if (IsRelayEvent())
	{
		// Blocks then rhythm, exactly as a sprint: a relay leg IS a sprint,
		// and the baton is a separate press.
		if (!PlayerRunner->GetState().bReleased)
		{
			if (bHolding)
			{
				return;
			}
			bHolding = true;
			return; // the hold itself is not an input the relay models
		}
		const FWSRelayInputEvent Tap{RaceClock, EWSRelayInputType::Tap};
		PlayerRelayTrace.Add(Tap);
		PlayerRunner->PushRelayInput(Tap);
		return;
	}

	// Still in the blocks — whatever the phase — so this press is the hold.
	// Gating this on the Ready phase used to strand any player whose first
	// touch landed after the gun: they could never release.
	if (!PlayerRunner->GetState().bReleased)
	{
		if (bHolding)
		{
			return;
		}
		bHolding = true;
		const FWSSprintInputEvent Event{RaceClock, EWSSprintInputType::HoldStart};
		PlayerTrace.Add(Event);
		PlayerRunner->PushInput(Event);
		return;
	}

	const FWSSprintInputEvent Event{RaceClock, EWSSprintInputType::Tap};
	PlayerTrace.Add(Event);
	PlayerRunner->PushInput(Event);
}

void AWSSprintGameMode::PlayerPass()
{
	// The one decision a relay turns on. Outside the takeover zone it is a
	// disqualification, and the simulation is what decides that — this only
	// delivers the press.
	if (!PlayerRunner || !PlayerRunner->IsRelayEvent() || !bRaceRunning || bPaused)
	{
		return;
	}
	if (RaceClock < 0.0)
	{
		return; // the gun has not gone
	}
	// RaceClock, exactly as the taps and the release use — NOT the
	// simulation's own clock. The trace is hashed into InputDigest and
	// submitted as the run's audit breadcrumb; a trace whose passes come
	// from a different clock than its taps cannot be replayed to reproduce
	// the handover, which is the only thing that digest is for.
	const FWSRelayInputEvent Pass{RaceClock, EWSRelayInputType::Pass};
	PlayerRelayTrace.Add(Pass);
	PlayerRunner->PushRelayInput(Pass);
}

float AWSSprintGameMode::GetMetresToHandover() const
{
	return PlayerRunner && PlayerRunner->IsRelayEvent()
		? static_cast<float>(PlayerRunner->GetRelayState().MetresToHandover) : 0.0f;
}

bool AWSSprintGameMode::IsInTakeoverZone() const
{
	return PlayerRunner && PlayerRunner->IsRelayEvent()
		&& PlayerRunner->GetRelayState().bInTakeoverZone;
}

float AWSSprintGameMode::GetTakeoverZoneMetres() const
{
	return IsRelayEvent()
		? static_cast<float>(CurrentRelayEvent().TakeoverZoneMetres) : 0.0f;
}

int32 AWSSprintGameMode::GetRelayLegCount() const
{
	return IsRelayEvent() ? CurrentRelayEvent().LegCount : 0;
}

int32 AWSSprintGameMode::GetRelayLeg() const
{
	return PlayerRunner && PlayerRunner->IsRelayEvent()
		? PlayerRunner->GetRelayState().Leg : 0;
}

void AWSSprintGameMode::PlayerTakeoff()
{
	// The one decision the event turns on. Everything before it is an
	// approach; everything after is physics.
	if (!JumpSim.IsValid() || !bRaceRunning || bPaused || RaceClock < 0.0)
	{
		return;
	}
	const FWSJumpInputEvent Takeoff{RaceClock, EWSJumpInputType::Takeoff};
	PlayerJumpTrace.Add(Takeoff);
	JumpSim->AddInput(Takeoff);
}

void AWSSprintGameMode::PlayerRelease()
{
	if (IsFieldEvent())
	{
		return; // nothing to release: there are no blocks in a field event
	}
	if (IsPaceEvent())
	{
		if (PlayerRunner && bRaceRunning && !bPaused)
		{
			// Letting go eases off. Easing is not free recovery — the tank
			// refills slowly — but it is how a race is saved.
			PushPlayerEffort(FMath::Max(0.35, PlayerEffort - 0.10));
		}
		return;
	}
	if (IsRelayEvent())
	{
		if (!bHolding || !PlayerRunner || bPaused || PlayerRunner->GetState().bReleased)
		{
			return;
		}
		bHolding = false;
		// Releasing before the gun is a false start, and the SIMULATION
		// decides that — not the UI.
		const FWSRelayInputEvent Event{RaceClock, EWSRelayInputType::Release};
		PlayerRelayTrace.Add(Event);
		PlayerRunner->PushRelayInput(Event);
		return;
	}
	if (!bHolding || !PlayerRunner || bPaused || PlayerRunner->GetState().bReleased)
	{
		return;
	}
	bHolding = false;
	// Release before the gun (negative clock) is a false start, and the
	// simulation — not the UI — is what decides that.
	const FWSSprintInputEvent Event{RaceClock, EWSSprintInputType::Release};
	PlayerTrace.Add(Event);
	PlayerRunner->PushInput(Event);
}

void AWSSprintGameMode::PlayerLean()
{
	if (!PlayerRunner || !bRaceRunning || bPaused)
	{
		return;
	}
	if (IsThrowEvent())
	{
		PlayerThrowRelease();
		return;
	}
	if (IsJumpEvent())
	{
		PlayerTakeoff();
		return;
	}
	if (IsPaceEvent())
	{
		// The dip at the line becomes the kick: spend whatever is left.
		// Early it costs, which is the decision the event is built on.
		const FWSPaceInputEvent Kick{FMath::Max(RaceClock, 0.0), EWSPaceInputType::Kick, 0.0};
		PlayerPaceTrace.Add(Kick);
		PlayerRunner->PushPaceInput(Kick);
		PlayerEffort = 1.0;
		return;
	}
	// One contextual action button. With a barrier close ahead it is the
	// takeoff; otherwise it is the dip at the line. A hurdler pressing
	// "lean" three metres from a hurdle means to jump it, and giving that
	// two separate controls on a phone would be worse, not clearer.
	const double ToHurdle = PlayerRunner->MetresToNextHurdle();
	if (ToHurdle >= 0.0 && ToHurdle <= HurdlePromptMetres)
	{
		const FWSSprintInputEvent Takeoff{RaceClock, EWSSprintInputType::Hurdle};
		PlayerTrace.Add(Takeoff);
		PlayerRunner->PushInput(Takeoff);
		return;
	}

	const FWSSprintInputEvent Event{RaceClock, EWSSprintInputType::Lean};
	PlayerTrace.Add(Event);
	PlayerRunner->PushInput(Event);
}

void AWSSprintGameMode::PushPlayerEffort(double Effort)
{
	if (!PlayerRunner)
	{
		return;
	}
	PlayerEffort = FMath::Clamp(Effort, 0.35, 1.0);
	// Times are clamped at the gun: an effort chosen during the countdown
	// is a plan, not a head start.
	const FWSPaceInputEvent Event{
		FMath::Max(RaceClock, 0.0), EWSPaceInputType::SetEffort, PlayerEffort};
	PlayerPaceTrace.Add(Event);
	PlayerRunner->PushPaceInput(Event);
}
