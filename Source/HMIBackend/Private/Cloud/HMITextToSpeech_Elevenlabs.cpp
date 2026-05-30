#include "Cloud/HMITextToSpeech_Elevenlabs.h"
#include "HMIProcessorImpl.h"
#include "HMISubsystemStatics.h"

#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "JsonObjectConverter.h"
#include "Misc/Base64.h"
#include "TimerManager.h"

// https://elevenlabs.io/docs/api-reference/text-to-speech/v-1-text-to-speech-voice-id-multi-stream-input
// https://elevenlabs.io/docs/cookbooks/multi-context-web-socket
// https://elevenlabs.io/docs/best-practices/prompting/normalization

HMI_PROC_DECLARE_STATS(TTS_Elevenlabs)

#define LOGPREFIX "[TTS_Elevenlabs] "

const FName UHMITextToSpeech_Elevenlabs::ImplName = TEXT("TTS_Elevenlabs");

class UHMITextToSpeech* UHMITextToSpeech_Elevenlabs::GetOrCreateTTS_Elevenlabs(UObject* WorldContextObject, 
	FName Name,
	FString BackendKey,
	FString VoiceId,
	float VoiceSpeed
)
{
	UHMITextToSpeech* Proc = UHMISubsystemStatics::GetOrCreateTTS(WorldContextObject, ImplName, Name);

	Proc->SetProcessorParam("BackendKey", BackendKey);
	Proc->SetProcessorParam("VoiceId", VoiceId);
	Proc->SetProcessorParam("VoiceSpeed", VoiceSpeed); // TODO

	return Proc;
}

UHMITextToSpeech_Elevenlabs::UHMITextToSpeech_Elevenlabs(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ProviderName = ImplName;
	ProcessorName = ImplName.ToString();

	SetProcessorParam("BackendUrl", TEXT("wss://api.elevenlabs.io/v1/text-to-speech/"));
	SetProcessorParam("BackendKey", TEXT(""));
	SetProcessorParam("VoiceId", TEXT("54Cze5LrTSyLgbO6Fhlc"));

	// elevenlabs specific
	SetProcessorParam("ModelId", TEXT("eleven_multilingual_v2"));
	SetProcessorParam("backend_output_format", TEXT("pcm_16000")); // only pcm implemented

	// impl specific
	SetProcessorParam("operation_timeout", 30.0f);
}

UHMITextToSpeech_Elevenlabs::UHMITextToSpeech_Elevenlabs(FVTableHelper& Helper) : Super(Helper)
{
}

UHMITextToSpeech_Elevenlabs::~UHMITextToSpeech_Elevenlabs()
{
}

#if HMI_WITH_ELEVENLABS_TTS

bool UHMITextToSpeech_Elevenlabs::Proc_Init()
{
	if (!Super::Proc_Init())
		return false;

	BackendUrl = GetProcessorString("BackendUrl");
	BackendKey = GetProcessorString("BackendKey");
	HMI_GUARD_PARAM_NOT_EMPTY(BackendUrl);

	VoiceId = GetProcessorString("VoiceId");
	HMI_GUARD_PARAM_NOT_EMPTY(VoiceId);

	ModelId = GetProcessorString("ModelId");

	BackendOutputFormat = GetProcessorString("backend_output_format");
	HMI_GUARD_PARAM_NOT_EMPTY(BackendOutputFormat);

	BackendSampleRate = FCString::Atoi(*BackendOutputFormat.Replace(TEXT("pcm_"), TEXT("")));
	BackendNumChannels = 1;

	if (!UHMIBufferStatics::IsValidSampleRate(BackendSampleRate))
	{
		ProcError(TEXT("Invalid sample rate"));
		return false;
	}

	OperationTimeout = FMath::Clamp(GetProcessorFloat("operation_timeout"), 0.1f, 100.0f);

	CompletionEvent = FPlatformProcess::GetSynchEventFromPool(false);
	check(CompletionEvent);

	if (!InitSocket())
	{
		ProcError(TEXT("InitSocket failed"));
		return false;
	}

	return true;
}

