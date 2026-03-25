#include "AvatarBehaviorManager.h"

#include "TTSWebhookComponent.h"

#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Misc/Guid.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

AAvatarBehaviorManager::AAvatarBehaviorManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AAvatarBehaviorManager::BeginPlay()
{
	Super::BeginPlay();

	ResolveRuntimeReferences();
	TryBindToTTSWebhook();

	bUserWasInGreetingZone = IsUserInGreetingZone();

	if (!bUseACEReadySignalForStartup && bEnableStartupGreeting && !bStartupGreetingSent)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				StartupGreetingTimerHandle,
				this,
				&AAvatarBehaviorManager::TryStartupGreeting,
				StartupGreetingDelaySeconds,
				false
			);
		}
	}
}

void AAvatarBehaviorManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!AvatarActor || !UserActor)
	{
		ResolveRuntimeReferences();
	}

	if (!TTSWebhookComponentRef && AvatarActor)
	{
		TryBindToTTSWebhook();
	}

	if (bEnableLookAt)
	{
		UpdateLookAt(DeltaSeconds);
	}

	if (CanProcessWaveUnlock() && DetectWaveGesture())
	{
		UnlockGreetingFromWave();
	}

	const bool bUserNowInGreetingZone = IsUserInGreetingZone();

	if (bEnableGreeting && CanTriggerGreeting())
	{
		if (bUserNowInGreetingZone && !bUserWasInGreetingZone)
		{
			TriggerGreeting(false);
		}
	}

	bUserWasInGreetingZone = bUserNowInGreetingZone;
}

void AAvatarBehaviorManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StartupGreetingTimerHandle);
		World->GetTimerManager().ClearTimer(GreetingStartTimeoutTimerHandle);
	}

	UnbindFromTTSWebhook();

	Super::EndPlay(EndPlayReason);
}

AActor* AAvatarBehaviorManager::FindActorByTag(FName InTag) const
{
	if (!GetWorld() || InTag.IsNone())
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor) && Actor->ActorHasTag(InTag))
		{
			return Actor;
		}
	}

	return nullptr;
}

void AAvatarBehaviorManager::ResolveRuntimeReferences()
{
	if (!AvatarActor)
	{
		AvatarActor = FindActorByTag(AvatarTag);
	}

	if (!UserActor)
	{
		UserActor = FindActorByTag(UserTag);
	}

	if (!UserActor)
	{
		ResolveRuntimeUserActor();
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("AvatarBehaviorManager - AvatarActor: %s | UserActor: %s"),
		AvatarActor ? *AvatarActor->GetName() : TEXT("NULL"),
		UserActor ? *UserActor->GetName() : TEXT("NULL")
	);
}

void AAvatarBehaviorManager::ResolveRuntimeUserActor()
{
	if (UserActor)
	{
		return;
	}

	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		UserActor = PlayerPawn;
		UE_LOG(LogTemp, Log, TEXT("ResolveRuntimeUserActor - Using player pawn: %s"), *UserActor->GetName());
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("ResolveRuntimeUserActor - No player pawn found"));
}

void AAvatarBehaviorManager::TryBindToTTSWebhook()
{
	if (TTSWebhookComponentRef || !AvatarActor)
	{
		return;
	}

	TTSWebhookComponentRef = AvatarActor->FindComponentByClass<UTTSWebhookComponent>();

	if (!TTSWebhookComponentRef)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("AvatarBehaviorManager - TTSWebhookComponent non trovato su AvatarActor %s"),
			*AvatarActor->GetName()
		);
		return;
	}

	TTSWebhookComponentRef->OnACEPrewarmCompleted.AddDynamic(this, &AAvatarBehaviorManager::HandleACEPrewarmCompleted);
	TTSWebhookComponentRef->OnUtteranceStarted.AddDynamic(this, &AAvatarBehaviorManager::HandleUtteranceStarted);
	TTSWebhookComponentRef->OnUtteranceCompleted.AddDynamic(this, &AAvatarBehaviorManager::HandleUtteranceCompleted);

	UE_LOG(LogTemp, Log, TEXT("AvatarBehaviorManager - Bound a TTSWebhookComponent su %s"), *AvatarActor->GetName());
}

