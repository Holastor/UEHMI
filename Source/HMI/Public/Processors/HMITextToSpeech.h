#pragma once

#include "HMIProcessor.h"
#include "HMITextToSpeech.generated.h"

USTRUCT(BlueprintType, Category="HMI|TextToSpeech")
struct HMI_API FHMITextToSpeechInput : public FHMIProcessorInput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	FString Text;

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	FString VoiceId;

	UPROPERTY(BlueprintReadWrite, Category="HMI", meta=(ClampMin="0.01", ClampMax="10.0", UIMin="0.01", UIMax="10.0"))
	float Speed = 1.0f;

	using TChunkFunc = TFunction<void(const FName& /*UserTag*/, const FHMIWaveHandle& /*ChunkWave*/, bool /*EndOfStream*/)>;
	TChunkFunc TTSChunkFunc;

	FHMITextToSpeechInput() = default;

	FHMITextToSpeechInput(FName InUserTag, FString InText, FString InVoiceId, float InSpeed)
	{
		UserTag = MoveTemp(InUserTag);
		Text = MoveTemp(InText);
		VoiceId = MoveTemp(InVoiceId);
		Speed = InSpeed;
	}
};

USTRUCT(BlueprintType, Category="HMI|TextToSpeech")
struct HMI_API FHMITextToSpeechResult : public FHMIProcessorResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="HMI")
	FHMIWaveHandle Wave;

	FHMITextToSpeechResult() = default;

	FHMITextToSpeechResult(double InTimestamp, bool InSuccess, FString InError, FHMIWaveHandle InWave) : 
		FHMIProcessorResult(InTimestamp, InSuccess, MoveTemp(InError)),
		Wave(MoveTemp(InWave))
	{}
};

USTRUCT(BlueprintType, Category="HMI|TextToSpeech")
struct HMI_API FHMITTSChunkOutput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="HMI")
	FHMIWaveHandle ChunkWave;

	UPROPERTY(BlueprintReadOnly, Category="HMI")
	bool EndOfStream = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHMIOnTextToSpeechComplete, const FHMITextToSpeechInput&, Input, const FHMITextToSpeechResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHMIOnTTSChunk, FName, UserTag, const FHMITTSChunkOutput&, Chunk);

UCLASS(BlueprintType, Abstract, ClassGroup="HMI|TextToSpeech")
class HMI_API UHMITextToSpeech : public UHMIProcessor
{
	GENERATED_BODY()

	public:

	UHMITextToSpeech(const FObjectInitializer& ObjectInitializer);
	UHMITextToSpeech(FVTableHelper& Helper);
	~UHMITextToSpeech();

	UPROPERTY(BlueprintAssignable, Category="HMI|TextToSpeech")
	FHMIOnTextToSpeechComplete OnTextToSpeechComplete;

	UFUNCTION(BlueprintCallable, Category="HMI|TextToSpeech")
	virtual void SetVoiceId(const FString& Value) { SetProcessorParam("VoiceId", Value); }

	UFUNCTION(BlueprintCallable, Category="HMI|TextToSpeech")
	virtual void SetVoiceSpeed(float Value) { SetProcessorParam("VoiceSpeed", Value); }

	UFUNCTION(BlueprintCallable, Category="HMI|TextToSpeech")
	virtual int64 ProcessText(FName UserTag, FString Text, FString VoiceId, float Speed)
	{
		return ProcessInput(FHMITextToSpeechInput(MoveTemp(UserTag), MoveTemp(Text), MoveTemp(VoiceId), Speed));
	}

	virtual int64 ProcessInput(FHMITextToSpeechInput&& Input) { return 0; }

	protected:

	virtual bool Proc_Init() override;

	FString ModelName;
	int OutputSampleRate = HMI_DEFAULT_SAMPLE_RATE;
	int OutputNumChannels = HMI_VOICE_CHANNELS;
	float OutputClampAbs = 0.9f;

	virtual FHMIWaveHandle GenerateOutputWave(TArray<float>& Samples, int SampleRate, int NumChannels);
	virtual FHMIWaveHandle GenerateOutputWave(TArrayView<const float> Samples, int SampleRate, int NumChannels);
	virtual FHMIWaveHandle GenerateOutputWave(TArrayView<const int16> Samples, int SampleRate, int NumChannels);

	TUniquePtr<class FHMIResampler> Resampler;
	TArray<float> TmpF32;
	TArray<int16> TmpS16;
};
