#include "PlaybackNotifyLibrary.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

void UPlaybackNotifyLibrary::PostPlaybackDone(const FString& Url, const FString& TraceId, const FString& UtteranceId)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("event"), TEXT("utterance_done"));
    Obj->SetStringField(TEXT("trace_id"), TraceId);
    Obj->SetStringField(TEXT("utterance_id"), UtteranceId);

    FString Body;
    {
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
        FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(Url);
    Req->SetVerb(TEXT("POST"));
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
    Req->SetContentAsString(Body);
    Req->ProcessRequest();
}