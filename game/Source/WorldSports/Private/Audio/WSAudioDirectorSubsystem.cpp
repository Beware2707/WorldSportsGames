#include "Audio/WSAudioDirectorSubsystem.h"

void UWSAudioDirectorSubsystem::NotifyPhase(EWSEventPhase Phase)
{
	CurrentPhase = Phase;
	if (Phase == EWSEventPhase::Finishing)
	{
		AddCrowdExcitement(0.6f);
	}
}

void UWSAudioDirectorSubsystem::AddCrowdExcitement(float Amount)
{
	CrowdExcitement = FMath::Clamp(CrowdExcitement + Amount, 0.0f, 1.0f);
}
