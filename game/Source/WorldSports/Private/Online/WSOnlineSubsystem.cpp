#include "Online/WSOnlineSubsystem.h"

#include "Core/WSLog.h"
#include "Core/WSOnlineSettings.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "Online/WSOfflineQueueSave.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
const TCHAR* LoginPath = TEXT("/api/v1/auth/login");
const TCHAR* RegisterPath = TEXT("/api/v1/auth/register");
const TCHAR* MePath = TEXT("/api/v1/auth/me");
const TCHAR* ResultsPath = TEXT("/api/v1/career/results");

FString JsonToString(const TSharedPtr<FJsonObject>& Json)
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
	return Out;
}

TSharedPtr<FJsonObject> StringToJson(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json))
	{
		return nullptr;
	}
	return Json;
}
}

void UWSOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The offline queue is per-account and loads at sign-in (SwitchQueueUser);
	// loading a global queue here is how one account's results would leak
	// into another's session.
}

void UWSOnlineSubsystem::Deinitialize()
{
	for (const FTSTicker::FDelegateHandle& Handle : PendingTimers)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	}
	PendingTimers.Empty();
	Super::Deinitialize();
}

// -- Auth --------------------------------------------------------------------

void UWSOnlineSubsystem::Login(const FString& Email, const FString& Password, FWSUserCallback Callback)
{
	// OAuth2 password form, exactly what FastAPI's OAuth2PasswordRequestForm reads.
	const FString Form = FString::Printf(TEXT("username=%s&password=%s"),
		*FGenericPlatformHttp::UrlEncode(Email),
		*FGenericPlatformHttp::UrlEncode(Password));

	SendRequest(TEXT("POST"), LoginPath,
		TEXT("application/x-www-form-urlencoded"), Form,
		[this, Callback](const FWSHttpResult& Result)
		{
			if (!Result.IsSuccess())
			{
				if (Callback)
				{
					Callback(false, FWSUserDto(),
						Result.bTransportOk
							? FString::Printf(TEXT("Login failed (%d)"), Result.StatusCode)
							: Result.ErrorText);
				}
				return;
			}
			FWSTokenResponse Token;
			if (!Result.Json.IsValid() ||
				!FJsonObjectConverter::JsonObjectToUStruct(Result.Json.ToSharedRef(), &Token) ||
				Token.access_token.IsEmpty())
			{
				if (Callback)
				{
					Callback(false, FWSUserDto(), TEXT("Malformed token response"));
				}
				return;
			}
			AccessToken = Token.access_token;

			// Confirm the token and learn who we are before declaring success.
			Request(TEXT("GET"), MePath, nullptr,
				[this, Callback](const FWSHttpResult& MeResult)
				{
					if (!MeResult.IsSuccess() || !MeResult.Json.IsValid())
					{
						AccessToken.Empty();
						if (Callback)
						{
							Callback(false, FWSUserDto(), TEXT("Token check failed"));
						}
						return;
					}
					FJsonObjectConverter::JsonObjectToUStruct(
						MeResult.Json.ToSharedRef(), &SignedInUser);
					UE_LOG(LogWorldSports, Log, TEXT("Signed in as user %d"), SignedInUser.id);
					SwitchQueueUser(SignedInUser.id);
					OnAuthChanged.Broadcast(true);
					if (Callback)
					{
						Callback(true, SignedInUser, FString());
					}
					FlushOfflineQueue();
				});
		},
		/*bAuthorized=*/false, GetDefault<UWSOnlineSettings>()->MaxRetries);
}

void UWSOnlineSubsystem::RegisterAccount(const FString& Email, const FString& Password,
	const FString& DisplayName, FWSUserCallback Callback)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("email"), Email);
	Body->SetStringField(TEXT("password"), Password);
	Body->SetStringField(TEXT("display_name"), DisplayName);

	SendRequest(TEXT("POST"), RegisterPath, TEXT("application/json"), JsonToString(Body),
		[this, Email, Password, Callback](const FWSHttpResult& Result) mutable
		{
			if (!Result.IsSuccess())
			{
				FString Error = Result.ErrorText;
				if (Result.bTransportOk)
				{
					Error = Result.StatusCode == 409
						? TEXT("An account with this email already exists")
						: FString::Printf(TEXT("Registration failed (%d)"), Result.StatusCode);
				}
				if (Callback)
				{
					Callback(false, FWSUserDto(), Error);
				}
				return;
			}
			Login(Email, Password, MoveTemp(Callback));
		},
		/*bAuthorized=*/false, /*AttemptsLeft=*/0);
}

