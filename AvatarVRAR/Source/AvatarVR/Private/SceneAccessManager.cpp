#include "SceneAccessManager.h"

#include "ARSessionManager.h"
#include "AvatarPlacementManager.h"

#include "EngineUtils.h"
#include "MRUtilityKitRoom.h"

ASceneAccessManager::ASceneAccessManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ASceneAccessManager::ResolveManagers()
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

	if (!ARSessionManager)
	{
		for (TActorIterator<AARSessionManager> It(GetWorld()); It; ++It)
		{
			ARSessionManager = *It;
			break;
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("SceneAccessManager - PlacementManager: %s | ARSessionManager: %s"),
		PlacementManager ? *PlacementManager->GetName() : TEXT("NULL"),
		ARSessionManager ? *ARSessionManager->GetName() : TEXT("NULL")
	);
}

void ASceneAccessManager::StartSceneLoad()
{
	ResolveManagers();
	SceneState = ESceneAccessState::WaitingForDeviceScene;

	UE_LOG(LogTemp, Log, TEXT("SceneAccessManager - StartSceneLoad called"));
	BP_StartSceneLoad();
}

void ASceneAccessManager::NotifySceneLoadSuccess()
{
	SceneState = ESceneAccessState::Ready;
	UE_LOG(LogTemp, Log, TEXT("SceneAccessManager - Scene load success"));
}

void ASceneAccessManager::NotifySceneLoadFailure()
{
	SceneState = ESceneAccessState::ManualFallback;
	UE_LOG(LogTemp, Warning, TEXT("SceneAccessManager - Scene load failed, using manual fallback"));
}

void ASceneAccessManager::HandleDeviceSceneLoaded(AActor* CurrentRoomActor)
{
	ResolveManagers();

	if (!PlacementManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("SceneAccessManager - No PlacementManager available after device scene load"));
		HandleDeviceSceneLoadFailed();
		return;
	}

	PlacementManager->FindSceneReferences();

	if (IsValid(CurrentRoomActor))
	{
		const bool bPlaced = PlacementManager->PlaceAvatarNearUserAvoidingObstacles(CurrentRoomActor, nullptr);
		if (bPlaced)
		{
			NotifySceneLoadSuccess();
			FinalizeScenePlacement(false);
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("SceneAccessManager - Scene room valid but placement returned false, using fallback"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SceneAccessManager - CurrentRoomActor invalid after device scene load"));
	}

	HandleDeviceSceneLoadFailed();
}

void ASceneAccessManager::HandleDeviceSceneLoadFailed()
{
	ResolveManagers();

	if (PlacementManager)
	{
		PlacementManager->FindSceneReferences();
		PlacementManager->PlaceAvatarInFrontOfUser();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SceneAccessManager - No PlacementManager available for fallback placement"));
	}

	NotifySceneLoadFailure();
	FinalizeScenePlacement(true);
}

void ASceneAccessManager::FinalizeScenePlacement(bool bUsedFallback)
{
	ResolveManagers();

	if (ARSessionManager)
	{
		ARSessionManager->OnPlacementStageFinished(bUsedFallback);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SceneAccessManager - ARSessionManager not found during placement finalization"));
	}
}