#include "ACEAudioSamplesLibrary.h"

#include "ACERuntimeModule.h"
#include "ACEAudioCurveSourceComponent.h"
#include "Logging/LogMacros.h"

bool UACEAudioSamplesLibrary::ValidateCommon(
    UACEAudioCurveSourceComponent* Consumer,
    int32 NumChannels,
    int32 SampleRate,
    int32 NumSamples,
    FString& OutErr
)
{
    if (!Consumer)
    {
        OutErr = TEXT("Consumer nullo (UACEAudioCurveSourceComponent).");
        return false;
    }
    if (NumChannels <= 0 || NumChannels > 2)
    {
        OutErr = FString::Printf(TEXT("NumChannels non valido: %d (atteso 1 o 2)."), NumChannels);
        return false;
    }
    if (SampleRate <= 0)
    {
        OutErr = FString::Printf(TEXT("SampleRate non valido: %d."), SampleRate);
        return false;
    }
    if (NumSamples <= 0)
    {
        OutErr = TEXT("Samples vuoti.");
        return false;
    }
    if ((NumSamples % NumChannels) != 0)
    {
        OutErr = FString::Printf(TEXT("Samples non allineati ai canali: NumSamples=%d NumChannels=%d."), NumSamples, NumChannels);
        return false;
    }
    return true;
}

bool UACEAudioSamplesLibrary::AnimateFromAudioSamplesFloat(
    UACEAudioCurveSourceComponent* Consumer,
    const TArray<float>& SamplesFloat,
    int32 NumChannels,
    int32 SampleRate,
    bool bEndOfSamples,
    FName A2FProviderName
)
{
    FString Err;
    if (!ValidateCommon(Consumer, NumChannels, SampleRate, SamplesFloat.Num(), Err))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ACEAudioSamplesLibrary] AnimateFromAudioSamplesFloat: %s"), *Err);
        return false;
    }

    const TArrayView<const float> View(SamplesFloat.GetData(), SamplesFloat.Num());

    const bool bOk = FACERuntimeModule::Get().AnimateFromAudioSamples(
        Consumer,
        View,
        NumChannels,
        SampleRate,
        bEndOfSamples,
        TOptional<FAudio2FaceEmotion>(),
        nullptr,
        A2FProviderName
    );

    if (!bOk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ACEAudioSamplesLibrary] AnimateFromAudioSamplesFloat fallito (provider=%s end=%d)"),
            *A2FProviderName.ToString(), bEndOfSamples ? 1 : 0);
    }
    return bOk;
}

bool UACEAudioSamplesLibrary::AnimateFromAudioSamplesInt16_CPPOnly(
    UACEAudioCurveSourceComponent* Consumer,
    const TArray<int16>& SamplesInt16,
    int32 NumChannels,
    int32 SampleRate,
    bool bEndOfSamples,
    FName A2FProviderName
)
{
    FString Err;
    if (!ValidateCommon(Consumer, NumChannels, SampleRate, SamplesInt16.Num(), Err))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ACEAudioSamplesLibrary] AnimateFromAudioSamplesInt16_CPPOnly: %s"), *Err);
        return false;
    }

    const TArrayView<const int16> View(SamplesInt16.GetData(), SamplesInt16.Num());

    const bool bOk = FACERuntimeModule::Get().AnimateFromAudioSamples(
        Consumer,
        View,
        NumChannels,
        SampleRate,
        bEndOfSamples,
        TOptional<FAudio2FaceEmotion>(),
        nullptr,
        A2FProviderName
    );

    if (!bOk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ACEAudioSamplesLibrary] AnimateFromAudioSamplesInt16_CPPOnly fallito (provider=%s end=%d)"),
            *A2FProviderName.ToString(), bEndOfSamples ? 1 : 0);
    }
    return bOk;
}

bool UACEAudioSamplesLibrary::EndAudioSamples(UACEAudioCurveSourceComponent* Consumer)
{
    if (!Consumer)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ACEAudioSamplesLibrary] EndAudioSamples: Consumer nullo."));
        return false;
    }
    const bool bOk = FACERuntimeModule::Get().EndAudioSamples(Consumer);
    if (!bOk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ACEAudioSamplesLibrary] EndAudioSamples: fallito."));
    }
    return bOk;
}

void UACEAudioSamplesLibrary::CancelAnimationGeneration(UACEAudioCurveSourceComponent* Consumer)
{
    if (!Consumer)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ACEAudioSamplesLibrary] CancelAnimationGeneration: Consumer nullo."));
        return;
    }
    FACERuntimeModule::Get().CancelAnimationGeneration(Consumer);
}