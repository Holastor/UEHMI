#pragma once

#include "Processors/HMITextToSpeech.h"
#include "GGML/GgmlBackend.h"
#include "HMITextToSpeech_OmniVoice.generated.h"

USTRUCT()
struct HMIBACKEND_API FHMITextToSpeech_OmniVoice_Operation
{
	GENERATED_BODY()

	UPROPERTY()
	FHMITextToSpeechInput Input;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHMIOnOmniVoiceTTSChunk, const FHMITextToSpeechInput&, Input, const FHMIWaveHandle&, ChunkWave);

UCLASS()
class HMIBACKEND_API UHMITextToSpeech_OmniVoice :
	public UHMITextToSpeech,
	public THMIProcessorQueue<FHMITextToSpeech_OmniVoice_Operation, FHMITextToSpeechInput>
{
	GENERATED_BODY()

	friend class UHMIBackendSubsystem;

public:

	static const FName ImplName;

	UFUNCTION(BlueprintCallable, Category="HMI|Backend", meta=(WorldContext="WorldContextObject"))
	static class UHMITextToSpeech* GetOrCreateTTS_OmniVoice(UObject* WorldContextObject,
		FName Name = NAME_None,
		FString ModelName = TEXT(""),
		FString CodecPath = TEXT(""),
		EGgmlBackend Backend = EGgmlBackend::Auto,
		FString Language = TEXT(""),
		FString Instruct = TEXT(""),
		FString ReferenceAudioPath = TEXT(""),
		FString ReferenceTranscript = TEXT(""),
		bool bCacheReference = false,
		bool bStreaming = false,
		int32 MaskGITSteps = 32,
		int32 GpuLayers = 0,
		float VoiceSpeed = 1.0f
	);

	UHMITextToSpeech_OmniVoice(const FObjectInitializer& ObjectInitializer);
	UHMITextToSpeech_OmniVoice(FVTableHelper& Helper);
	~UHMITextToSpeech_OmniVoice();

	UPROPERTY(BlueprintAssignable, Category="HMI|TextToSpeech")
	FHMIOnOmniVoiceTTSChunk OnOmniVoiceTTSChunk;

#if HMI_WITH_OMNIVOICE

public:

	virtual bool IsProcessorImplemented() const override { return true; }
	virtual void StartOrWakeProcessor() override;
	virtual int64 ProcessInput(FHMITextToSpeechInput&& Input) override;
	virtual void CancelOperation(bool PurgeQueue = false) override;

protected:

	void SetOmniVoiceAPI(struct FOmniVoiceAPI* Api) { OmniVoiceApi = Api; }

	virtual bool Proc_Init() override;
	virtual bool Proc_DoWork(int& QueueLength) override;
	virtual void Proc_PostWorkStats() override;
	virtual void Proc_Release() override;

	virtual void HandleOperationComplete(bool Success, FString&& Error, FHMITextToSpeechInput&& Input, FHMIWaveHandle&& Wave);

	struct FOmniVoiceAPI* OmniVoiceApi = nullptr;

	struct ov_context* OvContext = nullptr;

	EGgmlBackend Backend = EGgmlBackend::Auto;
	FString CodecPath;
	FString Language;
	FString Instruct;
	FString ReferenceAudioPath;
	FString ReferenceTranscript;
	bool bCacheReference = false;
	bool bStreaming = false;
	int32 MaskGITSteps = 32;
	int32 GpuLayers = 0;
	int32 ModelSampleRate = 24000;

	volatile int32 OvCancelFlag = false;

#endif // HMI_WITH_OMNIVOICE

protected:

	UPROPERTY(Transient)
	TArray<FHMITextToSpeech_OmniVoice_Operation> InputQueue;
};