void UHMITextToSpeech_Elevenlabs::Proc_Release()
{
	ResetSocket();

	if (CompletionEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
		CompletionEvent = nullptr;
	}

	Super::Proc_Release();
}

bool UHMITextToSpeech_Elevenlabs::InitSocket()
{
	FScopeLock LOCK(&WebSocketMux);

	if (WebSocket)
		return true;

	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "InitSocket"));

	IsConnectPending = false;
	IsContextInitialized = false;
	ConnectionErrorString.Reset();

	// wss://api.elevenlabs.io/v1/text-to-speech/:voice_id/stream-input
	// wss://api.elevenlabs.io/v1/text-to-speech/:voice_id/multi-stream-input

	FString Url = BackendUrl;
	Url.Append(VoiceId);
	Url.Append(TEXT("/multi-stream-input?model_id="));
	Url.Append(ModelId);

	FString Protocol;
	TMap<FString, FString> UpgradeHeaders;
	UpgradeHeaders.Add(TEXT("xi-api-key"), BackendKey);

	if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
	{
		FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");
	}

	WebSocket = FWebSocketsModule::Get().CreateWebSocket(Url, Protocol, UpgradeHeaders);
	if (!WebSocket)
		return false;

	CbConnectionError = WebSocket->OnConnectionError().AddLambda([this](const FString& Error)
	{
		IsConnectPending = false;
		ConnectionErrorString = Error;
		UE_LOG(LogHMI, Error, TEXT(LOGPREFIX "Connection error: %s"), *Error);
	});

	CbConnected = WebSocket->OnConnected().AddLambda([this]()
	{
		IsConnectPending = false;
		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Connected"));
	});

	CbClosed = WebSocket->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean)
	{
		IsConnectPending = false;
		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Closed: %s"), *Reason);
	});

	CbMessage = WebSocket->OnMessage().AddLambda([this](const FString& Message)
	{
		OnMsg(Message);
	});

	return true;
}

void UHMITextToSpeech_Elevenlabs::ResetSocket()
{
	FScopeLock LOCK(&WebSocketMux);

	if (WebSocket)
	{
		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ResetSocket"));

		WebSocket->OnConnectionError().Remove(CbConnectionError);
		WebSocket->OnConnected().Remove(CbConnected);
		WebSocket->OnClosed().Remove(CbClosed);
		WebSocket->OnMessage().Remove(CbMessage);

		WebSocket->Close();
		WebSocket.Reset();
	}

	IsConnectPending = false;
	IsContextInitialized = false;
	ConnectionErrorString.Reset();
}

void UHMITextToSpeech_Elevenlabs::SendJson(const TSharedRef<FJsonObject>& Obj)
{
	FString Content;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Content);
	FJsonSerializer::Serialize(Obj, Writer);
	SendMsg(Content);
}

void UHMITextToSpeech_Elevenlabs::SendMsg(const FString& Msg)
{
	if (Msg.IsEmpty())
		return;

	FScopeLock LOCK(&WebSocketMux);

	if (!WebSocket->IsConnected())
	{
		UE_LOG(LogHMI, Error, TEXT(LOGPREFIX "SendMsg: socket not connected"));
		return;
	}

	UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "Send: %s"), *Msg);
	WebSocket->Send(Msg);
}

int64 UHMITextToSpeech_Elevenlabs::ProcessInput(FHMITextToSpeechInput&& Input)
{
	const int64 OperationId = EnqueOperation(InputQueue, MoveTemp(Input), GetWorldContext());
	StartOrWakeProcessor();
	return OperationId;
}

void UHMITextToSpeech_Elevenlabs::CancelOperation(bool PurgeQueue)
{
	Super::CancelOperation(PurgeQueue);

	if (PurgeQueue)
	{
		PurgeInputQueue(InputQueue);
	}

	if (CompletionEvent)
	{
		CompletionEvent->Trigger();
	}
}

