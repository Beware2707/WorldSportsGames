#include "Progression/WSProgressionSubsystem.h"

#include "Core/WSLog.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Online/WSOnlineSubsystem.h"

namespace
{
const TCHAR* CareerAthletePath = TEXT("/api/v1/career/athlete");
const TCHAR* CareerTrainingPath = TEXT("/api/v1/career/training");
}

void UWSProgressionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UWSOnlineSubsystem>();
	if (UWSOnlineSubsystem* OnlineSubsystem = Online())
	{
		OnlineSubsystem->OnResultSubmitted.AddDynamic(this, &ThisClass::HandleResultSubmitted);
		OnlineSubsystem->OnAuthChanged.AddDynamic(this, &ThisClass::HandleAuthChanged);
	}
}

UWSOnlineSubsystem* UWSProgressionSubsystem::Online() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr;
}

void UWSProgressionSubsystem::HandleAuthChanged(bool bSignedIn)
{
	if (bSignedIn)
	{
		RefreshCareerAthlete();
		return;
	}
	// Signing out clears the athlete: continuing to race with the previous
	// account's attributes would simulate against a ceiling the server will
	// not validate the result against.
	CareerAthlete = FWSCareerAthleteDto();
	TotalXp = 0;
	CareerStage.Reset();
	OnProgressionChanged.Broadcast();
}

void UWSProgressionSubsystem::HandleResultSubmitted(const FWSResultResponse& Response, bool bAccepted)
{
	LastResult = Response;
	if (bAccepted)
	{
		// The response carries the authoritative totals — adopt, don't add.
		TotalXp = Response.total_xp;
		CareerStage = Response.career_stage;
		CareerAthlete.total_xp = Response.total_xp;
		CareerAthlete.career_stage = Response.career_stage;
	}
	OnProgressionChanged.Broadcast();
}

void UWSProgressionSubsystem::RefreshCareerAthlete(
	TFunction<void(bool, const FString&)> Callback)
{
	UWSOnlineSubsystem* OnlineSubsystem = Online();
	if (!OnlineSubsystem || !OnlineSubsystem->IsSignedIn())
	{
		if (Callback)
		{
			Callback(false, TEXT("Not signed in"));
		}
		return;
	}

	TWeakObjectPtr<UWSProgressionSubsystem> WeakThis(this);
	OnlineSubsystem->Request(TEXT("GET"), CareerAthletePath, nullptr,
		[WeakThis, Callback](const FWSHttpResult& Result)
		{
			UWSProgressionSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			if (Result.bTransportOk && Result.StatusCode == 404)
			{
				// No athlete yet is a normal state, not an error: the career
				// creation screen is what answers it.
				Self->CareerAthlete = FWSCareerAthleteDto();
				Self->OnProgressionChanged.Broadcast();
				if (Callback)
				{
					Callback(false, TEXT("No career athlete yet"));
				}
				return;
			}
			if (!Result.IsSuccess() || !Result.Json.IsValid())
			{
				if (Callback)
				{
					Callback(false, Result.bTransportOk
						? FString::Printf(TEXT("Could not load your athlete (%d)"), Result.StatusCode)
						: Result.ErrorText);
				}
				return;
			}

			FWSCareerAthleteDto Dto;
			FJsonObjectConverter::JsonObjectToUStruct(Result.Json.ToSharedRef(), &Dto);
			// FJsonObjectConverter does not fill a TMap<FString,float> from a
			// JSON object, so the attributes are read explicitly — by LOOKUP
			// rather than by iterating the map, because FJsonObject's key type
			// is not FString on every platform (Android builds it as
			// UE::TSharedString) and iterating it is not portable.
			Dto.attributes.Reset();
			const TSharedPtr<FJsonObject>* Attributes = nullptr;
			if (Result.Json->TryGetObjectField(TEXT("attributes"), Attributes) && Attributes)
			{
				// The backend's ATTRIBUTE_KEYS — the contract between the two.
				static const TCHAR* Keys[] = {
					TEXT("reaction"), TEXT("acceleration"), TEXT("max_speed"),
					TEXT("stride_efficiency"), TEXT("stamina"),
					TEXT("recovery"), TEXT("technique")};
				for (const TCHAR* Key : Keys)
				{
					double Value = 0.0;
					if ((*Attributes)->TryGetNumberField(Key, Value))
					{
						Dto.attributes.Add(Key, static_cast<float>(Value));
					}
				}
			}

			Self->CareerAthlete = MoveTemp(Dto);
			Self->TotalXp = Self->CareerAthlete.total_xp;
			Self->CareerStage = Self->CareerAthlete.career_stage;
			UE_LOG(LogWorldSports, Log,
				TEXT("Career athlete '%s' loaded: %s, %d XP, %d attributes"),
				*Self->CareerAthlete.name, *Self->CareerAthlete.career_stage,
				Self->CareerAthlete.total_xp, Self->CareerAthlete.attributes.Num());
			Self->OnProgressionChanged.Broadcast();
			if (Callback)
			{
				Callback(true, FString());
			}
		});
}

