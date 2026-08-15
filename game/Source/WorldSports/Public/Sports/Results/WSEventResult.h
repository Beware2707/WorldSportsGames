#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

#include "WSEventResult.generated.h"

/**
 * A finished, locally simulated result — the ONLY thing the client ever
 * claims about a performance. XP, PBs, ranks and validity all come back from
 * the server (backend ResultIn/ResultOut).
 *
 * Optional fields carry an explicit presence flag rather than a sentinel:
 * the backend distinguishes "no reaction phase in this event" (null) from a
 * measured value, and a sentinel like -1 would be sent as a real measurement.
 */
USTRUCT(BlueprintType)
struct WORLDSPORTS_API FWSEventResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports")
	FString EventCode;

	/** Seconds for timed events, metres for distance, points for scored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports")
	double ValueNum = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports")
	bool bHasReactionMs = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports")
	double ReactionMs = 0.0;

	/** Empty means "not measured"; the backend treats missing splits as null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports")
	TArray<double> Splits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports")
	bool bHasWind = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports")
	double Wind = 0.0;

	/** Seed the simulation ran with — lets the server replay/audit later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports")
	FString RngSeed;

	/** Digest of the input trace; an anti-tamper breadcrumb, not proof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports")
	FString InputDigest;

	/**
	 * Wire form of backend ResultIn. Returns nullptr if any numeric field is
	 * non-finite — the server rejects NaN/Infinity, and a client must never
	 * try to send what it knows the server forbids.
	 */
	TSharedPtr<FJsonObject> ToRequestJson() const;

	/** Inverse of ToRequestJson, used by the offline queue. */
	static bool FromRequestJson(const TSharedPtr<FJsonObject>& Json, FWSEventResult& Out);

	bool EquivalentTo(const FWSEventResult& Other, double Tolerance = 1e-9) const;
};
