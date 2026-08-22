#include "Progression/WSTournamentSubsystem.h"

#include "Core/WSLog.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Online/WSOnlineSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
const TCHAR* TournamentsPath = TEXT("/api/v1/career/tournaments");
}

void UWSTournamentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UWSOnlineSubsystem>();
	if (UWSOnlineSubsystem* OnlineSubsystem = Online())
	{
		OnlineSubsystem->OnAuthChanged.AddDynamic(this, &ThisClass::HandleAuthChanged);
	}
}

UWSOnlineSubsystem* UWSTournamentSubsystem::Online() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr;
}

void UWSTournamentSubsystem::HandleAuthChanged(bool bSignedIn)
{
	// A bracket belongs to an account. Keeping the previous player's
	// tournament on screen would let the next one submit rounds into it.
	Active = FWSTournamentDto();
	LastRound = FWSTournamentResultDto();
	StatusText.Reset();
	if (bSignedIn)
	{
		Refresh();
	}
	OnTournamentChanged.Broadcast();
}

const TArray<FWSTournamentRival>& UWSTournamentSubsystem::GetCurrentField() const
{
	static const TArray<FWSTournamentRival> Empty;
	for (const FWSTournamentRound& Round : Active.Rounds)
	{
		if (Round.Round == Active.CurrentRound)
		{
			return Round.Field;
		}
	}
	return Empty;
}

void UWSTournamentSubsystem::ApplyTournamentJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid())
	{
		return;
	}
	FWSTournamentDto Dto;
	Json->TryGetNumberField(TEXT("id"), Dto.Id);
	Json->TryGetStringField(TEXT("event"), Dto.Event);
	Json->TryGetStringField(TEXT("current_round"), Dto.CurrentRound);
	Json->TryGetStringField(TEXT("status"), Dto.Status);
	Json->TryGetNumberField(TEXT("final_position"), Dto.FinalPosition);

	const TArray<TSharedPtr<FJsonValue>>* Rounds = nullptr;
	if (Json->TryGetArrayField(TEXT("rounds"), Rounds))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Rounds)
		{
			const TSharedPtr<FJsonObject> RoundJson = Value->AsObject();
			if (!RoundJson.IsValid())
			{
				continue;
			}
			FWSTournamentRound Round;
			RoundJson->TryGetStringField(TEXT("round"), Round.Round);
			// position/advanced are null until the round has been run, which
			// is exactly how "not raced yet" is represented.
			Round.bRun = RoundJson->HasTypedField<EJson::Number>(TEXT("position"));
			RoundJson->TryGetNumberField(TEXT("position"), Round.Position);
			RoundJson->TryGetBoolField(TEXT("advanced"), Round.bAdvanced);

			const TArray<TSharedPtr<FJsonValue>>* Field = nullptr;
			if (RoundJson->TryGetArrayField(TEXT("field"), Field))
			{
				for (const TSharedPtr<FJsonValue>& RivalValue : *Field)
				{
					const TSharedPtr<FJsonObject> RivalJson = RivalValue->AsObject();
					if (!RivalJson.IsValid())
					{
						continue;
					}
					FWSTournamentRival Rival;
					RivalJson->TryGetStringField(TEXT("name"), Rival.Name);
					RivalJson->TryGetStringField(TEXT("iso3"), Rival.Country);
					RivalJson->TryGetNumberField(TEXT("time"), Rival.TimeSeconds);
					Round.Field.Add(MoveTemp(Rival));
				}
			}
			Dto.Rounds.Add(MoveTemp(Round));
		}
	}
	Active = MoveTemp(Dto);
	OnTournamentChanged.Broadcast();
}

void UWSTournamentSubsystem::FlushRefreshCallbacks(bool bOk, const FString& Error)
{
	// Swapped out first: a callback may start another refresh, and it must
	// not see itself in the queue.
	TArray<FWSTournamentCallback> Callbacks = MoveTemp(PendingRefreshCallbacks);
	PendingRefreshCallbacks.Reset();
	for (const FWSTournamentCallback& Callback : Callbacks)
	{
		if (Callback)
		{
			Callback(bOk, Error);
		}
	}
}

void UWSTournamentSubsystem::Refresh(FWSTournamentCallback Callback)
{
	UWSOnlineSubsystem* OnlineSubsystem = Online();
	if (!OnlineSubsystem || !OnlineSubsystem->IsSignedIn())
	{
		StatusText = TEXT("Sign in to enter a tournament");
		if (Callback)
		{
			Callback(false, StatusText);
		}
		return;
	}
	// Callbacks are QUEUED, never dropped. Returning silently while another
	// request was in flight left the caller waiting forever — and sign-in
	// starts a refresh, so "enter a tournament right after signing in" hit
	// it every time.
	if (Callback)
	{
		PendingRefreshCallbacks.Add(MoveTemp(Callback));
	}
	if (bRequestInFlight)
	{
		// The answer already on the wire may have been asked for BEFORE the
		// thing this caller needs to see — a round scored a moment ago, for
		// instance. Remember to read again rather than accepting it.
		bRefreshAgain = true;
		return;
	}
	bRequestInFlight = true;

	TWeakObjectPtr<UWSTournamentSubsystem> WeakThis(this);
	OnlineSubsystem->Request(TEXT("GET"), TournamentsPath, nullptr,
		[WeakThis](const FWSHttpResult& Result)
		{
			UWSTournamentSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bRequestInFlight = false;
			if (!Result.IsSuccess())
			{
				Self->StatusText = Result.bTransportOk
					? FString::Printf(TEXT("Tournaments unavailable (%d)"), Result.StatusCode)
					: TEXT("No connection to the server");
				Self->FlushRefreshCallbacks(false, Self->StatusText);
				return;
			}
			// The list endpoint returns a bare JSON array. A 2xx whose body
			// is NOT that array — a captive portal, a proxy notice — must
			// not be read as "you have no tournament": clearing the bracket
			// on it hid an in-progress one and still reported success, and
			// the next Enter would have started a second bracket.
			TArray<TSharedPtr<FJsonValue>> Parsed;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Body);
			if (!FJsonSerializer::Deserialize(Reader, Parsed))
			{
				Self->StatusText = TEXT("Tournaments unavailable (bad response)");
				Self->FlushRefreshCallbacks(false, Self->StatusText);
				return;
			}

			Self->Active = FWSTournamentDto();
			for (const TSharedPtr<FJsonValue>& Value : Parsed)
			{
				const TSharedPtr<FJsonObject> Json = Value->AsObject();
				FString Status;
				if (Json.IsValid() && Json->TryGetStringField(TEXT("status"), Status) &&
					Status == TEXT("in_progress"))
				{
					Self->ApplyTournamentJson(Json);
					break;
				}
			}
			Self->StatusText.Reset();
			Self->OnTournamentChanged.Broadcast();
			Self->FlushRefreshCallbacks(true, FString());
			if (Self->bRefreshAgain)
			{
				// Something happened while this read was in flight. Read
				// again, or the bracket on screen is older than the server's.
				Self->bRefreshAgain = false;
				Self->Refresh();
			}
		});
}