bool UHMITextToSpeech_Elevenlabs::Proc_DoWork(int& QueueLength)
{
	if (!DequeOperation(InputQueue, Operation, QueueLength))
		return false;

	auto& Text = Operation.Input.Text;
	UHMIStatics::NormalizeText(Text);

	if (Text.IsEmpty())
	{
		HandleOperationComplete(false, FString(TEXT("Empty input")), MoveTemp(Operation.Input), {});
		return false;
	}

	HMI_PROC_PRE_WORK_STATS(TTS_Elevenlabs)

	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "<<< \"%s\""), *Text);

	OperationSuccess = false;
	ErrorText.Reset();
	OutputWave.Reset();

	// text should end with space
	if (!Text.EndsWith(TEXT(" ")))
		Text.AppendChar(TEXT(' '));

	// Ensure WebSocket is connected before sending
	{
		FScopeLock LOCK(&WebSocketMux);
		if (!WebSocket->IsConnected() && !IsConnectPending)
		{
			IsConnectPending = true;
			ConnectionErrorString.Reset();
			WebSocket->Connect();
		}
	}

	if (IsConnectPending)
	{
		const double ConnectStartTime = GetTimestamp();
		while (IsConnectPending)
		{
			if (GetTimestamp() - ConnectStartTime > OperationTimeout)
			{
				ErrorText = TEXT("Connection timeout");
				HandleOperationComplete(false, MoveTemp(ErrorText), MoveTemp(Operation.Input), {});
				return false;
			}
			FPlatformProcess::Sleep(0.005f);
		}

		if (!WebSocket->IsConnected())
		{
			ErrorText = FString::Printf(TEXT("Connection failed: %s"), *ConnectionErrorString);
			HandleOperationComplete(false, MoveTemp(ErrorText), MoveTemp(Operation.Input), {});
			return false;
		}
	}

	if (!IsContextInitialized)
	{
		IsContextInitialized = true;

		TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		//JsonObject->SetStringField(TEXT("context_id"), ContextId);
		JsonObject->SetStringField(TEXT("text"), TEXT(" ")); // send space to initialize connection
		JsonObject->SetStringField(TEXT("output_format"), BackendOutputFormat);
		SendJson(JsonObject);
	}

	{
		TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		//JsonObject->SetStringField(TEXT("context_id"), ContextId);
		JsonObject->SetStringField(TEXT("text"), Text);
		JsonObject->SetBoolField(TEXT("flush"), true);
		
		Operation.RequestTimestamp = GetTimestamp();
		IsOperationPending = true;
		CompletionEvent->Reset();

		SendJson(JsonObject);
	}

	AsyncTask(ENamedThreads::GameThread, [this] ()
	{
		auto* Mgr = GetWorldTimerManager();
		if (Mgr)
			Mgr->SetTimer(PendingTimer, this, &UHMITextToSpeech_Elevenlabs::OnOperationTimeout, OperationTimeout, false);
	});
	
	CompletionEvent->Wait();
	IsOperationPending = false;

	AsyncTask(ENamedThreads::GameThread, [this] ()
	{
		auto* Mgr = GetWorldTimerManager();
		if (Mgr)
			Mgr->ClearTimer(PendingTimer);
	});

	#if 0
	if (CancelFlag && IsContextInitialized && WebSocket && WebSocket->IsConnected())
	{
		IsContextInitialized = false;

		TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		//JsonObject->SetStringField(TEXT("context_id"), ContextId);
		JsonObject->SetBoolField(TEXT("close_context"), true);
		SendJson(JsonObject);
	}
	#endif

	if (!CancelFlag)
	{
		HandleOperationComplete(OperationSuccess, MoveTemp(ErrorText), MoveTemp(Operation.Input), MoveTemp(OutputWave));
	}

	return OperationSuccess;
}

void UHMITextToSpeech_Elevenlabs::Proc_PostWorkStats()
{
	HMI_PROC_POST_WORK_STATS(TTS_Elevenlabs)
}

