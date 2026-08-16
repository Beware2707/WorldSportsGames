#include "Race/WSSprintGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Core/WSLog.h"
#include "Framework/WSGameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Online/WSOnlineSubsystem.h"
#include "Progression/WSProgressionSubsystem.h"
#include "Race/WSSprintAudio.h"
#include "Race/WSSprintHud.h"
#include "Race/WSSprintPlayerController.h"
#include "Race/WSSprintRunner.h"
#include "Math/RandomStream.h"
#include "Race/WSSprintTrack.h"
#include "Simulation/WSSprintDifficulty.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sports/Results/WSEventResult.h"

namespace
{
// The starter holds the field for a variable pause before the gun.
constexpr double MinSetSeconds = 2.2;
constexpr double MaxSetSeconds = 4.6;
constexpr float ResultDwellSeconds = 1.2f;
const TCHAR* SprintEventCode = TEXT("sprint-100m");

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
	if (NewState == EWSAppState::Leaderboard)
	{
		RefreshLeaderboard();
	}
}

void AWSSprintGameMode::StartQuickPlay()
{
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
		TEXT("/api/v1/career/leaderboard?event=sprint-100m&scope=global&period=all_time"),
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

void AWSSprintGameMode::StartRace()
{
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
			Runner->InitializeRace(ResolvePlayerAttributes(), RaceSeed, Lane,
				TEXT("You"), /*bIsPlayer=*/true);
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
			Runner->InitializeRace(Attributes, RaceSeed, Lane,
				OpponentNames[OpponentIndex % UE_ARRAY_COUNT(OpponentNames)],
				/*bIsPlayer=*/false);
			Runner->PushTrace(FWSSprintSimulation::GenerateAITrace(
				Attributes, RaceSeed, InputSeed, Level.ReactionMeanMs,
				Level.ReactionSpreadMs, Level.Consistency));
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
	Read(TEXT("technique"), Attributes.Technique);
	return Attributes;
}

void AWSSprintGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bRaceRunning && !bPaused)
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
	if (NewPhase == EWSEventPhase::Result)
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
	const FWSSprintState& State = PlayerRunner->GetState();
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
		Desired = FVector(AWSSprintTrack::TrackLengthCm + 260.0f, LaneY - 900.0f, 320.0f);
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
	const FWSSprintState& State = PlayerRunner->GetState();
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
		const FWSSprintOutcome OutA = A.GetOutcome();
		const FWSSprintOutcome OutB = B.GetOutcome();
		if (OutA.bFinished != OutB.bFinished)
		{
			return OutA.bFinished;
		}
		return OutA.TimeSeconds < OutB.TimeSeconds;
	});

	int32 Position = 1;
	for (const AWSSprintRunner* Runner : Sorted)
	{
		const FWSSprintOutcome Outcome = Runner->GetOutcome();
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
	if (!PlayerRunner)
	{
		return;
	}
	const FWSSprintOutcome Outcome = PlayerRunner->GetOutcome();
	if (!Outcome.bFinished)
	{
		// A false start has no time to submit. Saying so plainly beats
		// inventing a result or silently skipping the step.
		ServerVerdict = TEXT("False start — no time to submit");
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
	Result.EventCode = SprintEventCode;
	Result.ValueNum = Outcome.TimeSeconds;
	Result.bHasReactionMs = true;
	Result.ReactionMs = Outcome.ReactionMs;
	Result.Splits = Outcome.Splits;
	Result.bHasWind = true;
	Result.Wind = Outcome.Wind;
	Result.RngSeed = FString::Printf(TEXT("%u"), RaceSeed);
	Result.InputDigest = FWSSprintSimulation::DigestTrace(PlayerTrace);

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
	const FWSSprintState* State = PlayerRunner ? &PlayerRunner->GetState() : nullptr;
	UE_LOG(LogWorldSports, Display,
		TEXT("WSStatus app=%d phase=%d clock=%.2f paused=%d signedIn=%d "
			 "dist=%.1f speed=%.2f board=%d/%s verdict='%s'"),
		static_cast<int32>(AppState), static_cast<int32>(GetPhase()), RaceClock,
		bPaused ? 1 : 0, IsSignedIn() ? 1 : 0,
		State ? State->Distance : 0.0, State ? State->Speed : 0.0,
		LeaderboardRows.Num(), *LeaderboardStatus, *ServerVerdict);
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

void AWSSprintGameMode::PlayerRelease()
{
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
	const FWSSprintInputEvent Event{RaceClock, EWSSprintInputType::Lean};
	PlayerTrace.Add(Event);
	PlayerRunner->PushInput(Event);
}
