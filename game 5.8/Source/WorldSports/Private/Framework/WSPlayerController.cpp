#include "Framework/WSPlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

void AWSPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (BaseMappingContext.IsNull())
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		InputSubsystem->AddMappingContext(BaseMappingContext.LoadSynchronous(), /*Priority=*/0);
	}
}