void AAvatarBehaviorManager::UnbindFromTTSWebhook()
{
	if (!TTSWebhookComponentRef)
	{
		return;
	}

	TTSWebhookComponentRef->OnACEPrewarmCompleted.RemoveDynamic(this, &AAvatarBehaviorManager::HandleACEPrewarmCompleted);
	TTSWebhookComponentRef->OnUtteranceStarted.RemoveDynamic(this, &AAvatarBehaviorManager::HandleUtteranceStarted);
	TTSWebhookComponentRef->OnUtteranceCompleted.RemoveDynamic(this, &AAvatarBehaviorManager::HandleUtteranceCompleted);

	TTSWebhookComponentRef = nullptr;
}

void AAvatarBehaviorManager::HandleACEPrewarmCompleted()
{
	UE_LOG(LogTemp, Log, TEXT("AvatarBehaviorManager - HandleACEPrewarmCompleted fired"));
	NotifyACEReady();
}

void AAvatarBehaviorManager::HandleUtteranceStarted()
{
	NotifySpeechPlaybackStarted();
}

void AAvatarBehaviorManager::HandleUtteranceCompleted()
{
	NotifySpeechPlaybackCompleted();
}

void AAvatarBehaviorManager::UpdateLookAt(float DeltaSeconds)
{
	if (!AvatarActor || !UserActor)
	{
		return;
	}

	const FVector AvatarLocation = AvatarActor->GetActorLocation();
	const FVector UserLocation = UserActor->GetActorLocation();

	FVector LookDirection = UserLocation - AvatarLocation;
	if (LookDirection.IsNearlyZero())
	{
		return;
	}

	if (bYawOnly)
	{
		LookDirection.Z = 0.0f;
		if (LookDirection.IsNearlyZero())
		{
			return;
		}
	}

	FRotator TargetRotation = UKismetMathLibrary::MakeRotFromX(LookDirection);

	if (bYawOnly)
	{
		TargetRotation.Pitch = 0.0f;
		TargetRotation.Roll = 0.0f;
	}

	TargetRotation.Yaw += AvatarForwardYawOffset;

	const FRotator CurrentRotation = AvatarActor->GetActorRotation();
	const FRotator SmoothedRotation = FMath::RInterpTo(
		CurrentRotation,
		TargetRotation,
		DeltaSeconds,
		LookAtInterpSpeed
	);

	AvatarActor->SetActorRotation(SmoothedRotation);
}

bool AAvatarBehaviorManager::IsUserInGreetingZone() const
{
	if (!AvatarActor || !UserActor)
	{
		return false;
	}

	const FVector AvatarLocation = AvatarActor->GetActorLocation();
	const FVector UserLocation = UserActor->GetActorLocation();

	FVector ToUser = UserLocation - AvatarLocation;
	const float Distance = ToUser.Size();

	if (Distance > GreetingDistanceCm)
	{
		return false;
	}

	if (Distance <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	ToUser.Normalize();

	FVector AvatarForward = AvatarActor->GetActorForwardVector();
	AvatarForward.Z = 0.0f;

	if (!AvatarForward.Normalize())
	{
		return false;
	}

	ToUser.Z = 0.0f;
	if (!ToUser.Normalize())
	{
		return false;
	}

	const float Dot = FVector::DotProduct(AvatarForward, ToUser);
	const float Threshold = FMath::Cos(FMath::DegreesToRadians(GreetingFOVDegrees * 0.5f));
	return Dot >= Threshold;
}

bool AAvatarBehaviorManager::IsGreetingBlockedByPlacement() const
{
	return bRequireBootstrapReadyForGreeting && !bPlacementReady;
}

bool AAvatarBehaviorManager::CanTriggerGreeting() const
{
	return CurrentActivityState == EAvatarActivityState::Idle
		&& !bGreetingLocked
		&& !IsGreetingBlockedByPlacement();
}

bool AAvatarBehaviorManager::CanProcessWaveUnlock() const
{
	return CurrentActivityState == EAvatarActivityState::Idle && bGreetingLocked;
}

void AAvatarBehaviorManager::SetActivityState(EAvatarActivityState NewState)
{
	if (CurrentActivityState == NewState)
	{
		return;
	}

	CurrentActivityState = NewState;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("AvatarBehaviorManager - ActivityState changed to %d"),
		static_cast<uint8>(CurrentActivityState)
	);
}

