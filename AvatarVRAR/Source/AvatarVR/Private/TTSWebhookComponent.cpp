#include "TTSWebhookComponent.h"

#include "HTTPServerModule.h"
#include "IHttpRouter.h"
#include "HttpRequestHandler.h"
#include "HttpServerResponse.h"
#include "HttpPath.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Misc/Base64.h"
#include "Misc/ScopeLock.h"
#include "Containers/StringConv.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "ACEAudioSamplesLibrary.h"
#include "ACEBlueprintLibrary.h"
#include "ACEAudioCurveSourceComponent.h"

UTTSWebhookComponent::UTTSWebhookComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UTTSWebhookComponent::BeginPlay()
{
    Super::BeginPlay();

    AutoResolveAceConsumer();

    if (!AceConsumer)
    {
        UE_LOG(LogTemp, Error, TEXT("[TTSWebhook] AceConsumer nullo: impossibile avviare il componente."));
        return;
    }

    AceConsumer->OnAnimationEnded.AddDynamic(this, &UTTSWebhookComponent::HandleAceAnimationEnded);

    FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
    Router = HttpServerModule.GetHttpRouter(ListenPort);

    if (!Router.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[TTSWebhook] HttpRouter non valido (port %d)."), ListenPort);
        return;
    }

    const FHttpPath Path(AudioSamplesRoute);
    RouteHandle = Router->BindRoute(
        Path,
        EHttpServerRequestVerbs::VERB_POST,
        FHttpRequestHandler::CreateUObject(this, &UTTSWebhookComponent::HandleAudioSamplesRequest)
    );

    HttpServerModule.StartAllListeners();

    UE_LOG(LogTemp, Log, TEXT("[TTSWebhook] In ascolto: http://127.0.0.1:%d%s"), ListenPort, *AudioSamplesRoute);

    const TArray<FName> Providers = UACEBlueprintLibrary::GetAvailableA2FProviderNames();
    FString Joined;
    for (const FName& Name : Providers)
    {
        if (!Joined.IsEmpty())
        {
            Joined += TEXT(", ");
        }
        Joined += Name.ToString();
    }

    UE_LOG(LogTemp, Log, TEXT("[TTSWebhook] Available A2F providers: %s"), *Joined);
    UE_LOG(LogTemp, Log, TEXT("[TTSWebhook] Selected A2F provider: %s"), *A2FProviderName.ToString());

    if (bEnableACEPrewarm)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[TTSWebhook] ACE prewarm schedulato: delay=%.3fs duration=%dms sr=%d ch=%d attempts=%d"),
            ACEPrewarmDelaySeconds,
            ACEPrewarmDurationMs,
            ACEPrewarmSampleRate,
            ACEPrewarmNumChannels,
            ACEPrewarmMaxAttempts
        );

        ScheduleACEPrewarm(ACEPrewarmDelaySeconds);
    }
}

void UTTSWebhookComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(ACEPrewarmTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(ACEPrewarmCompletionFallbackTimerHandle);
    }

    if (AceConsumer)
    {
        AceConsumer->OnAnimationEnded.RemoveDynamic(this, &UTTSWebhookComponent::HandleAceAnimationEnded);
    }

    if (Router.IsValid() && RouteHandle.IsValid())
    {
        Router->UnbindRoute(RouteHandle);
    }

    Router.Reset();

    Super::EndPlay(EndPlayReason);
}

void UTTSWebhookComponent::AutoResolveAceConsumer()
{
    if (AceConsumer)
    {
        UE_LOG(LogTemp, Log, TEXT("[TTSWebhook] AceConsumer già impostato: %s"), *AceConsumer->GetName());
        return;
    }

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TTSWebhook] GetOwner() nullo: impossibile risolvere AceConsumer."));
        return;
    }

    AceConsumer = Owner->FindComponentByClass<UACEAudioCurveSourceComponent>();

    if (AceConsumer)
    {
        UE_LOG(LogTemp, Log, TEXT("[TTSWebhook] AceConsumer auto-bound: %s"), *AceConsumer->GetName());
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[TTSWebhook] Nessun UACEAudioCurveSourceComponent trovato su %s"), *Owner->GetName());
}

