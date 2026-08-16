#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "WSCreateSprintMapCommandlet.generated.h"

/** Creates the (intentionally empty) sprint level asset. See the .cpp. */
UCLASS()
class UWSCreateSprintMapCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UWSCreateSprintMapCommandlet();

	virtual int32 Main(const FString& Params) override;
};
