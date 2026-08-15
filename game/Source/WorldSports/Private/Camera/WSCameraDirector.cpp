#include "Camera/WSCameraDirector.h"

#include "Camera/CameraActor.h"
#include "Framework/WSGameStateBase.h"
#include "Kismet/GameplayStatics.h"

void AWSCameraDirector::BeginPlay()
{
	Super::BeginPlay();
	if (AWSGameStateBase* WSGameState = GetWorld()->GetGameState<AWSGameStateBase>())
	{
		WSGameState->OnPhaseChanged.AddDynamic(this, &ThisClass::HandlePhaseChanged);
	}
}

void AWSCameraDirector::HandlePhaseChanged(EWSEventPhase, EWSEventPhase NewPhase)
{
	CutToPhase(NewPhase);
}

void AWSCameraDirector::CutToPhase(EWSEventPhase Phase)
{
	const TObjectPtr<ACameraActor>* Camera = PhaseCameras.Find(Phase);
	if (!Camera || !*Camera)
	{
		return;
	}
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		PlayerController->SetViewTargetWithBlend(*Camera, BlendSeconds);
	}
}