void UWSOnlineSubsystem::Logout()
{
	if (AccessToken.IsEmpty() && SignedInUser.id == 0)
	{
		return;
	}
	SaveQueueToDisk(); // the queue stays with its owner, on disk
	AccessToken.Empty();
	SignedInUser = FWSUserDto();
	QueueUserId = 0;
	OfflineQueue.Empty();
	QueueCallbacks.Empty();
	OnAuthChanged.Broadcast(false);
}

// -- Results -----------------------------------------------------------------

void UWSOnlineSubsystem::SubmitResult(const FWSEventResult& Result, FWSSubmitCallback Callback)
{
	if (!Result.ToRequestJson().IsValid())
	{
		// Refuse locally what the server would reject anyway (non-finite or
		// empty event) — this is a client bug, not a network problem.
		UE_LOG(LogWorldSports, Error,
			TEXT("SubmitResult refused: result for '%s' is not serializable"), *Result.EventCode);
		if (Callback)
		{
			Callback(EWSSubmitOutcome::Failed, FWSResultResponse(),
				TEXT("Result is not serializable"));
		}
		return;
	}
	if (QueueUserId == 0)
	{
		// No account has ever signed in on this session: there is no owner to
		// queue under, and career results need an athlete anyway.
		if (Callback)
		{
			Callback(EWSSubmitOutcome::Failed, FWSResultResponse(),
				TEXT("Sign in before recording career results"));
		}
		return;
	}

	FWSEventResult Entry = Result;
	if (Entry.ClientRef.IsEmpty())
	{
		Entry.ClientRef = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	// Persist FIRST. If the process dies mid-request the result survives.
	OfflineQueue.Add(MoveTemp(Entry));
	QueueCallbacks.Add(MoveTemp(Callback));
	SaveQueueToDisk();
	FlushOfflineQueue();

	// The "never silence" guarantee: if this entry is not the in-flight head
	// (offline, expired session, or waiting behind older results), its caller
	// hears Queued now instead of nothing.
	const int32 Index = OfflineQueue.Num() - 1;
	const bool bIsInFlightHead = bQueueFlushInFlight && Index == 0;
	if (!bIsInFlightHead && QueueCallbacks.IsValidIndex(Index) && QueueCallbacks[Index])
	{
		FWSSubmitCallback Waiter = MoveTemp(QueueCallbacks[Index]);
		QueueCallbacks[Index] = nullptr;
		Waiter(EWSSubmitOutcome::Queued, FWSResultResponse(), FString());
	}
}

void UWSOnlineSubsystem::FlushOfflineQueue()
{
	if (bQueueFlushInFlight || OfflineQueue.Num() == 0 || !IsSignedIn())
	{
		return;
	}
	bQueueFlushInFlight = true;
	SubmitQueueHead();
}

EWSSubmitDisposition UWSOnlineSubsystem::ClassifyResultStatus(int32 StatusCode)
{
	if (StatusCode >= 200 && StatusCode < 300)
	{
		return EWSSubmitDisposition::Definitive;
	}
	if (StatusCode == 401 || StatusCode == 403)
	{
		// Rejected by the auth dependency BEFORE the submission was processed:
		// nothing was recorded, so keeping the entry cannot double-submit.
		return EWSSubmitDisposition::AuthExpired;
	}
	if (StatusCode == 408 || StatusCode == 429 || StatusCode >= 500)
	{
		// Transient. Resending is safe because replays carry client_ref and
		// the server answers idempotently.
		return EWSSubmitDisposition::Retryable;
	}
	// 400/404/409/422...: this submission can never succeed. Dropping loses
	// nothing the server would ever have accepted.
	return EWSSubmitDisposition::Fatal;
}

void UWSOnlineSubsystem::SubmitQueueHead()
{
	if (OfflineQueue.Num() == 0)
	{
		bQueueFlushInFlight = false;
		return;
	}

	const TSharedPtr<FJsonObject> Body = OfflineQueue[0].ToRequestJson();
	if (!Body.IsValid())
	{
		// Corrupt queue entry (should be impossible; guarded at enqueue).
		FinishQueueHead(EWSSubmitOutcome::Failed, FWSResultResponse(),
			TEXT("Unserializable queued result dropped"));
		return;
	}

	SendRequest(TEXT("POST"), ResultsPath, TEXT("application/json"), JsonToString(Body),
		[this](const FWSHttpResult& Result)
		{
			if (!Result.bTransportOk)
			{
				NotifyAllQueuedAndHalt(Result.ErrorText);
				return;
			}

			switch (ClassifyResultStatus(Result.StatusCode))
			{
			case EWSSubmitDisposition::Definitive:
			{
				FWSResultResponse Response;
				if (Result.Json.IsValid() &&
					FJsonObjectConverter::JsonObjectToUStruct(Result.Json.ToSharedRef(), &Response))
				{
					FinishQueueHead(
						Response.accepted ? EWSSubmitOutcome::Accepted : EWSSubmitOutcome::Rejected,
						Response, Response.rejection_reason);
				}
				else
				{
					// A 2xx we cannot parse is a client/contract bug. Keep the
					// entry: client_ref makes the eventual replay idempotent,
					// so keeping cannot double-submit but dropping loses it.
					UE_LOG(LogWorldSports, Error,
						TEXT("Unparseable %d from results endpoint; keeping entry queued"),
						Result.StatusCode);
					NotifyAllQueuedAndHalt(TEXT("Malformed server response"));
				}
				break;
			}
			case EWSSubmitDisposition::AuthExpired:
				// The token died mid-session. Keep the queue (it belongs to
				// SignedInUser, who is unchanged), drop the token, and tell
				// the UI to re-authenticate.
				UE_LOG(LogWorldSports, Warning,
					TEXT("Session expired (%d); %d result(s) held for re-auth"),
					Result.StatusCode, OfflineQueue.Num());
				AccessToken.Empty();
				OnAuthChanged.Broadcast(false);
				NotifyAllQueuedAndHalt(TEXT("Session expired"));
				break;
			case EWSSubmitDisposition::Retryable:
				NotifyAllQueuedAndHalt(
					FString::Printf(TEXT("Server busy (%d)"), Result.StatusCode));
				break;
			case EWSSubmitDisposition::Fatal:
			default:
				FinishQueueHead(EWSSubmitOutcome::Failed, FWSResultResponse(),
					FString::Printf(TEXT("Server answered %d: %s"), Result.StatusCode, *Result.Body));
				break;
			}
		},
		/*bAuthorized=*/true, /*AttemptsLeft=*/0);
}

void UWSOnlineSubsystem::NotifyAllQueuedAndHalt(const FString& Reason)
{
	bQueueFlushInFlight = false;
	for (FWSSubmitCallback& Waiter : QueueCallbacks)
	{
		if (Waiter)
		{
			FWSSubmitCallback Once = MoveTemp(Waiter);
			Waiter = nullptr;
			Once(EWSSubmitOutcome::Queued, FWSResultResponse(), Reason);
		}
	}
}

void UWSOnlineSubsystem::FinishQueueHead(EWSSubmitOutcome Outcome,
	const FWSResultResponse& Response, const FString& Error)
{
	FWSSubmitCallback Callback;
	if (QueueCallbacks.Num() > 0)
	{
		Callback = MoveTemp(QueueCallbacks[0]);
		QueueCallbacks.RemoveAt(0);
	}
	if (OfflineQueue.Num() > 0)
	{
		OfflineQueue.RemoveAt(0);
	}
	SaveQueueToDisk();

	if (Outcome == EWSSubmitOutcome::Accepted || Outcome == EWSSubmitOutcome::Rejected)
	{
		OnResultSubmitted.Broadcast(Response, Outcome == EWSSubmitOutcome::Accepted);
	}
	else if (!Error.IsEmpty())
	{
		UE_LOG(LogWorldSports, Warning, TEXT("Result submission failed: %s"), *Error);
	}
	if (Callback)
	{
		Callback(Outcome, Response, Error);
	}
	SubmitQueueHead();
}

// -- Queue persistence -------------------------------------------------------

TArray<FString> UWSOnlineSubsystem::SerializeQueue(const TArray<FWSEventResult>& Queue)
{
	TArray<FString> Out;
	Out.Reserve(Queue.Num());
	for (const FWSEventResult& Result : Queue)
	{
		const TSharedPtr<FJsonObject> Json = Result.ToRequestJson();
		if (Json.IsValid())
		{
			Out.Add(JsonToString(Json));
		}
	}
	return Out;
}

TArray<FWSEventResult> UWSOnlineSubsystem::DeserializeQueue(const TArray<FString>& Serialized)
{
	TArray<FWSEventResult> Out;
	for (const FString& Entry : Serialized)
	{
		FWSEventResult Result;
		if (FWSEventResult::FromRequestJson(StringToJson(Entry), Result))
		{
			Out.Add(MoveTemp(Result));
		}
	}
	return Out;
}

FString UWSOnlineSubsystem::QueueSlotForUser(int32 UserId) const
{
	return FString::Printf(TEXT("WSOfflineQueue_u%d"), UserId);
}

void UWSOnlineSubsystem::SaveQueueToDisk() const
{
	if (QueueUserId == 0)
	{
		return;
	}
	UWSOfflineQueueSave* Save = Cast<UWSOfflineQueueSave>(
		UGameplayStatics::CreateSaveGameObject(UWSOfflineQueueSave::StaticClass()));
	Save->SerializedResults = SerializeQueue(OfflineQueue);
	if (!UGameplayStatics::SaveGameToSlot(Save, QueueSlotForUser(QueueUserId), 0))
	{
		UE_LOG(LogWorldSports, Error, TEXT("Could not persist offline queue (%d entries)"),
			OfflineQueue.Num());
	}
}

void UWSOnlineSubsystem::SwitchQueueUser(int32 UserId)
{
	if (UserId == QueueUserId)
	{
		return;
	}
	if (QueueUserId != 0)
	{
		SaveQueueToDisk(); // previous owner keeps their pending results
		// Their waiters can no longer receive a server outcome this session.
		NotifyAllQueuedAndHalt(TEXT("Account changed"));
	}
	QueueUserId = UserId;
	OfflineQueue.Empty();
	QueueCallbacks.Empty();

	const FString Slot = QueueSlotForUser(UserId);
	if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		if (const UWSOfflineQueueSave* Save = Cast<UWSOfflineQueueSave>(
				UGameplayStatics::LoadGameFromSlot(Slot, 0)))
		{
			OfflineQueue = DeserializeQueue(Save->SerializedResults);
			QueueCallbacks.Init(nullptr, OfflineQueue.Num());
			if (OfflineQueue.Num() > 0)
			{
				UE_LOG(LogWorldSports, Log,
					TEXT("Offline queue for user %d restored with %d pending result(s)"),
					UserId, OfflineQueue.Num());
			}
		}
	}
}

