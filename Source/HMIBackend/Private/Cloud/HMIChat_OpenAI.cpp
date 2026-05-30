#include "Cloud/HMIChat_OpenAI.h"
#include "HMIProcessorImpl.h"
#include "HMISubsystemStatics.h"

#include "Http/HMIHttpRequest.h"
#include "JsonObjectConverter.h"

// https://platform.openai.com/docs/api-reference/chat/create

HMI_PROC_DECLARE_STATS(Chat_OpenAI)

#define LOGPREFIX "[Chat_OpenAI] "

const FName UHMIChat_OpenAI::ImplName = TEXT("Chat_OpenAI");

class UHMIChat* UHMIChat_OpenAI::GetOrCreateChat_OpenAI(UObject* WorldContextObject,
	FName Name,
	FString ModelName,
	FString BackendUrl,
	FString BackendKey
)
{
	UHMIChat* Proc = UHMISubsystemStatics::GetOrCreateChat(WorldContextObject, ImplName, Name);

	if (BackendUrl.IsEmpty())
	{
		BackendUrl = TEXT("http://127.0.0.1:8080/v1/chat/completions");
	}

	Proc->SetProcessorParam("ModelName", ModelName);
	Proc->SetProcessorParam("BackendUrl", BackendUrl);
	Proc->SetProcessorParam("BackendKey", BackendKey);

	return Proc;
}

UHMIChat_OpenAI::UHMIChat_OpenAI(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ProviderName = ImplName;
	ProcessorName = ImplName.ToString();

	SetProcessorParam("ModelName", TEXT("Llama-3.2_1b_Uncensored_RP_Aesir.gguf"));
	SetProcessorParam("BackendUrl", TEXT("http://127.0.0.1:8080/v1/chat/completions"));
	SetProcessorParam("BackendKey", TEXT(""));

	SetProcessorParam("FrequencyPenalty", 0.0f);
	SetProcessorParam("PresencePenalty", 0.0f);
	SetProcessorParam("Seed", -1);
	SetProcessorParam("N", 1);
	SetProcessorParam("ReasoningEffort", TEXT(""));
}

UHMIChat_OpenAI::UHMIChat_OpenAI(FVTableHelper& Helper) : Super(Helper)
{
}

UHMIChat_OpenAI::~UHMIChat_OpenAI()
{
}

#if HMI_WITH_OPENAI_CHAT

bool UHMIChat_OpenAI::Proc_Init()
{
	if (!Super::Proc_Init())
		return false;

	BackendUrl = GetProcessorString("BackendUrl");
	BackendKey = GetProcessorString("BackendKey");
	HMI_GUARD_PARAM_NOT_EMPTY(BackendUrl);

	ModelName = GetProcessorString("ModelName");
	HMI_GUARD_PARAM_NOT_EMPTY(ModelName);

	FrequencyPenalty = GetProcessorFloat("FrequencyPenalty");
	PresencePenalty = GetProcessorFloat("PresencePenalty");
	Seed = GetProcessorInt("Seed");
	N = GetProcessorInt("N");
	ReasoningEffort = GetProcessorString("ReasoningEffort");

	HttpRequest = MakeUnique<FHMIHttpRequest>(BackendKey);
	HttpRequest->OnProgress.BindUObject(this, &UHMIChat_OpenAI::OnHttpRequestProgress);
	HttpRequest->OnComplete.BindUObject(this, &UHMIChat_OpenAI::OnHttpRequestComplete);

	return true;
}

void UHMIChat_OpenAI::Proc_Release()
{
	HttpRequest.Reset();

	Super::Proc_Release();
}

int64 UHMIChat_OpenAI::ProcessInput(FHMIChatInput&& Input)
{
	const int64 OperationId = EnqueOperation(InputQueue, MoveTemp(Input), GetWorldContext());
	StartOrWakeProcessor();
	return OperationId;
}

void UHMIChat_OpenAI::CancelOperation(bool PurgeQueue)
{
	Super::CancelOperation(PurgeQueue);

	if (HttpRequest)
		HttpRequest->Cancel();

	if (PurgeQueue)
		PurgeInputQueue(InputQueue);
}

