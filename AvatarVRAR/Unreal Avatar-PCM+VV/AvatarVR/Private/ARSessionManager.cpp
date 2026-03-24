// AvatarVR/Private/ARSessionManager.cpp

#include "ARSessionManager.h"

#include "AvatarBehaviorManager.h"
#include "AvatarPlacementManager.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "MRUtilityKitRoom.h"
#include "MRUtilityKitSubsystem.h"
#include "TimerManager.h"

AARSessionManager::AARSessionManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AARSessionManager::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoStart)
	{
		StartBootstrap();
	}
}

void AARSessionManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindMrukSceneLoaded();
	ClearPassthroughTimeout();

	Super::EndPlay(EndPlayReason);
}

void AARSessionManager::ResolveManagers()
{
	if (!GetWorld())
	{
		return;
	}

	if (!PlacementManager)
	{
		for (TActorIterator<AAvatarPlacementManager> It(GetWorld()); It; ++It)
		{
			PlacementManager = *It;
			break;
		}
	}

	if (!BehaviorManager)
	{
		for (TActorIterator<AAvatarBehaviorManager> It(GetWorld()); It; ++It)
		{
			BehaviorManager = *It;
			break;
		}
	}

	ResolveMrukSubsystem();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("ARSessionManager - PlacementManager: %s | BehaviorManager: %s | MRUKSubsystem: %s"),
		PlacementManager ? *PlacementManager->GetName() : TEXT("NULL"),
		BehaviorManager ? *BehaviorManager->GetName() : TEXT("NULL"),
		MRUKSubsystem ? TEXT("VALID") : TEXT("NULL"));
}

void AARSessionManager::ResolveMrukSubsystem()
{
	if (MRUKSubsystem)
	{
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		MRUKSubsystem = GI->GetSubsystem<UMRUKSubsystem>();
	}
}

void AARSessionManager::StartBootstrap()
{
	BootstrapState = EARBootstrapState::Starting;
	bPassthroughHandled = false;
	bSceneLoadStarted = false;
	bBootstrapFinalized = false;

	ResolveManagers();
	ClearPassthroughTimeout();
	UnbindMrukSceneLoaded();

	BootstrapState = EARBootstrapState::PassthroughPending;

	if (PassthroughTimeoutSeconds > 0.0f && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			PassthroughTimeoutHandle,
			this,
			&AARSessionManager::OnPassthroughFailed,
			PassthroughTimeoutSeconds,
			false);
	}

	UE_LOG(LogTemp, Log, TEXT("ARSessionManager - Bootstrap started"));
	InitializePassthroughRuntime();
}

void AARSessionManager::OnPassthroughReady()
{
	if (bBootstrapFinalized)
	{
		UE_LOG(LogTemp, Log, TEXT("ARSessionManager - Ignoring passthrough ready: bootstrap already finalized"));
		return;
	}

	if (bPassthroughHandled || BootstrapState != EARBootstrapState::PassthroughPending)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("ARSessionManager - Ignoring duplicate passthrough ready. State=%d"),
			static_cast<uint8>(BootstrapState));
		return;
	}

	bPassthroughHandled = true;
	ClearPassthroughTimeout();

	UE_LOG(LogTemp, Log, TEXT("ARSessionManager - Passthrough ready"));

	BootstrapState = EARBootstrapState::PassthroughReady;
	RunSceneStage();
}

void AARSessionManager::OnPassthroughFailed()
{
	if (bBootstrapFinalized)
	{
		UE_LOG(LogTemp, Log, TEXT("ARSessionManager - Ignoring passthrough failure: bootstrap already finalized"));
		return;
	}

	ClearPassthroughTimeout();
	UnbindMrukSceneLoaded();

	UE_LOG(LogTemp, Warning, TEXT("ARSessionManager - Passthrough failed"));

	if (PlacementManager)
	{
		PlacementManager->FindSceneReferences();
		PlacementManager->PlaceAvatarInFrontOfUser();
	}

	BootstrapState = EARBootstrapState::Failed;
	FinalizeBootstrap(true);
}

void AARSessionManager::RunSceneStage()
{
	if (bBootstrapFinalized)
	{
		UE_LOG(LogTemp, Log, TEXT("ARSessionManager - Scene stage skipped: bootstrap already finalized"));
		return;
	}

	BootstrapState = EARBootstrapState::SceneLoadRequested;
	StartMrukSceneLoad();
}

