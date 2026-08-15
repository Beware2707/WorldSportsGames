#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WSSaveSubsystem.generated.h"

class UWSLocalSave;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWSOnSaveSynced, int32, ServerVersion);

using FWSSyncCallback = TFunction<void(bool bOk, const FString& Error)>;

/**
 * Local save + cloud sync with additive conflict resolution.
 *
 * Model: the payload is written locally FIRST (a result must never be lost
 * to a network error — architecture §6). `SyncedVersion` is the last server
 * version we synced against; local edits set a dirty flag. A push that hits
 * a 409 adopts the server's `suggested_merge` (numeric max + list union,
 * computed server-side by the same code the backend tests) and retries once
 * from the server's version.
 */
UCLASS()
class WORLDSPORTS_API UWSSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Current payload as a JSON object string. Never empty — at least "{}". */
	UFUNCTION(BlueprintPure, Category = "WorldSports|Save")
	FString GetPayloadJson() const;

	/** Replace the local payload and persist to disk. Marks the save dirty. */
	bool SetPayloadJson(const FString& PayloadJson);

	/** Pull the server save. Clean local state adopts the server's; a dirty
	 * one pushes instead so the server can arbitrate the conflict. */
	void CloudPull(FWSSyncCallback Callback = nullptr);

	/** Push local payload from SyncedVersion; on 409 adopt the suggested
	 * merge and retry once from the server's version. */
	void CloudPush(FWSSyncCallback Callback = nullptr);

	UFUNCTION(BlueprintPure, Category = "WorldSports|Save")
	int32 GetSyncedVersion() const;

	UFUNCTION(BlueprintPure, Category = "WorldSports|Save")
	bool IsDirty() const;

	UPROPERTY(BlueprintAssignable, Category = "WorldSports|Save")
	FWSOnSaveSynced OnSaveSynced;

private:
	UFUNCTION()
	void HandleAuthChanged(bool bSignedIn);

	void PushInternal(FWSSyncCallback Callback, bool bRetryOnConflict);
	void PersistLocal();
	class UWSOnlineSubsystem* Online() const;

	UPROPERTY()
	TObjectPtr<UWSLocalSave> LocalSave;
};
