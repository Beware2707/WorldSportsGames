#include "Sports/Results/WSEventResult.h"

namespace
{
bool AllFinite(const FWSEventResult& Result)
{
	if (!FMath::IsFinite(Result.ValueNum))
	{
		return false;
	}
	if (Result.bHasReactionMs && !FMath::IsFinite(Result.ReactionMs))
	{
		return false;
	}
	if (Result.bHasWind && !FMath::IsFinite(Result.Wind))
	{
		return false;
	}
	for (const double Split : Result.Splits)
	{
		if (!FMath::IsFinite(Split))
		{
			return false;
		}
	}
	return true;
}
}

TSharedPtr<FJsonObject> FWSEventResult::ToRequestJson() const
{
	if (EventCode.IsEmpty() || !AllFinite(*this))
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("event"), EventCode);
	Json->SetNumberField(TEXT("value_num"), ValueNum);
	if (bHasReactionMs)
	{
		Json->SetNumberField(TEXT("reaction_ms"), ReactionMs);
	}
	if (Splits.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> SplitValues;
		SplitValues.Reserve(Splits.Num());
		for (const double Split : Splits)
		{
			SplitValues.Add(MakeShared<FJsonValueNumber>(Split));
		}
		Json->SetArrayField(TEXT("splits"), SplitValues);
	}
	if (bHasWind)
	{
		Json->SetNumberField(TEXT("wind"), Wind);
	}
	if (!RngSeed.IsEmpty())
	{
		Json->SetStringField(TEXT("rng_seed"), RngSeed);
	}
	if (!InputDigest.IsEmpty())
	{
		Json->SetStringField(TEXT("input_digest"), InputDigest);
	}
	return Json;
}

bool FWSEventResult::FromRequestJson(const TSharedPtr<FJsonObject>& Json, FWSEventResult& Out)
{
	if (!Json.IsValid())
	{
		return false;
	}
	Out = FWSEventResult();
	if (!Json->TryGetStringField(TEXT("event"), Out.EventCode) ||
		!Json->TryGetNumberField(TEXT("value_num"), Out.ValueNum))
	{
		return false;
	}
	Out.bHasReactionMs = Json->TryGetNumberField(TEXT("reaction_ms"), Out.ReactionMs);
	Out.bHasWind = Json->TryGetNumberField(TEXT("wind"), Out.Wind);

	const TArray<TSharedPtr<FJsonValue>>* SplitValues = nullptr;
	if (Json->TryGetArrayField(TEXT("splits"), SplitValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SplitValues)
		{
			Out.Splits.Add(Value->AsNumber());
		}
	}
	Json->TryGetStringField(TEXT("rng_seed"), Out.RngSeed);
	Json->TryGetStringField(TEXT("input_digest"), Out.InputDigest);
	return true;
}

bool FWSEventResult::EquivalentTo(const FWSEventResult& Other, double Tolerance) const
{
	if (EventCode != Other.EventCode || RngSeed != Other.RngSeed ||
		InputDigest != Other.InputDigest ||
		bHasReactionMs != Other.bHasReactionMs || bHasWind != Other.bHasWind ||
		Splits.Num() != Other.Splits.Num())
	{
		return false;
	}
	if (!FMath::IsNearlyEqual(ValueNum, Other.ValueNum, Tolerance) ||
		(bHasReactionMs && !FMath::IsNearlyEqual(ReactionMs, Other.ReactionMs, Tolerance)) ||
		(bHasWind && !FMath::IsNearlyEqual(Wind, Other.Wind, Tolerance)))
	{
		return false;
	}
	for (int32 Index = 0; Index < Splits.Num(); ++Index)
	{
		if (!FMath::IsNearlyEqual(Splits[Index], Other.Splits[Index], Tolerance))
		{
			return false;
		}
	}
	return true;
}
