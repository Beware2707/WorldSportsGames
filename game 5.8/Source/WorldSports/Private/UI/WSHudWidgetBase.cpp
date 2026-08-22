#include "UI/WSHudWidgetBase.h"

#include "Framework/WSGameStateBase.h"

void UWSHudWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();
	if (AWSGameStateBase* WSGameState = GetWorld()->GetGameState<AWSGameStateBase>())
	{
		WSGameState->OnPhaseChanged.AddDynamic(this, &ThisClass::HandlePhaseChanged);
	}
}

void UWSHudWidgetBase::NativeDestruct()
{
	if (AWSGameStateBase* WSGameState =
			GetWorld() ? GetWorld()->GetGameState<AWSGameStateBase>() : nullptr)
	{
		WSGameState->OnPhaseChanged.RemoveDynamic(this, &ThisClass::HandlePhaseChanged);
	}
	Super::NativeDestruct();
}

void UWSHudWidgetBase::HandlePhaseChanged(EWSEventPhase OldPhase, EWSEventPhase NewPhase)
{
	OnEventPhaseChanged(OldPhase, NewPhase);
}