// -- Transport ---------------------------------------------------------------

void UWSOnlineSubsystem::Request(const FString& Verb, const FString& Path,
	const TSharedPtr<FJsonObject>& Body, FWSHttpCallback Callback, bool bAuthorized)
{
	const bool bIdempotent = Verb == TEXT("GET") || Verb == TEXT("PUT");
	SendRequest(Verb, Path, TEXT("application/json"),
		Body.IsValid() ? JsonToString(Body) : FString(),
		MoveTemp(Callback), bAuthorized,
		bIdempotent ? GetDefault<UWSOnlineSettings>()->MaxRetries : 0);
}

void UWSOnlineSubsystem::SendRequest(const FString& Verb, const FString& Path,
	const FString& ContentType, const FString& Content,
	FWSHttpCallback Callback, bool bAuthorized, int32 AttemptsLeft)
{
	const UWSOnlineSettings* Settings = GetDefault<UWSOnlineSettings>();

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
		FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Settings->BaseUrl + Path);
	HttpRequest->SetVerb(Verb);
	HttpRequest->SetTimeout(Settings->RequestTimeoutSeconds);
	if (!Content.IsEmpty())
	{
		HttpRequest->SetHeader(TEXT("Content-Type"), ContentType);
		HttpRequest->SetContentAsString(Content);
	}
	if (bAuthorized && !AccessToken.IsEmpty())
	{
		HttpRequest->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	}

	HttpRequest->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Verb, Path, ContentType, Content, Callback, bAuthorized, AttemptsLeft](
			FHttpRequestPtr, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
		{
			FWSHttpResult Result;
			if (bConnectedSuccessfully && HttpResponse.IsValid())
			{
				Result.bTransportOk = true;
				Result.StatusCode = HttpResponse->GetResponseCode();
				Result.Body = HttpResponse->GetContentAsString();
				Result.Json = StringToJson(Result.Body);
			}
			else
			{
				Result.ErrorText = TEXT("No connection to the server");
			}

			const bool bRetryable = !Result.bTransportOk || Result.StatusCode >= 500;
			if (bRetryable && AttemptsLeft > 0)
			{
				ScheduleRetry(
					[this, Verb, Path, ContentType, Content, Callback, bAuthorized, AttemptsLeft]()
					{
						SendRequest(Verb, Path, ContentType, Content,
							Callback, bAuthorized, AttemptsLeft - 1);
					},
					AttemptsLeft);
				return;
			}
			if (Callback)
			{
				Callback(Result);
			}
		});
	HttpRequest->ProcessRequest();
}

void UWSOnlineSubsystem::ScheduleRetry(TFunction<void()> Retry, int32 AttemptsLeft)
{
	const UWSOnlineSettings* Settings = GetDefault<UWSOnlineSettings>();
	const int32 AttemptIndex = FMath::Max(0, Settings->MaxRetries - AttemptsLeft);
	const float Delay = Settings->InitialBackoffSeconds * static_cast<float>(1 << AttemptIndex);

	const FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this,
			[Retry = MoveTemp(Retry)](float) -> bool
			{
				Retry();
				return false; // one-shot
			}),
		Delay);
	PendingTimers.Add(Handle);
}
