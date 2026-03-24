#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "HttpResultCallback.h"
#include "TimerManager.h"
#include "TTSWebhookComponent.generated.h"

class IHttpRouter;
class FJsonObject;
class UACEAudioCurveSourceComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTTSUtteranceEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FACEPrewarmCompletedEvent);

UENUM()
enum class ETTSPlaybackState : uint8
{
    Idle,
    StreamingToACE,
    WaitingForAnimationEnd
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class AVATARVR_API UTTSWebhookComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UTTSWebhookComponent();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|HTTP", meta = (ClampMin = "1", ClampMax = "65535"))
    int32 ListenPort = 7777;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|HTTP")
    FString AudioSamplesRoute = TEXT("/ace/audio_samples");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACK")
    bool bAutoSendAckToPython = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACK")
    FString AckUrl = TEXT("http://127.0.0.1:9998/playback_done");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACE")
    TObjectPtr<UACEAudioCurveSourceComponent> AceConsumer = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACE")
    FName A2FProviderName = FName("Default");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACE|Prewarm")
    bool bEnableACEPrewarm = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACE|Prewarm", meta = (ClampMin = "0.0"))
    float ACEPrewarmDelaySeconds = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACE|Prewarm", meta = (ClampMin = "1"))
    int32 ACEPrewarmDurationMs = 100;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACE|Prewarm", meta = (ClampMin = "1"))
    int32 ACEPrewarmSampleRate = 24000;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACE|Prewarm", meta = (ClampMin = "1"))
    int32 ACEPrewarmNumChannels = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACE|Prewarm", meta = (ClampMin = "1"))
    int32 ACEPrewarmMaxAttempts = 3;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TTS|ACE|Prewarm", meta = (ClampMin = "0.0"))
    float ACEPrewarmRetryDelaySeconds = 1.0f;

    UPROPERTY(BlueprintAssignable, Category = "TTS|Events")
    FTTSUtteranceEvent OnUtteranceStarted;

    UPROPERTY(BlueprintAssignable, Category = "TTS|Events")
    FTTSUtteranceEvent OnUtteranceCompleted;

    UPROPERTY(BlueprintAssignable, Category = "TTS|Events")
    FACEPrewarmCompletedEvent OnACEPrewarmCompleted;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    TSharedPtr<IHttpRouter> Router;
    FHttpRouteHandle RouteHandle;

    FCriticalSection StateMutex;

    ETTSPlaybackState PlaybackState = ETTSPlaybackState::Idle;
    FString CurrentTraceId;
    FString CurrentUtteranceId;
    bool bSawLastChunk = false;

    FTimerHandle ACEPrewarmTimerHandle;
    FTimerHandle ACEPrewarmCompletionFallbackTimerHandle;
    int32 ACEPrewarmAttemptCount = 0;
    bool bACEPrewarmSent = false;
    bool bAwaitingACEPrewarmEnded = false;

    void AutoResolveAceConsumer();

    void ScheduleACEPrewarm(float DelaySeconds);
    void ScheduleACEPrewarmCompletionFallback(float DelaySeconds);
    void ExecuteACEPrewarm();
    void CompleteACEPrewarmIfPending();

    void ResetPlaybackState_Locked();

    bool SendSamplesToACE(const TArray<float>& Samples, int32 NumChannels, int32 SampleRate, bool bEndOfSamples);
    void SendAckNow(const FString& TraceId, const FString& UtteranceId);

    bool HandleAudioSamplesRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

    UFUNCTION()
    void HandleAceAnimationEnded();

    static bool JsonTryGetStringAny(const TSharedPtr<FJsonObject>& Obj, const TCHAR* KeyA, const TCHAR* KeyB, FString& Out);
    static bool JsonTryGetIntAny(const TSharedPtr<FJsonObject>& Obj, const TCHAR* KeyA, const TCHAR* KeyB, int32& Out);
    static bool JsonTryGetBoolAny(const TSharedPtr<FJsonObject>& Obj, const TCHAR* KeyA, const TCHAR* KeyB, bool& Out);
};