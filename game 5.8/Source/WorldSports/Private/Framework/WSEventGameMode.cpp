#include "Framework/WSEventGameMode.h"

#include "Core/WSLog.h"
#include "Framework/WSEventRules.h"
#include "Framework/WSGameStateBase.h"
#include "Online/WSOnlineSubsystem.h"
#include "Sports/Definitions/WSEventDefinition.h"
#include "Sports/Results/WSEventResult.h"

void AWSEventGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (EventDefinition && !EventDefinition->Rules.IsNull())
	{
		Rules = EventDefinition->Rules.LoadSynchronous();
	}
	SetPhase(EWSEventPhase::Load);
}

AWSGameStateBase* AWSEventGameMode::WSGameState() const
{
	// The world's game state, not AGameModeBase::GameState: the two are the
	// same in normal play, but the world's is also valid for a game mode
	// driven directly (automation, headless replay).
	return GetWorld() ? GetWorld()->GetGameState<AWSGameStateBase>() : nullptr;
}

EWSEventPhase AWSEventGameMode::GetPhase() const
{
	const AWSGameStateBase* State = WSGameState();
	return State ? State->GetPhase() : EWSEventPhase::Load;
}

void AWSEventGameMode::AdvancePhase()
{
	const EWSEventPhase Current = GetPhase();
	if (Current == EWSEventPhase::Reward || !CanLeavePhase(Current))
	{
		return;
	}
	SetPhase(static_cast<EWSEventPhase>(static_cast<uint8>(Current) + 1));
}

void AWSEventGameMode::SetPhase(EWSEventPhase NewPhase)
{
	if (AWSGameStateBase* State = WSGameState())
	{
		State->SetPhase(NewPhase);
		OnPhaseEntered(NewPhase);
	}
}

void AWSEventGameMode::OnPhaseEntered(EWSEventPhase NewPhase)
{
}

void AWSEventGameMode::SubmitLocalResult(const FWSEventResult& Result)
{
	UWSOnlineSubsystem* Online =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr;
	if (!Online)
	{
		UE_LOG(LogWorldSports, Error, TEXT("No online subsystem; result dropped"));
		return;
	}
	SetPhase(EWSEventPhase::Submit);
	// The callback outlives level travel on the GameInstance-lifetime queue;
	// a raw `this` here is a use-after-free once the player exits mid-submit.
	TWeakObjectPtr<AWSEventGameMode> WeakThis(this);
	Online->SubmitResult(Result,
		[WeakThis](EWSSubmitOutcome Outcome, const FWSResultResponse&, const FString&)
		{
			// Queued still reaches Reward: the local result stands, the
			// authoritative numbers arrive whenever connectivity returns.
			if (AWSEventGameMode* Self = WeakThis.Get())
			{
				Self->SetPhase(EWSEventPhase::Reward);
			}
		});
}