void UHMITextToSpeech_Elevenlabs::OnMsg(const FString& Msg)
{
	UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "OnMsg"));

	const double ResponseTimestamp = GetTimestamp();

	do
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::CreateFromView(Msg);
		TSharedPtr<FJsonObject> JsonObject;
		if (!FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			ErrorText = TEXT("Deserialize");
			break;
		}

		// error, code, message
		FString ErrorValue;
		if (JsonObject->TryGetStringField(TEXT("error"), ErrorValue))
		{
			ErrorText = MoveTemp(ErrorValue);
			break;
		}

		bool IsFinal = false;
		if (JsonObject->TryGetBoolField(TEXT("isFinal"), IsFinal)) // multi-stream-input
		{
			if (IsFinal)
			{
				UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "isFinal=true"));
				IsContextInitialized = false;
			}
		}

		#if 0
		if (JsonObject->TryGetBoolField(TEXT("is_final"), IsFinal)) // stream-input
		{
			if (IsFinal)
			{
				UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "is_final=true"));
				IsContextInitialized = false;
			}
		}
		#endif

		// {"audio":"xxxxxxx","isFinal":null,
		// "normalizedAlignment":{"chars":[" ","H","e","l","l","o"," ","W","o","r","l","d"," "],
		// "charStartTimesMs":[0,70,104,174,209,255,313,372,406,464,522,592,662],
		// "charDurationsMs":[70,34,70,35,46,58,59,34,58,58,70,70,360]},
		// "alignment":{"chars":["H","e","l","l","o"," ","W","o","r","l","d"," "],
		// "charStartTimesMs":[0,104,174,209,255,313,372,406,464,522,592,662],
		// "charDurationsMs":[104,70,35,46,58,59,34,58,58,70,70,360]}}

		FString AudioB64;
		if (JsonObject->TryGetStringField(TEXT("audio"), AudioB64))
		{
			//FString ContextId;
			//JsonObject->TryGetStringField(TEXT("contextId"), ContextId);

			TArray<uint8> AudioBytes;
			if (!FBase64::Decode(AudioB64, AudioBytes))
			{
				ErrorText = TEXT("Decode audio");
				break;
			}

			UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "Decoded %d bytes"), AudioBytes.Num());

			#if 0
			const auto& Alignment = JsonObject->GetObjectField(TEXT("alignment"));
			if (!Alignment)
			{
				ErrorText = TEXT("Missing alignment field");
				break;
			}

			const auto& Chars = Alignment->GetArrayField(TEXT("chars"));

			FString Text;
			Text.Reserve(Chars.Num());
			for (int i = 0; i < Chars.Num(); ++i)
				Text.Append(Chars[i]->AsString());
			#endif

			OutputWave = GenerateOutputWave({ (const int16*)AudioBytes.GetData(), (int32)(AudioBytes.Num() / sizeof(int16)) }, BackendSampleRate, BackendNumChannels);

			Operation.ResponseTimestamp = ResponseTimestamp;
			UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "Backend latency %.4f"), ResponseTimestamp - Operation.RequestTimestamp);

			OperationSuccess = true;
			break;
		}

		// SHOULD NOT REACH BUT WHO KNOWS..
		ErrorText = TEXT("Unknown message");
	}
	while (false);

	if (IsOperationPending)
	{
		CompletionEvent->Trigger();
	}
}

void UHMITextToSpeech_Elevenlabs::OnOperationTimeout()
{
	if (IsOperationPending)
	{
		ErrorText = TEXT("Timeout");
		CompletionEvent->Trigger();
	}
}

void UHMITextToSpeech_Elevenlabs::HandleOperationComplete(bool Success, FString&& Error, FHMITextToSpeechInput&& Input, FHMIWaveHandle&& Wave)
{
	if (Success)
	{
		UE_LOG(LogHMI, Log, TEXT(LOGPREFIX ">>> Duration=%.4f"), Wave.GetDuration());
	}

	Wave.SetTag(Input.UserTag);

	FHMITextToSpeechResult Result(GetTimestamp(), Success, MoveTemp(Error), MoveTemp(Wave));

	BroadcastResult(MoveTemp(Input), MoveTemp(Result), OnTextToSpeechComplete);
}

#endif // HMI_WITH_ELEVENLABS_TTS

#undef LOGPREFIX