void UTTSWebhookComponent::ScheduleACEPrewarm(float DelaySeconds)
{
    if (!bEnableACEPrewarm || bACEPrewarmSent)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TTSWebhook] ScheduleACEPrewarm fallita: World nullo."));
        return;
    }

    World->GetTimerManager().ClearTimer(ACEPrewarmTimerHandle);
    World->GetTimerManager().SetTimer(
        ACEPrewarmTimerHandle,
        this,
        &UTTSWebhookComponent::ExecuteACEPrewarm,
        FMath::Max(0.0f, DelaySeconds),
        false
    );
}

void UTTSWebhookComponent::ScheduleACEPrewarmCompletionFallback(float DelaySeconds)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    World->GetTimerManager().ClearTimer(ACEPrewarmCompletionFallbackTimerHandle);
    World->GetTimerManager().SetTimer(
        ACEPrewarmCompletionFallbackTimerHandle,
        this,
        &UTTSWebhookComponent::CompleteACEPrewarmIfPending,
        FMath::Max(0.0f, DelaySeconds),
        false
    );
}

void UTTSWebhookComponent::ExecuteACEPrewarm()
{
    if (!bEnableACEPrewarm || bACEPrewarmSent)
    {
        return;
    }

    if (!AceConsumer)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TTSWebhook] ACE prewarm saltato: AceConsumer nullo."));
        return;
    }

    {
        FScopeLock Lock(&StateMutex);

        if (PlaybackState != ETTSPlaybackState::Idle)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[TTSWebhook] ACE prewarm rimandato: playback state=%d"),
                static_cast<int32>(PlaybackState)
            );

            if (ACEPrewarmAttemptCount < ACEPrewarmMaxAttempts)
            {
                ScheduleACEPrewarm(ACEPrewarmRetryDelaySeconds);
            }

            return;
        }
    }

    ++ACEPrewarmAttemptCount;

    const int32 SafeChannels = FMath::Max(1, ACEPrewarmNumChannels);
    const int32 SafeSampleRate = FMath::Max(1, ACEPrewarmSampleRate);
    const int32 SafeDurationMs = FMath::Max(1, ACEPrewarmDurationMs);

    const int32 NumSamples = FMath::Max(
        1,
        FMath::RoundToInt(
            (static_cast<float>(SafeDurationMs) / 1000.0f) *
            static_cast<float>(SafeSampleRate) *
            static_cast<float>(SafeChannels)
        )
    );

    TArray<float> Silence;
    Silence.Init(0.0f, NumSamples);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[TTSWebhook] ACE prewarm attempt %d/%d: invio %d sample di silenzio (%dms @ %dHz ch=%d)"),
        ACEPrewarmAttemptCount,
        ACEPrewarmMaxAttempts,
        NumSamples,
        SafeDurationMs,
        SafeSampleRate,
        SafeChannels
    );

    const bool bOk = SendSamplesToACE(
        Silence,
        SafeChannels,
        SafeSampleRate,
        true
    );

    if (bOk)
    {
        {
            FScopeLock Lock(&StateMutex);
            bACEPrewarmSent = true;
            bAwaitingACEPrewarmEnded = true;
        }

        const float CompletionFallbackDelaySeconds = FMath::Max(
            0.10f,
            (static_cast<float>(SafeDurationMs) / 1000.0f) + 0.10f
        );

        ScheduleACEPrewarmCompletionFallback(CompletionFallbackDelaySeconds);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[TTSWebhook] ACE prewarm inviato con successo. Fallback ready in %.3fs"),
            CompletionFallbackDelaySeconds
        );
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[TTSWebhook] ACE prewarm fallito al tentativo %d."), ACEPrewarmAttemptCount);

        if (ACEPrewarmAttemptCount < ACEPrewarmMaxAttempts)
        {
            ScheduleACEPrewarm(ACEPrewarmRetryDelaySeconds);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[TTSWebhook] ACE prewarm esaurito dopo %d tentativi."), ACEPrewarmAttemptCount);
        }
    }
}

void UTTSWebhookComponent::ResetPlaybackState_Locked()
{
    PlaybackState = ETTSPlaybackState::Idle;
    CurrentTraceId.Reset();
    CurrentUtteranceId.Reset();
    bSawLastChunk = false;
}

void UTTSWebhookComponent::CompleteACEPrewarmIfPending()
{
    bool bShouldBroadcast = false;

    {
        FScopeLock Lock(&StateMutex);

        if (!bAwaitingACEPrewarmEnded)
        {
            return;
        }

        bAwaitingACEPrewarmEnded = false;
        bShouldBroadcast = true;
    }

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(ACEPrewarmCompletionFallbackTimerHandle);
    }

    if (bShouldBroadcast)
    {
        UE_LOG(LogTemp, Log, TEXT("[TTSWebhook] ACE prewarm completed."));
        OnACEPrewarmCompleted.Broadcast();
    }
}

