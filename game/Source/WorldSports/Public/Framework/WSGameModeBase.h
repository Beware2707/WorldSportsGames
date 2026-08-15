#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "WSGameModeBase.generated.h"

/** Project-wide defaults; menu levels use this directly. */
UCLASS()
class WORLDSPORTS_API AWSGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWSGameModeBase();
};
