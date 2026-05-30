#pragma once

#include "Processors/HMITextToSpeech.h"
#include "GGML/GgmlBackend.h"
#include "HMITextToSpeech_QwenTTS.generated.h"

USTRUCT()
struct HMIBACKEND_API FHMITextToSpeech_QwenTTS_Operation
{
	GENERATED_BODY()

	UPROPERTY()
	FHMITextToSpeechInput Input;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHMIOnQwenTTSChunk, const FHMITextToSpeechInput&, Input, const FHMIWaveHandle&, ChunkWave);

UCLASS()
class HMIBACKEND_API UHMITextToSpeech_QwenTTS :
	public UHMITextToSpeech,
	public THMIProcessorQueue<FHMITextToSpeech_QwenTTS_Operation, FHMITextToSpeechInput>
{
	GENERATED_BODY()

	friend class UHMIBackendSubsystem;

public:

	static const FName ImplName;

	UFUNCTION(BlueprintCallable, Category="HMI|Backend", meta=(WorldContext="WorldContextObject"))
	static class UHMITextToSpeech* GetOrCreateTTS_QwenTTS(UObject* WorldContextObject,
		FName Name = NAME_None,
		FString ModelName = TEXT(""),
		FString CodecPath = TEXT(""),
		EGgmlBackend Backend = EGgmlBackend::Auto,
		FString Language = TEXT(""),
		FString Instruct = TEXT(""),
		FString Speaker = TEXT(""),
		FString ReferenceAudioPath = TEXT(""),
		FString ReferenceTranscript = TEXT(""),
		bool bStreaming = false,
		int32 GpuLayers = 0,
		int32 MaxNewTokens = 500,
		float VoiceSpeed = 1.0f
	);

	UHMITextToSpeech_QwenTTS(const FObjectInitializer& ObjectInitializer);
	UHMITextToSpeech_QwenTTS(FVTableHelper& Helper);
	~UHMITextToSpeech_QwenTTS();

	UPROPERTY(BlueprintAssignable, Category="HMI|TextToSpeech")
	FHMIOnQwenTTSChunk OnQwenTTSChunk;

#if HMI_WITH_QWENTTS

public:

	virtual bool IsProcessorImplemented() const override { return true; }
	virtual void StartOrWakeProcessor() override;
	virtual int64 ProcessInput(FHMITextToSpeechInput&& Input) override;
	virtual void CancelOperation(bool PurgeQueue = false) override;

protected:

	void SetQwenTTSAPI(struct FQwenTTSAPI* Api) { QwenTTSApi = Api; }

	virtual bool Proc_Init() override;
	virtual bool Proc_DoWork(int& QueueLength) override;
	virtual void Proc_PostWorkStats() override;
	virtual void Proc_Release() override;

	virtual void HandleOperationComplete(bool Success, FString&& Error, FHMITextToSpeechInput&& Input, FHMIWaveHandle&& Wave);

	struct FQwenTTSAPI* QwenTTSApi = nullptr;

	struct qt_context* QtContext = nullptr;

	EGgmlBackend Backend = EGgmlBackend::Auto;
	FString CodecPath;
	FString Language;
	FString Instruct;
	FString Speaker;
	FString ReferenceAudioPath;
	FString ReferenceTranscript;
	bool bStreaming = false;
	int32 GpuLayers = 0;
	int32 ModelSampleRate = 24000;

	volatile int32 QtCancelFlag = false;

#endif // HMI_WITH_QWENTTS

protected:

	UPROPERTY(Transient)
	TArray<FHMITextToSpeech_QwenTTS_Operation> InputQueue;
};
