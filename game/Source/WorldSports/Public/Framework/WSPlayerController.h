#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "WSPlayerController.generated.h"

class UInputMappingContext;

/** Input routing and HUD ownership. Sport modules add their own mapping
 * contexts on top of the base one. */
UCLASS()
class WORLDSPORTS_API AWSPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "WorldSports|Input")
	TSoftObjectPtr<UInputMappingContext> BaseMappingContext;
};