static void EmitOptionalString(TSharedRef<FJsonObject> Obj, const TCHAR* Key, const FString& Val)
{
	if (!Val.IsEmpty())
		Obj->SetStringField(Key, Val);
}

static void EmitOptionalNumber(TSharedRef<FJsonObject> Obj, const TCHAR* Key, double Val, double SkipVal)
{
	if (Val != SkipVal)
		Obj->SetNumberField(Key, Val);
}

static void EmitOptionalInt(TSharedRef<FJsonObject> Obj, const TCHAR* Key, int Val, int SkipVal)
{
	if (Val != SkipVal)
		Obj->SetNumberField(Key, (double)Val);
}

static void EmitOptionalStringArray(TSharedRef<FJsonObject> Obj, const TCHAR* Key, const TArray<FString>& Arr)
{
	if (Arr.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> JsonArr;
		for (const FString& S : Arr)
			JsonArr.Add(MakeShared<FJsonValueString>(S));
		Obj->SetArrayField(Key, JsonArr);
	}
}

static void EmitOptionalBool(TSharedRef<FJsonObject> Obj, const TCHAR* Key, bool Val, bool SkipVal)
{
	if (Val != SkipVal)
		Obj->SetBoolField(Key, Val);
}

bool UHMIChat_OpenAI::Proc_DoWork(int& QueueLength)
{
	if (!DequeOperation(InputQueue, Operation, QueueLength))
		return false;

	auto& InputText = Operation.Input.Text;

	if (InputText.IsEmpty())
	{
		HandleOperationComplete(false, FString(TEXT("Empty input")), MoveTemp(Operation.Input));
		return false;
	}

	HMI_PROC_PRE_WORK_STATS(Chat_OpenAI)

	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "<<< \"%s\""), *InputText);

	ResetChatOperation();
	ErrorText.Reset();
	DataChunkPos = 0;
	BadChunkCount = 0;
	CompletionTokens = 0;
	PromptTokens = 0;
	TotalTokens = 0;

	// ---- build messages array ----
	TArray<TSharedPtr<FJsonValue>> JsonMessages;

	for (const auto& Msg : Operation.Input.History)
	{
		TSharedRef<FJsonObject> JsonMsg = MakeShared<FJsonObject>();
		JsonMsg->SetStringField(TEXT("role"), Msg.Role);
		JsonMsg->SetStringField(TEXT("content"), Msg.Text);
		JsonMessages.Add(MakeShared<FJsonValueObject>(JsonMsg));
	}

	{
		TSharedRef<FJsonObject> JsonMsg = MakeShared<FJsonObject>();
		JsonMsg->SetStringField(TEXT("role"), TEXT("user"));
		JsonMsg->SetStringField(TEXT("content"), InputText);
		JsonMessages.Add(MakeShared<FJsonValueObject>(JsonMsg));
	}

	// ---- root request object ----
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("model"), ModelName);
	JsonObject->SetBoolField(TEXT("stream"), true);
	JsonObject->SetArrayField(TEXT("messages"), JsonMessages);

	// ---- sampling params (numbers, not strings) ----
	EmitOptionalNumber(JsonObject, TEXT("temperature"), (double)Operation.Input.Temperature, 1.0);
	EmitOptionalNumber(JsonObject, TEXT("top_p"), (double)Operation.Input.TopP, 1.0);

	// max_tokens / max_completion_tokens
	if (Operation.Input.MaxTokens > 0)
	{
		// api.openai.com uses max_completion_tokens for newer models;
		// most compatible backends (llama.cpp, ollama, vLLM) use max_tokens.
		// Let the user override via BackendParams if needed.
		const bool bUseMaxCompletionTokens = BackendUrl.Contains(TEXT("api.openai.com"));
		JsonObject->SetNumberField(
			bUseMaxCompletionTokens ? TEXT("max_completion_tokens") : TEXT("max_tokens"),
			(double)Operation.Input.MaxTokens
		);
	}

	// frequency_penalty & presence_penalty
	const float FreqPen = Operation.Input.BackendParams.Contains(TEXT("frequency_penalty"))
		? FCString::Atof(*Operation.Input.BackendParams[TEXT("frequency_penalty")])
		: FrequencyPenalty;
	const float PresPen = Operation.Input.BackendParams.Contains(TEXT("presence_penalty"))
		? FCString::Atof(*Operation.Input.BackendParams[TEXT("presence_penalty")])
		: PresencePenalty;
	EmitOptionalNumber(JsonObject, TEXT("frequency_penalty"), (double)FreqPen, 0.0);
	EmitOptionalNumber(JsonObject, TEXT("presence_penalty"), (double)PresPen, 0.0);

	// seed
	const int SeedVal = Operation.Input.BackendParams.Contains(TEXT("seed"))
		? FCString::Atoi(*Operation.Input.BackendParams[TEXT("seed")])
		: Seed;
	EmitOptionalInt(JsonObject, TEXT("seed"), SeedVal, -1);

	// stop
	EmitOptionalStringArray(JsonObject, TEXT("stop"), Operation.Input.Stop);

	// n
	const int NVal = Operation.Input.BackendParams.Contains(TEXT("n"))
		? FCString::Atoi(*Operation.Input.BackendParams[TEXT("n")])
		: N;
	EmitOptionalInt(JsonObject, TEXT("n"), NVal, 1);

	// reasoning_effort (for reasoning models)
	const FString* ReasoningEffortOverride = Operation.Input.BackendParams.Find(TEXT("reasoning_effort"));
	const FString& ReasoningEffortVal = ReasoningEffortOverride ? *ReasoningEffortOverride : ReasoningEffort;
	if (!ReasoningEffortVal.IsEmpty())
		JsonObject->SetStringField(TEXT("reasoning_effort"), ReasoningEffortVal);

	// stream_options: include_usage
	{
		TSharedRef<FJsonObject> StreamOpts = MakeShared<FJsonObject>();
		StreamOpts->SetBoolField(TEXT("include_usage"), true);
		JsonObject->SetObjectField(TEXT("stream_options"), StreamOpts);
	}

	// ---- BackendParams override (pass-through as strings, preserving legacy compatibility) ----
	for (const auto& Iter : Operation.Input.BackendParams)
	{
		// Skip params we already handle natively
		static const TSet<FString> NativeParams = {
			TEXT("frequency_penalty"),
			TEXT("presence_penalty"),
			TEXT("seed"),
			TEXT("n"),
			TEXT("reasoning_effort")
		};
		if (!NativeParams.Contains(Iter.Key))
		{
			JsonObject->SetStringField(Iter.Key, Iter.Value);
		}
	}

	// ---- serialize and send ----
	FString Content;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Content);
	FJsonSerializer::Serialize(JsonObject, Writer);

	const bool Success = HttpRequest->PostAndWait(BackendUrl, TEXT("application/json"), Content);

	if (!HttpRequest->GetCancelFlag())
	{
		HandleOperationComplete(Success, MoveTemp(ErrorText), MoveTemp(Operation.Input));
	}

	return Success;
}

