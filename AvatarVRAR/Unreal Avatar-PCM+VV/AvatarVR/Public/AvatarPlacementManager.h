// AvatarVR/Public/AvatarPlacementManager.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AvatarPlacementManager.generated.h"

class AMRUKRoom;

UCLASS()
class AVATARVR_API AAvatarPlacementManager : public AActor
{
	GENERATED_BODY()

public:
	AAvatarPlacementManager();

protected:
	virtual void BeginPlay() override;

public:
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "References")
	TObjectPtr<AActor> AvatarActor = nullptr;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "References")
	TObjectPtr<AActor> UserActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Find")
	FName AvatarTag = TEXT("AvatarPrimary");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Find")
	FName UserTag = TEXT("UserPrimary");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0.0"))
	float DistanceFromUser = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	float HeightOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	float FacingYawOffset = -90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	bool bPlaceOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	bool bYawOnly = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	bool bAttachAvatarToFloorAnchor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0.0"))
	float OccupancyToleranceCm = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0.0"))
	float LateralProbeStepCm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0"))
	int32 MaxLateralProbeSteps = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0.0"))
	float DistanceProbeStepCm = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0"))
	int32 MaxDistanceProbeSteps = 2;

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Placement")
	void FindSceneReferences();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Placement")
	void PlaceAvatarInFrontOfUser();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Placement")
	void ResetAvatarTransform();

	UFUNCTION(BlueprintCallable, Category = "Placement")
	void ResolveRuntimeUserActor();

	bool PlaceAvatarNearUserAvoidingObstacles(AActor* RoomActor, AActor* FloorAnchorActor);

private:
	bool bInitialTransformCached = false;
	FTransform InitialAvatarTransform;

	AActor* FindActorByTag(FName InTag) const;
	void CacheInitialAvatarTransform();

	// Helper interni: non fanno più parte della superficie pubblica del manager.
	void PlaceAvatarUsingFloorZ(float FloorZ);
	void PlaceAvatarUsingFloorAnchor(AActor* FloorAnchorActor);
};