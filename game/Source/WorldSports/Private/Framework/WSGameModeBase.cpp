#include "Framework/WSGameModeBase.h"

#include "Framework/WSAthleteCharacter.h"
#include "Framework/WSGameStateBase.h"
#include "Framework/WSPlayerController.h"

AWSGameModeBase::AWSGameModeBase()
{
	GameStateClass = AWSGameStateBase::StaticClass();
	PlayerControllerClass = AWSPlayerController::StaticClass();
	DefaultPawnClass = AWSAthleteCharacter::StaticClass();
}
