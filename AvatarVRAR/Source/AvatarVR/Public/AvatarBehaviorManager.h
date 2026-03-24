#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AvatarBehaviorManager.generated.h"

class UTTSWebhookComponent;

UENUM(BlueprintType)
enum class EAvatarActivityState : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	Listening UMETA(DisplayName = "Listening"),
	Thinking UMETA(DisplayName = "Thinking"),
	Speaking UMETA(DisplayName = "Speaking"),
	Moving UMETA(DisplayName = "Moving"),
	PerformingAction UMETA(DisplayName = "PerformingAction")
};

UCLASS()
class AVATARVR_API AAvatarBehaviorManager : public AActor
{
	GENERATED_BODY()

public:
	AAvatarBehaviorManager();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "References")
	TObjectPtr<AActor> AvatarActor = nullptr;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "References")
	TObjectPtr<AActor> UserActor = nullptr;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "References")
	TObjectPtr<UTTSWebhookComponent> TTSWebhookComponentRef = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Find")
	FName AvatarTag = TEXT("AvatarPrimary");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Find")
	FName UserTag = TEXT("UserPrimary");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt")
	bool bEnableLookAt = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt")
	bool bYawOnly = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt", meta = (ClampMin = "0.0"))
	float LookAtInterpSpeed = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt")
	float AvatarForwardYawOffset = -90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting")
	bool bEnableGreeting = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting")
	bool bEnableStartupGreeting = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting", meta = (ClampMin = "0.0"))
	float StartupGreetingDelaySeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting", meta = (ClampMin = "0.0"))
	float GreetingDistanceCm = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float GreetingFOVDegrees = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting")
	bool bGreetingLocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	EAvatarActivityState CurrentActivityState = EAvatarActivityState::Idle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting|Startup")
	bool bUseACEReadySignalForStartup = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting|Startup")
	bool bRequireBootstrapReadyForGreeting = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Greeting|Startup")
	bool bPlacementReady = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Greeting|Startup")
	bool bACEReady = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting|HTTP")
	bool bUseNativeGreetingHttp = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting|HTTP")
	FString GreetingServerUrl = TEXT("http://127.0.0.1:8011/greeting/start");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting|HTTP")
	FString GreetingSpeakerName = TEXT("it-spk0_woman");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting|HTTP", meta = (MultiLine = "true"))
	FString StartupGreetingText = TEXT("Ciao, benvenuto.");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting|HTTP", meta = (MultiLine = "true"))
	FString StandardGreetingText = TEXT("Ciao!");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greeting|HTTP", meta = (ClampMin = "0.0"))
	float GreetingStartTimeoutSeconds = 15.0f;

	UFUNCTION(BlueprintCallable, Category = "Behavior")
	void ResolveRuntimeReferences();

	UFUNCTION(BlueprintCallable, Category = "Behavior")
	void ResolveRuntimeUserActor();

	UFUNCTION(BlueprintCallable, Category = "Behavior")
	void UpdateLookAt(float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "Greeting")
	bool IsUserInGreetingZone() const;

	UFUNCTION(BlueprintCallable, Category = "Greeting")
	bool CanTriggerGreeting() const;

	UFUNCTION(BlueprintCallable, Category = "Greeting")
	bool CanProcessWaveUnlock() const;

	UFUNCTION(BlueprintCallable, Category = "State")
	void SetActivityState(EAvatarActivityState NewState);

	UFUNCTION(BlueprintCallable, Category = "State")
	bool IsAvatarBusy() const;

	UFUNCTION(BlueprintCallable, Category = "Greeting")
	void TriggerGreeting(bool bIsStartupGreeting);

	UFUNCTION(BlueprintCallable, Category = "Greeting")
	void UnlockGreetingFromWave();

	UFUNCTION(BlueprintCallable, Category = "Greeting")
	void NotifyACEReady();

	UFUNCTION(BlueprintCallable, Category = "Greeting|Startup")
	void NotifyPlacementReady();

	UFUNCTION(BlueprintCallable, Category = "State")
	void NotifySpeechPlaybackStarted();

	UFUNCTION(BlueprintCallable, Category = "State")
	void NotifySpeechPlaybackCompleted();

	UFUNCTION(BlueprintImplementableEvent, Category = "Greeting")
	void BP_RequestGreetingWebhook(bool bIsStartupGreeting);

	UFUNCTION(BlueprintImplementableEvent, Category = "Greeting")
	void BP_OnGreetingTriggered(bool bIsStartupGreeting);

	UFUNCTION(BlueprintImplementableEvent, Category = "Greeting")
	void BP_OnGreetingUnlockedByWave();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Gesture")
	bool DetectWaveGesture() const;
	virtual bool DetectWaveGesture_Implementation() const;

	UFUNCTION()
	void HandleACEPrewarmCompleted();

	UFUNCTION()
	void HandleUtteranceStarted();

	UFUNCTION()
	void HandleUtteranceCompleted();

private:
	float LastGreetingTime = -1000.0f;
	bool bUserWasInGreetingZone = false;
	bool bStartupGreetingSent = false;
	bool bGreetingAwaitingPlaybackStart = false;

	FString LastGreetingTraceId;
	FString LastGreetingUtteranceId;

	FTimerHandle StartupGreetingTimerHandle;
	FTimerHandle GreetingStartTimeoutTimerHandle;

	AActor* FindActorByTag(FName InTag) const;
	void TryStartupGreeting();
	void SendGreetingRequest(bool bIsStartupGreeting);
	FString BuildGreetingText(bool bIsStartupGreeting) const;
	void HandleGreetingStartTimeout();

	void TryBindToTTSWebhook();
	void UnbindFromTTSWebhook();

	bool IsGreetingBlockedByPlacement() const;
};