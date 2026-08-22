#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "WSLocalSave.generated.h"

/** The local career save. Payload is opaque JSON — the same dict the backend
 * stores — so client and server never disagree about its shape. */
UCLASS()
class WORLDSPORTS_API UWSLocalSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString PayloadJson = TEXT("{}");

	/** Last server version this payload was synced against. */
	UPROPERTY()
	int32 SyncedVersion = 0;

	/** True when PayloadJson has local edits the server has not seen. */
	UPROPERTY()
	bool bDirty = false;
};
