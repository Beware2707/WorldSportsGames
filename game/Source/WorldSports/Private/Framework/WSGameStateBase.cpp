#include "Framework/WSGameStateBase.h"

#include "Core/WSLog.h"
#include "Net/UnrealNetwork.h"

void AWSGameStateBase::SetPhase(EWSEventPhase NewPhase)
{
	if (!HasAuthority() || NewPhase == Phase)
	{
		return;
	}
	const EWSEventPhase OldPhase = Phase;
	Phase = NewPhase;
	UE_LOG(LogWorldSports, Verbose, TEXT("Event phase %d -> %d"),
		static_cast<int32>(OldPhase), static_cast<int32>(NewPhase));
	OnPhaseChanged.Broadcast(OldPhase, NewPhase);
}

void AWSGameStateBase::OnRep_Phase(EWSEventPhase OldPhase)
{
	OnPhaseChanged.Broadcast(OldPhase, Phase);
}

void AWSGameStateBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWSGameStateBase, Phase);
	DOREPLIFETIME(AWSGameStateBase, EventClockSeconds);
}