void UHMIChat_OpenAI::Proc_PostWorkStats()
{
	HMI_PROC_POST_WORK_STATS(Chat_OpenAI)
}

void UHMIChat_OpenAI::OnHttpRequestProgress(const FAnsiString& ResponseAnsi, const FAnsiString& Chunk)
{
	// SSE stream format:
	//   data: {"id":"...","choices":[{"delta":{"content":"Hi"},"index":0}],...}\n\n
	//   data: {"id":"...","choices":[{"delta":{},"finish_reason":"stop","index":0}],"usage":{...}}\n\n
	//   data: [DONE]\n\n

	static const FAnsiStringView DataPrefix("data:");
	static const FAnsiStringView EndChunk("\n\n");
	static const FAnsiStringView DoneSentinel("[DONE]");

	const int ResponseLen = ResponseAnsi.Len();
	while (DataChunkPos < ResponseLen)
	{
		FAnsiStringView StreamView(&ResponseAnsi[DataChunkPos], ResponseLen - DataChunkPos);

		const int32 DataIndex = StreamView.Find(DataPrefix);
		if (DataIndex == INDEX_NONE)
		{
			DataChunkPos = ResponseLen;
			break;
		}

		if (DataIndex > 0)
		{
			DataChunkPos += DataIndex;
			continue;
		}

		const int32 EndChunkIndex = StreamView.Find(EndChunk);
		if (EndChunkIndex == INDEX_NONE)
			break;

		DataChunkPos += (EndChunkIndex + EndChunk.Len());

		const int32 ChunkBeginIndex = DataIndex + DataPrefix.Len();
		FAnsiStringView Payload(&StreamView[ChunkBeginIndex], EndChunkIndex - ChunkBeginIndex);

		// Trim leading whitespace
		while (Payload.Len() > 0 && (Payload[0] == ' ' || Payload[0] == '\t'))
			Payload = FAnsiStringView(&Payload[1], Payload.Len() - 1);

		// Skip [DONE] sentinel
		if (Payload.StartsWith(DoneSentinel))
			continue;

		int32 BracketIndex;
		if (Payload.FindChar('{', BracketIndex))
		{
			FAnsiStringView JsonPayload(&Payload[BracketIndex], Payload.Len() - BracketIndex);

			const FUTF8ToTCHAR Conv(JsonPayload.GetData(), JsonPayload.Len());
			FString JsonString(Conv.Length(), Conv.Get());

			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::CreateFromView(JsonString);
			TSharedPtr<FJsonObject> JsonObject;
			if (!FJsonSerializer::Deserialize(Reader, JsonObject))
			{
				++BadChunkCount;
				continue;
			}

			FOpenAIChatCompletionResponse OpenAIResponse;
			if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), &OpenAIResponse))
			{
				++BadChunkCount;
				continue;
			}

			// Backend error
			if (!OpenAIResponse.error.code.IsEmpty() || !OpenAIResponse.error.message.IsEmpty())
			{
				ErrorText = FString::Printf(TEXT(LOGPREFIX "Backend error: code=%s type=%s message=%s"),
					*OpenAIResponse.error.code, *OpenAIResponse.error.type, *OpenAIResponse.error.message);
				++BadChunkCount;
				continue;
			}

			// Delta content (streaming)
			for (const auto& Choice : OpenAIResponse.choices)
			{
				if (!Choice.delta.content.IsEmpty())
				{
					HandleDeltaContent(Operation.Input, FString(Choice.delta.content));
				}
			}

			// Accumulate usage from final chunk
			if (OpenAIResponse.usage.total_tokens > 0)
			{
				PromptTokens = OpenAIResponse.usage.prompt_tokens;
				CompletionTokens = OpenAIResponse.usage.completion_tokens;
				TotalTokens = OpenAIResponse.usage.total_tokens;
			}
		}
	}
}

