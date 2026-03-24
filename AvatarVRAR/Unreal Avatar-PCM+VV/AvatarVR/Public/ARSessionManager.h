#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRUtilityKit.h"
#include "ARSessionManager.generated.h"

class AAvatarPlacementManager;
class AAvatarBehaviorManager;
class UMRUKSubsystem;

UENUM(BlueprintType)
enum class EARBootstrapState : uint8
{
	Idle,
	Starting,
	PassthroughPending,
	PassthroughReady,
	SceneLoadRequested,
	ManualFallback,
	Ready,
	Failed
};

UCLASS()
class AVATARVR_API AARSessionManager : public AActor
{
	GENERATED_BODY()

public:
	AARSessionManager();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "References")
	TObjectPtr<AAvatarPlacementManager> PlacementManager = nullptr;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "References")
	TObjectPtr<AAvatarBehaviorManager> BehaviorManager = nullptr;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "References")
	TObjectPtr<UMRUKSubsystem> MRUKSubsystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bootstrap")
	bool bAutoStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bootstrap", meta = (ClampMin = "0.0"))
	float PassthroughTimeoutSeconds = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bootstrap")
	EMRUKSceneModel SceneModel = EMRUKSceneModel::V2FallbackV1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bootstrap")
	EARBootstrapState BootstrapState = EARBootstrapState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bootstrap")
	bool bPassthroughHandled = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bootstrap")
	bool bSceneLoadStarted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bootstrap")
	bool bBootstrapFinalized = false;

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Bootstrap")
	void ResolveManagers();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Bootstrap")
	void StartBootstrap();

	UFUNCTION(BlueprintCallable, Category = "Bootstrap")
	void OnPassthroughReady();

	UFUNCTION(BlueprintCallable, Category = "Bootstrap")
	void OnPassthroughFailed();

	// Compatibilità temporanea finché SceneAccessManager è ancora nel progetto
	UFUNCTION(BlueprintCallable, Category = "Bootstrap")
	void OnPlacementStageFinished(bool bUsedFallback);

	UFUNCTION(BlueprintImplementableEvent, Category = "Bootstrap")
	void InitializePassthroughRuntime();

private:
	void RunSceneStage();
	void ClearPassthroughTimeout();

	void ResolveMrukSubsystem();
	void BindMrukSceneLoaded();
	void UnbindMrukSceneLoaded();
	void StartMrukSceneLoad();

	UFUNCTION()
	void HandleMrukSceneLoaded(bool bSuccess);

	void FinalizeBootstrap(bool bUsedFallback);

private:
	FTimerHandle PassthroughTimeoutHandle;
	bool bMrukSceneLoadBound = false;
};