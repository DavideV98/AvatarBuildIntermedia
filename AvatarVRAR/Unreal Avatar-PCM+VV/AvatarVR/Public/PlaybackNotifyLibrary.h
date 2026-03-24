#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "PlaybackNotifyLibrary.generated.h"

UCLASS()
class AVATARVR_API UPlaybackNotifyLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Avatar|Notify")
    static void PostPlaybackDone(const FString& Url, const FString& TraceId, const FString& UtteranceId);
};