bool AAvatarBehaviorManager::IsAvatarBusy() const
{
	return CurrentActivityState != EAvatarActivityState::Idle;
}

FString AAvatarBehaviorManager::BuildGreetingText(bool bIsStartupGreeting) const
{
	return bIsStartupGreeting ? StartupGreetingText : StandardGreetingText;
}

void AAvatarBehaviorManager::NotifyACEReady()
{
	bACEReady = true;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("AvatarBehaviorManager - ACE ready notified. PlacementReady=%s StartupSent=%s"),
		bPlacementReady ? TEXT("true") : TEXT("false"),
		bStartupGreetingSent ? TEXT("true") : TEXT("false")
	);

	if (bUseACEReadySignalForStartup)
	{
		TryStartupGreeting();
	}
}

void AAvatarBehaviorManager::NotifyPlacementReady()
{
	bPlacementReady = true;

	UE_LOG(LogTemp, Log, TEXT("AvatarBehaviorManager - Placement ready notified"));

	if (!bUseACEReadySignalForStartup || bACEReady)
	{
		TryStartupGreeting();
	}
}

void AAvatarBehaviorManager::NotifySpeechPlaybackStarted()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GreetingStartTimeoutTimerHandle);
	}

	if (bGreetingAwaitingPlaybackStart)
	{
		bGreetingAwaitingPlaybackStart = false;
	}

	SetActivityState(EAvatarActivityState::Speaking);
}

void AAvatarBehaviorManager::NotifySpeechPlaybackCompleted()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GreetingStartTimeoutTimerHandle);
	}

	bGreetingAwaitingPlaybackStart = false;
	SetActivityState(EAvatarActivityState::Idle);
}

bool AAvatarBehaviorManager::DetectWaveGesture_Implementation() const
{
	return false;
}

void AAvatarBehaviorManager::HandleGreetingStartTimeout()
{
	if (!bGreetingAwaitingPlaybackStart)
	{
		return;
	}

	bGreetingAwaitingPlaybackStart = false;
	bGreetingLocked = false;

	if (bStartupGreetingSent && LastGreetingUtteranceId.IsEmpty())
	{
		bStartupGreetingSent = false;
	}

	SetActivityState(EAvatarActivityState::Idle);

	UE_LOG(LogTemp, Warning, TEXT("AvatarBehaviorManager - Greeting start timeout, reverting to Idle"));
}

void AAvatarBehaviorManager::SendGreetingRequest(bool bIsStartupGreeting)
{
	if (GreetingServerUrl.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("SendGreetingRequest - GreetingServerUrl vuoto"));

		bGreetingLocked = false;

		if (bIsStartupGreeting)
		{
			bStartupGreetingSent = false;
		}

		SetActivityState(EAvatarActivityState::Idle);
		return;
	}

	LastGreetingTraceId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
	LastGreetingUtteranceId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(10);

	const FString TextToSend = BuildGreetingText(bIsStartupGreeting);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("event"), TEXT("greeting_request"));
	Obj->SetStringField(TEXT("trace_id"), LastGreetingTraceId);
	Obj->SetStringField(TEXT("utterance_id"), LastGreetingUtteranceId);
	Obj->SetStringField(TEXT("speaker_name"), GreetingSpeakerName);
	Obj->SetStringField(TEXT("text"), TextToSend);
	Obj->SetBoolField(TEXT("is_startup_greeting"), bIsStartupGreeting);

	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(GreetingServerUrl);
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetContentAsString(Body);

	TWeakObjectPtr<AAvatarBehaviorManager> WeakThis(this);

	Req->OnProcessRequestComplete().BindLambda(
		[WeakThis, bIsStartupGreeting](FHttpRequestPtr Request, FHttpResponsePtr Resp, bool bSucceeded)
		{
			if (!WeakThis.IsValid())
			{
				return;
			}

			AAvatarBehaviorManager* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			const bool bHttpOk = bSucceeded && Resp.IsValid() && EHttpResponseCodes::IsOk(Resp->GetResponseCode());
			if (!bHttpOk)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("SendGreetingRequest failed. succeeded=%d code=%d body=%s"),
					bSucceeded ? 1 : 0,
					Resp.IsValid() ? Resp->GetResponseCode() : -1,
					Resp.IsValid() ? *Resp->GetContentAsString() : TEXT("<no response>")
				);

				Self->bGreetingAwaitingPlaybackStart = false;
				Self->bGreetingLocked = false;
				Self->LastGreetingTraceId.Empty();
				Self->LastGreetingUtteranceId.Empty();

				if (bIsStartupGreeting)
				{
					Self->bStartupGreetingSent = false;
				}

				if (UWorld* World = Self->GetWorld())
				{
					World->GetTimerManager().ClearTimer(Self->GreetingStartTimeoutTimerHandle);
				}

				Self->SetActivityState(EAvatarActivityState::Idle);
				return;
			}

			UE_LOG(
				LogTemp,
				Log,
				TEXT("SendGreetingRequest accepted. code=%d body=%s"),
				Resp->GetResponseCode(),
				*Resp->GetContentAsString()
			);
		}
	);

	Req->ProcessRequest();
}

