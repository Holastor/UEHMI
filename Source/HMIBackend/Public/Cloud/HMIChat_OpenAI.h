#pragma once

#include "Processors/HMIChat.h"
#include "Http/OpenAITypes.h"
#include "HMIChat_OpenAI.generated.h"

USTRUCT()
struct HMIBACKEND_API FHMIChat_OpenAI_Operation
{
	GENERATED_BODY()

	UPROPERTY()
	FHMIChatInput Input;
};

UCLASS()
class HMIBACKEND_API UHMIChat_OpenAI : 
	public UHMIChat, 
	public THMIProcessorQueue<FHMIChat_OpenAI_Operation, FHMIChatInput>
{
	GENERATED_BODY()

	public:

	static const FName ImplName;

	UFUNCTION(BlueprintCallable, Category="HMI|Backend", meta=(WorldContext="WorldContextObject"))
	static class UHMIChat* GetOrCreateChat_OpenAI(UObject* WorldContextObject, 
		FName Name = NAME_None,
		FString ModelName = TEXT("gpt-5-nano"),
		FString BackendUrl = TEXT("https://api.openai.com/v1/chat/completions"),
		FString BackendKey = TEXT("")
	);

	UHMIChat_OpenAI(const FObjectInitializer& ObjectInitializer);
	UHMIChat_OpenAI(FVTableHelper& Helper);
	~UHMIChat_OpenAI();

	#if HMI_WITH_OPENAI_CHAT

	public:

	virtual bool IsProcessorImplemented() const override { return true; }
	virtual int64 ProcessInput(FHMIChatInput&& Input) override;
	virtual void CancelOperation(bool PurgeQueue = false) override;

	protected:

	virtual bool Proc_Init() override;
	virtual bool Proc_DoWork(int& QueueLength) override;
	virtual void Proc_PostWorkStats() override;
	virtual void Proc_Release() override;

	void OnHttpRequestProgress(const FAnsiString& ResponseAnsi, const FAnsiString& Chunk);
	void OnHttpRequestComplete(const FAnsiString& ResponseAnsi, int ErrorCode);

	FString BackendUrl;
	FString BackendKey;

	float FrequencyPenalty = 0.0f;
	float PresencePenalty = 0.0f;
	int Seed = -1;
	int N = 1;
	FString ReasoningEffort;
	TUniquePtr<class FHMIHttpRequest> HttpRequest;
	FString ErrorText;
	int DataChunkPos = 0;
	int BadChunkCount = 0;

	int CompletionTokens = 0;
	int PromptTokens = 0;
	int TotalTokens = 0;

	#endif // HMI_WITH_OPENAI_CHAT

	protected:

	UPROPERTY(Transient)
	TArray<FHMIChat_OpenAI_Operation> InputQueue;

	UPROPERTY(Transient)
	FHMIChat_OpenAI_Operation Operation;
};
