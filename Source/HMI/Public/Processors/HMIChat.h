#pragma once

#include "HMIProcessor.h"
#include "HMIChat.generated.h"

USTRUCT(BlueprintType, Category="HMI|Chat")
struct HMI_API FHMIChatMessage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMI")
	FString Role;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HMI")
	FString Text;
};

USTRUCT(BlueprintType, Category="HMI|Chat")
struct HMI_API FHMIChatInput : public FHMIProcessorInput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	FString Text;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	TArray<FHMIChatMessage> History;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	TMap<FString, FString> BackendParams;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	int MaxTokens = 0;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	float Temperature = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	float TopP = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	float FrequencyPenalty = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	float PresencePenalty = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	int Seed = -1;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	TArray<FString> Stop;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	int N = 1;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	FString ReasoningEffort;

	using TProgressFunc = TFunction<void(const FName& /*UserTag*/, const FString& /*Text*/, bool /*EndOfText*/)>;
	TProgressFunc ChatDeltaFunc; // SPAM WARNING!
	TProgressFunc ChatBatchFunc;

	FHMIChatInput() = default;

	FHMIChatInput(FName InUserTag, FString InText,
		TArray<FHMIChatMessage> InHistory, TMap<FString, FString> InBackendParams,
		int InMaxTokens = 0, float InTemperature = 1.0f, float InTopP = 1.0f)
	{
		UserTag = MoveTemp(InUserTag);
		Text = MoveTemp(InText);
		History = MoveTemp(InHistory);
		BackendParams = MoveTemp(InBackendParams);
		MaxTokens = InMaxTokens;
		Temperature = InTemperature;
		TopP = InTopP;
	}
};

USTRUCT(BlueprintType, Category="HMI|Chat")
struct HMI_API FHMIChatResult : public FHMIProcessorResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	FString Text;

	FHMIChatResult() = default;

	FHMIChatResult(double InTimestamp, bool InSuccess, FString InError, FString InText) : 
		FHMIProcessorResult(InTimestamp, InSuccess, MoveTemp(InError)),
		Text(MoveTemp(InText))
	{}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FHMIOnChatDelta, const FName&, UserTag, const FString&, DeltaText, bool, EndOfText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FHMIOnChatBatch, const FName&, UserTag, const FString&, BatchText, bool, EndOfText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHMIOnChatComplete, const FHMIChatInput&, Input, const FHMIChatResult&, Result);

UCLASS(BlueprintType, Abstract, ClassGroup="HMI|Chat")
class HMI_API UHMIChat : public UHMIProcessor
{
	GENERATED_BODY()

	public:

	UHMIChat(const FObjectInitializer& ObjectInitializer);
	UHMIChat(FVTableHelper& Helper);
	~UHMIChat();

	/* WARNING: AVOID USING THIS IN BP */
	UPROPERTY(BlueprintAssignable, Category="HMI|Chat")
	FHMIOnChatDelta OnChatDelta;

	UPROPERTY(BlueprintAssignable, Category="HMI|Chat")
	FHMIOnChatBatch OnChatBatch;

	UPROPERTY(BlueprintAssignable, Category="HMI|Chat")
	FHMIOnChatComplete OnChatComplete;

	UFUNCTION(BlueprintCallable, Category="HMI|Chat", meta=(AutoCreateRefTerm="SplitCharacters"))
	virtual void SetupBatching(bool Enable = true, int MinLen = 10, int MaxLen = 200, int SpaceSplitThresh = 100, FString SplitCharacters = TEXT(".!?"));

	UFUNCTION(BlueprintCallable, Category="HMI|Chat", meta=(AutoCreateRefTerm="History,BackendParams"))
	virtual int64 ProcessText(FName UserTag, FString Text, 
		TArray<FHMIChatMessage> History, TMap<FString, FString> BackendParams,
		int MaxTokens = 0, float Temperature = 1.0f, float TopP = 1.0f)
	{ 
		return ProcessInput(FHMIChatInput(MoveTemp(UserTag), MoveTemp(Text), MoveTemp(History), MoveTemp(BackendParams), MaxTokens, Temperature, TopP));
	}
	
	virtual int64 ProcessInput(FHMIChatInput&& Input) { return 0; }

	protected:

	virtual bool Proc_Init() override;

	virtual void ResetChatOperation();
	virtual void HandleDeltaContent(const FHMIChatInput& Input, FString&& Delta);
	virtual void DetectThinkTags(const FHMIChatInput& Input);
	virtual void AccumulateBatch(const FHMIChatInput& Input);
	virtual void HandleOperationComplete(bool Success, FString&& Error, FHMIChatInput&& Input);

	virtual void BroadcastChatDelta(const FHMIChatInput& Input, FString&& Delta, bool EndOfText);
	virtual void BroadcastChatBatch(const FHMIChatInput& Input, FString&& Batch, bool EndOfText);
	virtual void BroadcastChatResult(bool Success, FString&& Error, FHMIChatInput&& Input);

	protected:

	FString ModelName;

	bool EnableBatching = false;
	int MinBatchLength = 0;
	int MaxBatchLength = 0;
	int SpaceSplitThreshold = 0;
	FString BatchSplitCharacters;

	FString OutputMsg;
	int ThinkStartIdx = 0;
	int ThinkEndIdx = 0;
	int ThinkScanOffset = 0;
	bool ThinkFlag = false;
	int BatchPos = 0;
};
