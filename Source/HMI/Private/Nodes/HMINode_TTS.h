#pragma once

#include "Kismet/BlueprintAsyncActionBase.h"
#include "Processors/HMITextToSpeech.h"
#include "HMINode_TTS.generated.h"

UCLASS()
class HMI_API UHMINode_TTS : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

	public:

	UPROPERTY(BlueprintAssignable)
	FHMIOnTextToSpeechComplete Complete;

	UPROPERTY(BlueprintAssignable)
	FHMIOnTTSChunk Chunk;

	protected:

	UFUNCTION(BlueprintCallable, Category="HMI|Async", meta=(DisplayName="TTS", BlueprintInternalUseOnly="true", WorldContext="WorldContextObject"))
	static UHMINode_TTS* TTS_Async(
		UObject* WorldContextObject, UHMIProcessor* Processor,
		FName UserTag, FString Text, FString VoiceId = TEXT("0"), float Speed = 1.0f, bool Streaming = false);

	virtual void Activate() override;

	UPROPERTY(Transient)
	UObject* WorldContext;

	UPROPERTY(Transient)
	UHMITextToSpeech* Processor;

	UPROPERTY(Transient)
	FName UserTag;

	UPROPERTY(Transient)
	FString Text;

	UPROPERTY(Transient)
	FString VoiceId;

	UPROPERTY(Transient)
	float Speed;

	UPROPERTY(Transient)
	bool Streaming;
};