void UHMIChat_OpenAI::OnHttpRequestComplete(const FAnsiString& ResponseAnsi, int ErrorCode)
{
	if (ErrorCode != 0)
	{
		ErrorText = FString::Printf(TEXT("CURL: %d"), ErrorCode);
		return;
	}

	if (ResponseAnsi.Contains(TEXT("\"error\":")))
	{
		const FUTF8ToTCHAR Conv(ResponseAnsi.GetCharArray().GetData(), ResponseAnsi.GetCharArray().Num());
		FString Response(Conv);

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::CreateFromView(Response);
		TSharedPtr<FJsonObject> JsonObject;
		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			FOpenAIChatCompletionResponse OpenAIResponse;
			if (FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), &OpenAIResponse))
			{
				ErrorText = FString::Printf(TEXT("Backend error: code=%s type=%s message=%s"),
					*OpenAIResponse.error.code, *OpenAIResponse.error.type, *OpenAIResponse.error.message);
				return;
			}
		}

		ErrorText = TEXT("Unknown backend error");
		return;
	}

	if (BadChunkCount > 0 && OutputMsg.IsEmpty() && ErrorText.IsEmpty())
	{
		ErrorText = TEXT("Stream error");
		return;
	}

	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Usage: prompt=%d completion=%d total=%d"),
		PromptTokens, CompletionTokens, TotalTokens);
}

#endif // HMI_WITH_OPENAI_CHAT

#undef LOGPREFIX

// ---- Console variable for BP text chat input ----
//   Set in console:  ChatText Your message here
//   Read in BP:      Get Console Variable String "ChatText"
static TAutoConsoleVariable<FString> CVarChatText(
	TEXT("ChatText"),
	TEXT(""),
	TEXT("Text to send to chat. Write in console, read in BP via Get Console Variable String."),
	ECVF_Default
);