void AAvatarBehaviorManager::TriggerGreeting(bool bIsStartupGreeting)
{
	if (!CanTriggerGreeting())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("TriggerGreeting - blocked. State=%d Locked=%s PlacementReady=%s ACEReady=%s"),
			static_cast<uint8>(CurrentActivityState),
			bGreetingLocked ? TEXT("true") : TEXT("false"),
			bPlacementReady ? TEXT("true") : TEXT("false"),
			bACEReady ? TEXT("true") : TEXT("false")
		);
		return;
	}

	bGreetingLocked = true;
	bGreetingAwaitingPlaybackStart = true;
	LastGreetingTime = GetWorld() ? GetWorld()->GetTimeSeconds() : LastGreetingTime;

	SetActivityState(EAvatarActivityState::PerformingAction);

	if (bIsStartupGreeting)
	{
		bStartupGreetingSent = true;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			GreetingStartTimeoutTimerHandle,
			this,
			&AAvatarBehaviorManager::HandleGreetingStartTimeout,
			GreetingStartTimeoutSeconds,
			false
		);
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("AvatarBehaviorManager - Greeting triggered. Startup=%s"),
		bIsStartupGreeting ? TEXT("true") : TEXT("false")
	);

	BP_OnGreetingTriggered(bIsStartupGreeting);
	SendGreetingRequest(bIsStartupGreeting);
}

void AAvatarBehaviorManager::UnlockGreetingFromWave()
{
	if (!CanProcessWaveUnlock())
	{
		return;
	}

	bGreetingLocked = false;
	bUserWasInGreetingZone = IsUserInGreetingZone();

	UE_LOG(LogTemp, Log, TEXT("AvatarBehaviorManager - Greeting unlocked by wave"));

	BP_OnGreetingUnlockedByWave();
}

void AAvatarBehaviorManager::TryStartupGreeting()
{
	if (!bEnableGreeting || !bEnableStartupGreeting || bStartupGreetingSent)
	{
		return;
	}

	if (bUseACEReadySignalForStartup && !bACEReady)
	{
		UE_LOG(LogTemp, Log, TEXT("AvatarBehaviorManager - ACE ready, waiting for placement ready"));
		return;
	}

	if (IsGreetingBlockedByPlacement())
	{
		UE_LOG(LogTemp, Log, TEXT("AvatarBehaviorManager - Startup greeting blocked by placement readiness"));
		return;
	}

	if (CurrentActivityState != EAvatarActivityState::Idle)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				StartupGreetingTimerHandle,
				this,
				&AAvatarBehaviorManager::TryStartupGreeting,
				0.25f,
				false
			);
		}
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("AvatarBehaviorManager - TryStartupGreeting. StartupSent=%s Enable=%s ACEReady=%s PlacementReady=%s State=%d"),
		bStartupGreetingSent ? TEXT("true") : TEXT("false"),
		bEnableStartupGreeting ? TEXT("true") : TEXT("false"),
		bACEReady ? TEXT("true") : TEXT("false"),
		bPlacementReady ? TEXT("true") : TEXT("false"),
		static_cast<uint8>(CurrentActivityState)
	);

	TriggerGreeting(true);
}