void UWSProgressionSubsystem::CreateCareerAthlete(const FString& Name, const FString& Gender,
	TFunction<void(bool, const FString&)> Callback)
{
	UWSOnlineSubsystem* OnlineSubsystem = Online();
	if (!OnlineSubsystem || !OnlineSubsystem->IsSignedIn())
	{
		if (Callback)
		{
			Callback(false, TEXT("Sign in first"));
		}
		return;
	}

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("name"), Name);
	Body->SetStringField(TEXT("gender"), Gender);

	TWeakObjectPtr<UWSProgressionSubsystem> WeakThis(this);
	OnlineSubsystem->Request(TEXT("POST"), CareerAthletePath, Body,
		[WeakThis, Callback](const FWSHttpResult& Result)
		{
			UWSProgressionSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			// 409 means one already exists — refreshing is the right answer.
			if (Result.IsSuccess() || (Result.bTransportOk && Result.StatusCode == 409))
			{
				Self->RefreshCareerAthlete(Callback);
				return;
			}
			if (Callback)
			{
				Callback(false, Result.bTransportOk
					? FString::Printf(TEXT("Could not create your athlete (%d)"), Result.StatusCode)
					: Result.ErrorText);
			}
		});
}

void UWSProgressionSubsystem::SubmitTraining(const FString& Drill, double Metric,
	TFunction<void(bool, const FWSTrainingResponse&, const FString&)> Callback)
{
	UWSOnlineSubsystem* OnlineSubsystem = Online();
	if (!OnlineSubsystem || !OnlineSubsystem->IsSignedIn())
	{
		if (Callback)
		{
			Callback(false, FWSTrainingResponse(), TEXT("Sign in to train"));
		}
		return;
	}
	if (!FMath::IsFinite(Metric))
	{
		// The server forbids non-finite metrics; never send what it forbids.
		if (Callback)
		{
			Callback(false, FWSTrainingResponse(), TEXT("Invalid drill result"));
		}
		return;
	}

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("drill"), Drill);
	Body->SetNumberField(TEXT("metric"), Metric);
	// Idempotent: a resend cannot train twice.
	Body->SetStringField(TEXT("client_ref"),
		FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));

	TWeakObjectPtr<UWSProgressionSubsystem> WeakThis(this);
	OnlineSubsystem->Request(TEXT("POST"), CareerTrainingPath, Body,
		[WeakThis, Callback](const FWSHttpResult& Result)
		{
			UWSProgressionSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			if (!Result.IsSuccess() || !Result.Json.IsValid())
			{
				if (Callback)
				{
					Callback(false, FWSTrainingResponse(), Result.bTransportOk
						? FString::Printf(TEXT("Training not recorded (%d)"), Result.StatusCode)
						: Result.ErrorText);
				}
				return;
			}
			FWSTrainingResponse Response;
			FJsonObjectConverter::JsonObjectToUStruct(Result.Json.ToSharedRef(), &Response);
			Self->LastTraining = Response;
			// The gain is the SERVER's; re-read the athlete rather than
			// adding the reported number to a local copy.
			Self->RefreshCareerAthlete();
			if (Callback)
			{
				Callback(true, Response, FString());
			}
		});
}
