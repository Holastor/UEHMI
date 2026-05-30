#pragma once

#include "Kismet/BlueprintAsyncActionBase.h"
#include "Processors/HMIChat.h"
#include "HMINode_Prompt.generated.h"

UCLASS()
class HMI_API UHMINode_Prompt : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

	public:

	UPROPERTY(BlueprintAssignable)
	FHMIOnChatBatch Progress;

	protected:

	UFUNCTION(BlueprintCallable, Category="HMI|Async", meta=(DisplayName="Prompt", BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", AutoCreateRefTerm="History,BackendParams,Stop"))
	static UHMINode_Prompt* Prompt_Async(
		UObject* WorldContextObject, UHMIProcessor* Processor,
		FName UserTag, FString Text,
		TArray<FHMIChatMessage> History, TMap<FString, FString> BackendParams,
		const TArray<FString>& Stop,
		int MaxTokens = 0, float Temperature = 1.0f, float TopP = 1.0f, bool Streaming = true,
		float FrequencyPenalty = 0.0f, float PresencePenalty = 0.0f,
		int Seed = -1, int N = 1, const FString& ReasoningEffort = TEXT("")
	);

	virtual void Activate() override;

	UPROPERTY(Transient)
	UObject* WorldContext;

	UPROPERTY(Transient)
	UHMIChat* Processor;

	UPROPERTY(Transient)
	FName UserTag;

	UPROPERTY(Transient)
	FString Text;

	UPROPERTY(Transient)
	TArray<FHMIChatMessage> History;

	UPROPERTY(Transient)
	TMap<FString, FString> BackendParams;

	UPROPERTY(Transient)
	int MaxTokens;

	UPROPERTY(Transient)
	float Temperature;

	UPROPERTY(Transient)
	float TopP;

	UPROPERTY(Transient)
	float FrequencyPenalty;

	UPROPERTY(Transient)
	float PresencePenalty;

	UPROPERTY(Transient)
	int Seed;

	UPROPERTY(Transient)
	TArray<FString> Stop;

	UPROPERTY(Transient)
	int N;

	UPROPERTY(Transient)
	FString ReasoningEffort;

	UPROPERTY(Transient)
	bool Streaming;
};
