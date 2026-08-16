#include "Race/WSSprintGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Core/WSLog.h"
#include "Framework/WSGameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Online/WSOnlineSubsystem.h"
#include "Race/WSSprintHud.h"
#include "Race/WSSprintPlayerController.h"
#include "Race/WSSprintRunner.h"
#include "Math/RandomStream.h"
#include "Race/WSSprintTrack.h"
#include "Simulation/WSSprintDifficulty.h"
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
	}

	StartRace();
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
	// Quick Play defaults match a fresh career athlete (the backend seeds
	// every attribute at 40), so a Quick Play time is submittable as-is and
	// the ceiling the server enforces is the ceiling the player feels.
	FWSSprintAttributes Attributes;
	Attributes.Reaction = 40.0f;
	Attributes.Acceleration = 40.0f;
	Attributes.MaxSpeed = 40.0f;
	Attributes.StrideEfficiency = 40.0f;
	Attributes.Stamina = 40.0f;
	Attributes.Technique = 40.0f;
	return Attributes;
}

void AWSSprintGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bRaceRunning)
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
	if (!PlayerRunner || !bRaceRunning)
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
	if (!bHolding || !PlayerRunner || PlayerRunner->GetState().bReleased)
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
	if (!PlayerRunner || !bRaceRunning)
	{
		return;
	}
	const FWSSprintInputEvent Event{RaceClock, EWSSprintInputType::Lean};
	PlayerTrace.Add(Event);
	PlayerRunner->PushInput(Event);
}