void AARSessionManager::ClearPassthroughTimeout()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PassthroughTimeoutHandle);
	}
}

void AARSessionManager::BindMrukSceneLoaded()
{
	if (!MRUKSubsystem)
	{
		return;
	}

	if (!bMrukSceneLoadBound)
	{
		MRUKSubsystem->OnSceneLoaded.AddDynamic(this, &AARSessionManager::HandleMrukSceneLoaded);
		bMrukSceneLoadBound = true;
	}
}

void AARSessionManager::UnbindMrukSceneLoaded()
{
	if (MRUKSubsystem && bMrukSceneLoadBound)
	{
		MRUKSubsystem->OnSceneLoaded.RemoveDynamic(this, &AARSessionManager::HandleMrukSceneLoaded);
	}

	bMrukSceneLoadBound = false;
}

void AARSessionManager::StartMrukSceneLoad()
{
	ResolveManagers();

	if (bBootstrapFinalized)
	{
		UE_LOG(LogTemp, Log, TEXT("ARSessionManager - Scene load skipped: bootstrap already finalized"));
		return;
	}

	if (bSceneLoadStarted)
	{
		UE_LOG(LogTemp, Log, TEXT("ARSessionManager - Scene load skipped: already started"));
		return;
	}

	bSceneLoadStarted = true;

	if (!MRUKSubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("ARSessionManager - MRUKSubsystem not available, using fallback placement"));

		if (PlacementManager)
		{
			PlacementManager->FindSceneReferences();
			PlacementManager->PlaceAvatarInFrontOfUser();
		}

		FinalizeBootstrap(true);
		return;
	}

	BindMrukSceneLoaded();

	UE_LOG(LogTemp, Log, TEXT("ARSessionManager - Starting MRUK scene load"));
	MRUKSubsystem->LoadSceneFromDevice(SceneModel);
}

void AARSessionManager::HandleMrukSceneLoaded(bool bSuccess)
{
	if (bBootstrapFinalized)
	{
		UE_LOG(LogTemp, Log, TEXT("ARSessionManager - Ignoring MRUK callback: bootstrap already finalized"));
		return;
	}

	UnbindMrukSceneLoaded();
	ResolveManagers();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("ARSessionManager - MRUK scene loaded callback. Success=%s"),
		bSuccess ? TEXT("true") : TEXT("false"));

	if (!PlacementManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("ARSessionManager - No PlacementManager available during scene load completion"));
		FinalizeBootstrap(true);
		return;
	}

	PlacementManager->FindSceneReferences();

	if (!bSuccess || !MRUKSubsystem)
	{
		PlacementManager->PlaceAvatarInFrontOfUser();
		FinalizeBootstrap(true);
		return;
	}

	AMRUKRoom* CurrentRoom = MRUKSubsystem->GetCurrentRoom();
	if (IsValid(CurrentRoom))
	{
		const bool bPlaced = PlacementManager->PlaceAvatarNearUserAvoidingObstacles(CurrentRoom, nullptr);
		if (bPlaced)
		{
			FinalizeBootstrap(false);
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("ARSessionManager - Room valid but placement returned false, using fallback"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ARSessionManager - No valid current room, using fallback"));
	}

	PlacementManager->PlaceAvatarInFrontOfUser();
	FinalizeBootstrap(true);
}

void AARSessionManager::FinalizeBootstrap(bool bUsedFallback)
{
	if (bBootstrapFinalized)
	{
		UE_LOG(LogTemp, Log, TEXT("ARSessionManager - FinalizeBootstrap ignored: already finalized"));
		return;
	}

	bBootstrapFinalized = true;
	ClearPassthroughTimeout();

	BootstrapState = bUsedFallback
		? EARBootstrapState::ManualFallback
		: EARBootstrapState::Ready;

	if (!BehaviorManager)
	{
		ResolveManagers();
	}

	if (BehaviorManager)
	{
		BehaviorManager->NotifyPlacementReady();
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("ARSessionManager - Bootstrap finalized. State=%d UsedFallback=%s"),
		static_cast<uint8>(BootstrapState),
		bUsedFallback ? TEXT("true") : TEXT("false"));
}