bool UTTSWebhookComponent::HandleAudioSamplesRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    FString BodyStr;
    if (Request.Body.Num() > 0)
    {
        FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
        BodyStr = FString(Converter.Length(), Converter.Get());
    }

    TSharedPtr<FJsonObject> Obj;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);

    if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[TTSWebhook] bad_json, body_bytes=%d"), Request.Body.Num());

        TUniquePtr<FHttpServerResponse> Resp =
            FHttpServerResponse::Create(TEXT("{\"ok\":false,\"err\":\"bad_json\"}"), TEXT("application/json"));
        OnComplete(MoveTemp(Resp));
        return true;
    }

    FString TraceId, UttId, SamplesB64;
    int32 ChunkIndex = 0;
    int32 ChunkTotal = 0;
    int32 SampleRate = 22050;
    int32 NumChannels = 1;
    bool bIsLast = false;

    JsonTryGetStringAny(Obj, TEXT("trace_id"), TEXT("trace"), TraceId);
    JsonTryGetStringAny(Obj, TEXT("utterance_id"), TEXT("utt"), UttId);
    JsonTryGetIntAny(Obj, TEXT("chunk_index"), TEXT("chunkIndex"), ChunkIndex);
    JsonTryGetIntAny(Obj, TEXT("chunk_total"), TEXT("chunkTotal"), ChunkTotal);
    JsonTryGetBoolAny(Obj, TEXT("is_last"), TEXT("isLast"), bIsLast);
    JsonTryGetIntAny(Obj, TEXT("sample_rate"), TEXT("sampleRate"), SampleRate);
    JsonTryGetIntAny(Obj, TEXT("num_channels"), TEXT("numChannels"), NumChannels);
    JsonTryGetStringAny(Obj, TEXT("samples_b64"), TEXT("samplesB64"), SamplesB64);

    if (TraceId.IsEmpty() || UttId.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[TTSWebhook] missing trace/utt"));

        TUniquePtr<FHttpServerResponse> Resp =
            FHttpServerResponse::Create(TEXT("{\"ok\":false,\"err\":\"missing_trace_or_utt\"}"), TEXT("application/json"));
        OnComplete(MoveTemp(Resp));
        return true;
    }

    if (SamplesB64.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[TTSWebhook] missing_samples_b64 trace=%s utt=%s"), *TraceId, *UttId);

        TUniquePtr<FHttpServerResponse> Resp =
            FHttpServerResponse::Create(TEXT("{\"ok\":false,\"err\":\"missing_samples_b64\"}"), TEXT("application/json"));
        OnComplete(MoveTemp(Resp));
        return true;
    }

    if (!AceConsumer)
    {
        UE_LOG(LogTemp, Error, TEXT("[TTSWebhook] no_ace_consumer trace=%s utt=%s"), *TraceId, *UttId);

        TUniquePtr<FHttpServerResponse> Resp =
            FHttpServerResponse::Create(TEXT("{\"ok\":false,\"err\":\"no_ace_consumer\"}"), TEXT("application/json"));
        OnComplete(MoveTemp(Resp));
        return true;
    }

    TArray<uint8> Raw;
    if (!FBase64::Decode(SamplesB64, Raw) || Raw.Num() < 4 || (Raw.Num() % 4) != 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[TTSWebhook] bad_base64 trace=%s utt=%s raw_bytes=%d"), *TraceId, *UttId, Raw.Num());

        TUniquePtr<FHttpServerResponse> Resp =
            FHttpServerResponse::Create(TEXT("{\"ok\":false,\"err\":\"bad_base64\"}"), TEXT("application/json"));
        OnComplete(MoveTemp(Resp));
        return true;
    }

    const int32 NumFloats = Raw.Num() / static_cast<int32>(sizeof(float));

    TArray<float> Samples;
    Samples.SetNumUninitialized(NumFloats);
    FMemory::Memcpy(Samples.GetData(), Raw.GetData(), Raw.Num());

    bool bBroadcastStarted = false;

    {
        FScopeLock Lock(&StateMutex);

        const bool bIdle = (PlaybackState == ETTSPlaybackState::Idle);
        const bool bSameUtterance = (CurrentTraceId == TraceId && CurrentUtteranceId == UttId);

        if (bIdle)
        {
            CurrentTraceId = TraceId;
            CurrentUtteranceId = UttId;
            bSawLastChunk = false;
            PlaybackState = ETTSPlaybackState::StreamingToACE;
            bBroadcastStarted = true;
            bAwaitingACEPrewarmEnded = false;

            UE_LOG(LogTemp, Log, TEXT("[TTSWebhook] New utterance trace=%s utt=%s total=%d"), *TraceId, *UttId, ChunkTotal);
        }
        else if (!bSameUtterance)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[TTSWebhook] busy: nuova utterance rifiutata trace=%s utt=%s mentre è attiva trace=%s utt=%s"),
                *TraceId,
                *UttId,
                *CurrentTraceId,
                *CurrentUtteranceId
            );

            TUniquePtr<FHttpServerResponse> Resp =
                FHttpServerResponse::Create(TEXT("{\"ok\":false,\"err\":\"busy\"}"), TEXT("application/json"));
            OnComplete(MoveTemp(Resp));
            return true;
        }
        else if (PlaybackState == ETTSPlaybackState::WaitingForAnimationEnd)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[TTSWebhook] chunk ignorato dopo VV_DONE trace=%s utt=%s chunk=%d"),
                *TraceId,
                *UttId,
                ChunkIndex
            );

            TUniquePtr<FHttpServerResponse> Resp =
                FHttpServerResponse::Create(TEXT("{\"ok\":false,\"err\":\"already_waiting_final_ack\"}"), TEXT("application/json"));
            OnComplete(MoveTemp(Resp));
            return true;
        }
    }

    if (bBroadcastStarted)
    {
        OnUtteranceStarted.Broadcast();
    }

    const bool bOk = SendSamplesToACE(
        Samples,
        FMath::Max(1, NumChannels),
        FMath::Max(1, SampleRate),
        bIsLast
    );

    if (!bOk)
    {
        {
            FScopeLock Lock(&StateMutex);
            ResetPlaybackState_Locked();
        }

        UE_LOG(LogTemp, Error, TEXT("[TTSWebhook] ace_send_failed trace=%s utt=%s chunk=%d"), *TraceId, *UttId, ChunkIndex);

        TUniquePtr<FHttpServerResponse> Resp =
            FHttpServerResponse::Create(TEXT("{\"ok\":false,\"err\":\"ace_send_failed\"}"), TEXT("application/json"));
        OnComplete(MoveTemp(Resp));
        return true;
    }

    if (bIsLast)
    {
        FScopeLock Lock(&StateMutex);

        bSawLastChunk = true;
        PlaybackState = ETTSPlaybackState::WaitingForAnimationEnd;

        UE_LOG(LogTemp, Log, TEXT("[TTSWebhook] VV_DONE trace=%s utt=%s chunk=%d total=%d"), *TraceId, *UttId, ChunkIndex, ChunkTotal);
    }

    TUniquePtr<FHttpServerResponse> Resp =
        FHttpServerResponse::Create(TEXT("{\"ok\":true,\"forwarded\":true}"), TEXT("application/json"));
    OnComplete(MoveTemp(Resp));
    return true;
}

