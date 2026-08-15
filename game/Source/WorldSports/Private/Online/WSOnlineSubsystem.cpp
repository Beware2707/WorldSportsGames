#include "Online/WSOnlineSubsystem.h"

#include "Core/WSLog.h"
#include "Core/WSOnlineSettings.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
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
const TCHAR* QueueSlotName = TEXT("WSOfflineQueue");

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
	LoadQueueFromDisk();
	if (OfflineQueue.Num() > 0)
	{
		UE_LOG(LogWorldSports, Log,
			TEXT("Offline queue restored with %d pending result(s)"), OfflineQueue.Num());
	}
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
	if (AccessToken.IsEmpty())
	{
		return;
	}
	AccessToken.Empty();
	SignedInUser = FWSUserDto();
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

	// Persist FIRST. If the process dies mid-request the result survives.
	OfflineQueue.Add(Result);
	QueueCallbacks.Add(MoveTemp(Callback));
	SaveQueueToDisk();
	FlushOfflineQueue();
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
				// Still offline: keep the head queued and stop; the next
				// login/reconnect flush will retry it.
				bQueueFlushInFlight = false;
				if (QueueCallbacks.Num() > 0 && QueueCallbacks[0])
				{
					QueueCallbacks[0](EWSSubmitOutcome::Queued, FWSResultResponse(), Result.ErrorText);
					QueueCallbacks[0] = nullptr; // report Queued only once
				}
				return;
			}

			FWSResultResponse Response;
			if (Result.StatusCode == 201 && Result.Json.IsValid() &&
				FJsonObjectConverter::JsonObjectToUStruct(Result.Json.ToSharedRef(), &Response))
			{
				FinishQueueHead(
					Response.accepted ? EWSSubmitOutcome::Accepted : EWSSubmitOutcome::Rejected,
					Response, Response.rejection_reason);
			}
			else
			{
				// A definitive server answer (4xx/5xx). Dropping the entry is
				// deliberate: the server SAW the submission; blind resubmission
				// of e.g. a rate-limited result would double-submit.
				FinishQueueHead(EWSSubmitOutcome::Failed, FWSResultResponse(),
					FString::Printf(TEXT("Server answered %d: %s"), Result.StatusCode, *Result.Body));
			}
		},
		/*bAuthorized=*/true, /*AttemptsLeft=*/0);
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

void UWSOnlineSubsystem::SaveQueueToDisk() const
{
	UWSOfflineQueueSave* Save = Cast<UWSOfflineQueueSave>(
		UGameplayStatics::CreateSaveGameObject(UWSOfflineQueueSave::StaticClass()));
	Save->SerializedResults = SerializeQueue(OfflineQueue);
	if (!UGameplayStatics::SaveGameToSlot(Save, QueueSlotName, 0))
	{
		UE_LOG(LogWorldSports, Error, TEXT("Could not persist offline queue (%d entries)"),
			OfflineQueue.Num());
	}
}

void UWSOnlineSubsystem::LoadQueueFromDisk()
{
	if (!UGameplayStatics::DoesSaveGameExist(QueueSlotName, 0))
	{
		return;
	}
	if (const UWSOfflineQueueSave* Save = Cast<UWSOfflineQueueSave>(
			UGameplayStatics::LoadGameFromSlot(QueueSlotName, 0)))
	{
		OfflineQueue = DeserializeQueue(Save->SerializedResults);
		QueueCallbacks.Init(nullptr, OfflineQueue.Num());
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
