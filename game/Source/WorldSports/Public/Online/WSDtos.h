#pragma once

#include "CoreMinimal.h"

#include "WSDtos.generated.h"

/**
 * DTOs mirroring the FastAPI schemas. Property names are deliberately
 * snake_case, identical to the wire format, so FJsonObjectConverter maps them
 * without a rename table — the backend schema files are the single source of
 * truth for these names (backend/app/schemas/).
 */

USTRUCT()
struct WORLDSPORTS_API FWSTokenResponse
{
	GENERATED_BODY()

	UPROPERTY()
	FString access_token;

	UPROPERTY()
	FString token_type = TEXT("bearer");
};

USTRUCT(BlueprintType)
struct WORLDSPORTS_API FWSUserDto
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	int32 id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString email;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString display_name;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString created_at;
};

/** backend ResultOut: the server's authoritative answer to a submitted result. */
USTRUCT(BlueprintType)
struct WORLDSPORTS_API FWSResultResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	bool accepted = false;

	/** Empty when accepted; the server's exact reason otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString rejection_reason;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString event;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString value_text;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	bool is_personal_best = false;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	int32 xp_awarded = 0;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	int32 total_xp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString career_stage;
};

USTRUCT(BlueprintType)
struct WORLDSPORTS_API FWSAnalyticsRejection
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	int32 index = 0;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString reason;
};

USTRUCT(BlueprintType)
struct WORLDSPORTS_API FWSAnalyticsBatchResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	int32 accepted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	TArray<FWSAnalyticsRejection> rejected;
};

USTRUCT(BlueprintType)
struct WORLDSPORTS_API FWSEventCatalogueEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString code;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString name;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString value_kind;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	bool lower_is_better = true;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	FString unit;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	int32 splits_expected = 0;

	UPROPERTY(BlueprintReadOnly, Category = "WorldSports")
	bool requires_reaction = false;
};