void UWSTournamentSubsystem::EnterOrResume(const FString& EventCode, FWSTournamentCallback Callback)
{
	UWSOnlineSubsystem* OnlineSubsystem = Online();
	if (!OnlineSubsystem || !OnlineSubsystem->IsSignedIn())
	{
		StatusText = TEXT("Sign in to enter a tournament");
		if (Callback)
		{
			Callback(false, StatusText);
		}
		return;
	}
	if (HasActiveTournament())
	{
		// One bracket at a time: resuming is the honest answer, not silently
		// abandoning a tournament the player is mid-way through.
		if (Callback)
		{
			Callback(true, FString());
		}
		return;
	}
	if (bRequestInFlight)
	{
		// Wait for the in-flight read, then decide: it may reveal a
		// tournament already in progress, in which case entering again would
		// be wrong.
		TWeakObjectPtr<UWSTournamentSubsystem> Weak(this);
		const FString Event = EventCode;
		Refresh([Weak, Event, Callback](bool, const FString&)
		{
			if (UWSTournamentSubsystem* Self = Weak.Get())
			{
				Self->EnterOrResume(Event, Callback);
			}
		});
		return;
	}
	bRequestInFlight = true;
	StatusText = TEXT("Entering…");

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("event"), EventCode);

	TWeakObjectPtr<UWSTournamentSubsystem> WeakThis(this);
	OnlineSubsystem->Request(TEXT("POST"), TournamentsPath, Body,
		[WeakThis, Callback](const FWSHttpResult& Result)
		{
			UWSTournamentSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bRequestInFlight = false;
			if (!Result.IsSuccess() || !Result.Json.IsValid())
			{
				Self->StatusText = Result.bTransportOk
					? FString::Printf(TEXT("Could not enter (%d)"), Result.StatusCode)
					: Result.ErrorText;
				if (Callback)
				{
					Callback(false, Self->StatusText);
				}
				return;
			}
			Self->StatusText.Reset();
			Self->ApplyTournamentJson(Result.Json);
			UE_LOG(LogWorldSports, Log, TEXT("Tournament %d entered at %s"),
				Self->Active.Id, *Self->Active.CurrentRound);
			if (Callback)
			{
				Callback(true, FString());
			}
		});
}

void UWSTournamentSubsystem::SubmitRound(const FWSEventResult& Result, FWSRoundCallback Callback)
{
	UWSOnlineSubsystem* OnlineSubsystem = Online();
	if (!OnlineSubsystem || !Active.IsValid())
	{
		if (Callback)
		{
			Callback(false, FWSTournamentResultDto(), TEXT("No tournament in progress"));
		}
		return;
	}
	const TSharedPtr<FJsonObject> Body = Result.ToRequestJson();
	if (!Body.IsValid())
	{
		if (Callback)
		{
			Callback(false, FWSTournamentResultDto(), TEXT("Result is not serializable"));
		}
		return;
	}

	const FString Path = FString::Printf(TEXT("%s/%d/results"), TournamentsPath, Active.Id);
	TWeakObjectPtr<UWSTournamentSubsystem> WeakThis(this);
	OnlineSubsystem->Request(TEXT("POST"), Path, Body,
		[WeakThis, Callback](const FWSHttpResult& HttpResult)
		{
			UWSTournamentSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			if (!HttpResult.IsSuccess() || !HttpResult.Json.IsValid())
			{
				// A tournament round is NOT queued offline: the bracket is
				// server state and the round must be scored against the field
				// the server stored, so a silent local "success" would be a
				// lie the next refresh would contradict.
				const FString Error = HttpResult.bTransportOk
					? FString::Printf(TEXT("Round not recorded (%d)"), HttpResult.StatusCode)
					: TEXT("No connection — the round was not recorded");
				if (Callback)
				{
					Callback(false, FWSTournamentResultDto(), Error);
				}
				return;
			}
			FWSTournamentResultDto Dto;
			FJsonObjectConverter::JsonObjectToUStruct(HttpResult.Json.ToSharedRef(), &Dto);
			Self->LastRound = Dto;
			// Re-read the bracket so the next round's field is the server's.
			Self->Refresh();
			if (Callback)
			{
				Callback(true, Dto, FString());
			}
		});
}
