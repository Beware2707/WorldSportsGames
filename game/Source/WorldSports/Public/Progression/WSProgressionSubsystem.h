#pragma once

#include "CoreMinimal.h"
#include "Online/WSDtos.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WSProgressionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWSOnProgressionChanged);

/**
 * DISPLAY MIRROR of server-owned progression. Every number here came out of
 * a backend response; nothing in the client may compute XP, levels or PBs
 * and push them into this state. If a value looks wrong here, the fix is on
 * the server, never a client-side adjustment.
 */
UCLASS()
class WORLDSPORTS_API UWSProgressionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintPure, Category = "WorldSports|Progression")
	int32 GetTotalXp() const { return TotalXp; }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Progression")
	FString GetCareerStage() const { return CareerStage; }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Progression")
	const FWSResultResponse& GetLastResult() const { return LastResult; }

	// -- Career athlete (server-owned) -----------------------------------

	/** Fetch GET /career/athlete. Also called automatically on sign-in. */
	void RefreshCareerAthlete(TFunction<void(bool bOk, const FString& Error)> Callback = nullptr);

	/** Create the career athlete, then refresh. */
	void CreateCareerAthlete(const FString& Name, const FString& Gender,
		TFunction<void(bool bOk, const FString& Error)> Callback = nullptr);

	UFUNCTION(BlueprintPure, Category = "WorldSports|Progression")
	bool HasCareerAthlete() const { return CareerAthlete.id != 0; }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Progression")
	const FWSCareerAthleteDto& GetCareerAthlete() const { return CareerAthlete; }

	/** Submit a completed drill. The server decides the gain. */
	void SubmitTraining(const FString& Drill, double Metric,
		TFunction<void(bool bOk, const FWSTrainingResponse&, const FString& Error)> Callback);

	UFUNCTION(BlueprintPure, Category = "WorldSports|Progression")
	const FWSTrainingResponse& GetLastTraining() const { return LastTraining; }

	UPROPERTY(BlueprintAssignable, Category = "WorldSports|Progression")
	FWSOnProgressionChanged OnProgressionChanged;

private:
	UFUNCTION()
	void HandleResultSubmitted(const FWSResultResponse& Response, bool bAccepted);

	UFUNCTION()
	void HandleAuthChanged(bool bSignedIn);

	class UWSOnlineSubsystem* Online() const;

	int32 TotalXp = 0;
	FString CareerStage;
	FWSResultResponse LastResult;
	FWSCareerAthleteDto CareerAthlete;
	FWSTrainingResponse LastTraining;
};
