#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SceneAccessManager.generated.h"

class AAvatarPlacementManager;
class AARSessionManager;

UENUM(BlueprintType)
enum class ESceneAccessState : uint8
{
	Idle,
	RequestingPermission,
	WaitingForDeviceScene,
	WaitingForJsonScene,
	ManualFallback,
	Ready
};

UCLASS()
class AVATARVR_API ASceneAccessManager : public AActor
{
	GENERATED_BODY()

public:
	ASceneAccessManager();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scene")
	ESceneAccessState SceneState = ESceneAccessState::Idle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	bool bUseJsonFallbackIfDeviceFails = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	FString RoomJsonPath;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "References")
	TObjectPtr<AAvatarPlacementManager> PlacementManager = nullptr;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "References")
	TObjectPtr<AARSessionManager> ARSessionManager = nullptr;

	UFUNCTION(BlueprintCallable, Category = "Scene")
	void ResolveManagers();

	UFUNCTION(BlueprintCallable, Category = "Scene")
	void StartSceneLoad();

	UFUNCTION(BlueprintCallable, Category = "Scene")
	void NotifySceneLoadSuccess();

	UFUNCTION(BlueprintCallable, Category = "Scene")
	void NotifySceneLoadFailure();

	UFUNCTION(BlueprintCallable, Category = "Scene")
	void HandleDeviceSceneLoaded(AActor* CurrentRoomActor);

	UFUNCTION(BlueprintCallable, Category = "Scene")
	void HandleDeviceSceneLoadFailed();

	UFUNCTION(BlueprintImplementableEvent, Category = "Scene")
	void BP_StartSceneLoad();

private:
	void FinalizeScenePlacement(bool bUsedFallback);
};