bool UTTSWebhookComponent::SendSamplesToACE(const TArray<float>& Samples, int32 NumChannels, int32 SampleRate, bool bEndOfSamples)
{
    if (!AceConsumer)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TTSWebhook] SendSamplesToACE fallita: AceConsumer nullo."));
        return false;
    }

    const bool bOk = UACEAudioSamplesLibrary::AnimateFromAudioSamplesFloat(
        AceConsumer,
        Samples,
        NumChannels,
        SampleRate,
        bEndOfSamples,
        A2FProviderName
    );

    if (!bOk)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[TTSWebhook] AnimateFromAudioSamplesFloat fallita provider=%s end=%d samples=%d sr=%d ch=%d"),
            *A2FProviderName.ToString(),
            bEndOfSamples ? 1 : 0,
            Samples.Num(),
            SampleRate,
            NumChannels
        );
    }

    return bOk;
}

void UTTSWebhookComponent::HandleAceAnimationEnded()
{
    bool bShouldBroadcastPrewarm = false;
    FString FinishedTraceId;
    FString FinishedUtteranceId;
    bool bShouldAck = false;

    {
        FScopeLock Lock(&StateMutex);

        if (bAwaitingACEPrewarmEnded && PlaybackState == ETTSPlaybackState::Idle && CurrentUtteranceId.IsEmpty())
        {
            bAwaitingACEPrewarmEnded = false;
            bShouldBroadcastPrewarm = true;
        }
        else if (PlaybackState != ETTSPlaybackState::WaitingForAnimationEnd || !bSawLastChunk)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[TTSWebhook] OnAnimationEnded ignorato: non siamo in finale."));
            return;
        }
        else
        {
            FinishedTraceId = CurrentTraceId;
            FinishedUtteranceId = CurrentUtteranceId;

            UE_LOG(LogTemp, Log, TEXT("[TTSWebhook] OnAnimationEnded utt=%s -> FINAL ACK"), *FinishedUtteranceId);

            ResetPlaybackState_Locked();
            bShouldAck = bAutoSendAckToPython;
        }
    }

    if (bShouldBroadcastPrewarm)
    {
        if (GetWorld())
        {
            GetWorld()->GetTimerManager().ClearTimer(ACEPrewarmCompletionFallbackTimerHandle);
        }

        UE_LOG(LogTemp, Log, TEXT("[TTSWebhook] ACE prewarm completed (animation ended)."));
        OnACEPrewarmCompleted.Broadcast();
        return;
    }

    OnUtteranceCompleted.Broadcast();

    if (bShouldAck)
    {
        SendAckNow(FinishedTraceId, FinishedUtteranceId);
    }
}

