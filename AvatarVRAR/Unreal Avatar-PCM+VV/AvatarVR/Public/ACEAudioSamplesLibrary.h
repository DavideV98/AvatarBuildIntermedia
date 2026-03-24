#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ACEAudioSamplesLibrary.generated.h"

class UACEAudioCurveSourceComponent;

UCLASS()
class AVATARVR_API UACEAudioSamplesLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Invia un chunk di samples float32 interleaved (LRLR...) a ACE. */
    UFUNCTION(BlueprintCallable, Category = "ACE|Audio2Face")
    static bool AnimateFromAudioSamplesFloat(
        UACEAudioCurveSourceComponent* Consumer,
        const TArray<float>& SamplesFloat,
        int32 NumChannels,
        int32 SampleRate,
        bool bEndOfSamples,
        FName A2FProviderName = FName("Default")
    );

    /** Chiude esplicitamente la sessione corrente (di solito non serve se usi bEndOfSamples=true sull’ultimo chunk). */
    UFUNCTION(BlueprintCallable, Category = "ACE|Audio2Face")
    static bool EndAudioSamples(UACEAudioCurveSourceComponent* Consumer);

    /** Cancella generazione animazioni in corso (utile quando arriva una nuova utterance). */
    UFUNCTION(BlueprintCallable, Category = "ACE|Audio2Face")
    static void CancelAnimationGeneration(UACEAudioCurveSourceComponent* Consumer);

    // ------------------------------
    // ✅ SOLO C++ (NON Blueprint)
    // ------------------------------
    static bool AnimateFromAudioSamplesInt16_CPPOnly(
        UACEAudioCurveSourceComponent* Consumer,
        const TArray<int16>& SamplesInt16,
        int32 NumChannels,
        int32 SampleRate,
        bool bEndOfSamples,
        FName A2FProviderName = FName("Default")
    );

private:
    static bool ValidateCommon(
        UACEAudioCurveSourceComponent* Consumer,
        int32 NumChannels,
        int32 SampleRate,
        int32 NumSamples,
        FString& OutErr
    );
};