void UTTSWebhookComponent::SendAckNow(const FString& TraceId, const FString& UtteranceId)
{
    if (AckUrl.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[TTSWebhook] AckUrl vuoto: ACK non inviato."));
        return;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(AckUrl);
    Req->SetVerb(TEXT("POST"));
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));

    const FString Payload = FString::Printf(
        TEXT("{")
        TEXT("\"event\":\"playback_done\",")
        TEXT("\"trace\":\"%s\",")
        TEXT("\"utt\":\"%s\",")
        TEXT("\"trace_id\":\"%s\",")
        TEXT("\"utterance_id\":\"%s\"")
        TEXT("}"),
        *TraceId,
        *UtteranceId,
        *TraceId,
        *UtteranceId
    );

    Req->SetContentAsString(Payload);

    Req->OnProcessRequestComplete().BindLambda(
        [TraceId, UtteranceId](FHttpRequestPtr, FHttpResponsePtr Resp, bool bSucceeded)
        {
            if (!bSucceeded || !Resp.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("[TTSWebhook] ACK failed (no response) trace=%s utt=%s"), *TraceId, *UtteranceId);
                return;
            }

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[TTSWebhook] ACK -> %d body=%s trace=%s utt=%s"),
                Resp->GetResponseCode(),
                *Resp->GetContentAsString(),
                *TraceId,
                *UtteranceId
            );
        }
    );

    Req->ProcessRequest();
}

bool UTTSWebhookComponent::JsonTryGetStringAny(const TSharedPtr<FJsonObject>& Obj, const TCHAR* KeyA, const TCHAR* KeyB, FString& Out)
{
    if (!Obj.IsValid())
    {
        return false;
    }

    if (Obj->HasField(KeyA))
    {
        Out = Obj->GetStringField(KeyA);
        return true;
    }

    if (Obj->HasField(KeyB))
    {
        Out = Obj->GetStringField(KeyB);
        return true;
    }

    return false;
}

bool UTTSWebhookComponent::JsonTryGetIntAny(const TSharedPtr<FJsonObject>& Obj, const TCHAR* KeyA, const TCHAR* KeyB, int32& Out)
{
    if (!Obj.IsValid())
    {
        return false;
    }

    if (Obj->HasField(KeyA))
    {
        Out = static_cast<int32>(Obj->GetIntegerField(KeyA));
        return true;
    }

    if (Obj->HasField(KeyB))
    {
        Out = static_cast<int32>(Obj->GetIntegerField(KeyB));
        return true;
    }

    return false;
}

bool UTTSWebhookComponent::JsonTryGetBoolAny(const TSharedPtr<FJsonObject>& Obj, const TCHAR* KeyA, const TCHAR* KeyB, bool& Out)
{
    if (!Obj.IsValid())
    {
        return false;
    }

    if (Obj->HasField(KeyA))
    {
        Out = Obj->GetBoolField(KeyA);
        return true;
    }

    if (Obj->HasField(KeyB))
    {
        Out = Obj->GetBoolField(KeyB);
        return true;
    }

    return false;
}