#include "GGML/HMITextToSpeech_QwenTTS.h"
#include "HMIProcessorImpl.h"
#include "HMISubsystemStatics.h"
#include "HMIBuffer.h"
#include "Audio/HMIResampler.h"
#include "Misc/FileHelper.h"

#if HMI_WITH_QWENTTS
	#include <string>
	#include "HMIThirdPartyBegin.h"
	#include "GGML/QwenTTSLoader.h"
	#include "HMIThirdPartyEnd.h"

	#define QT_CALL(name) QwenTTSApi->name##_fp
#endif

HMI_PROC_DECLARE_STATS(TTS_QwenTTS)

#define LOGPREFIX "[TTS_QwenTTS] "

const FName UHMITextToSpeech_QwenTTS::ImplName = TEXT("TTS_QwenTTS");

//
// GetOrCreateTTS_QwenTTS
//

class UHMITextToSpeech* UHMITextToSpeech_QwenTTS::GetOrCreateTTS_QwenTTS(UObject* WorldContextObject,
	FName Name,
	FString ModelName,
	FString CodecPath,
	EGgmlBackend Backend,
	FString Language,
	FString Instruct,
	FString Speaker,
	FString ReferenceAudioPath,
	FString ReferenceTranscript,
	bool bStreaming,
	int32 GpuLayers,
	int32 MaxNewTokens,
	float VoiceSpeed
)
{
	UHMITextToSpeech* Proc = UHMISubsystemStatics::GetOrCreateTTS(WorldContextObject, ImplName, Name);

	Proc->SetProcessorParam("ModelName", ModelName);
	Proc->SetProcessorParam("CodecPath", CodecPath);
	Proc->SetProcessorParam("Backend", (int32)Backend);
	Proc->SetProcessorParam("Language", Language);
	Proc->SetProcessorParam("Instruct", Instruct);
	Proc->SetProcessorParam("Speaker", Speaker);
	Proc->SetProcessorParam("ReferenceAudioPath", ReferenceAudioPath);
	Proc->SetProcessorParam("ReferenceTranscript", ReferenceTranscript);
	Proc->SetProcessorParam("Streaming", bStreaming ? 1 : 0);
	Proc->SetProcessorParam("GpuLayers", GpuLayers);
	Proc->SetProcessorParam("MaxNewTokens", MaxNewTokens);
	Proc->SetProcessorParam("VoiceSpeed", VoiceSpeed);

	return Proc;
}

UHMITextToSpeech_QwenTTS::UHMITextToSpeech_QwenTTS(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ProviderName = ImplName;
	ProcessorName = ImplName.ToString();

	SetProcessorParam("ModelName", TEXT("qwen-talker-Q8_0.gguf"));
	SetProcessorParam("CodecPath", TEXT("qwen-tokenizer-F32.gguf"));
	SetProcessorParam("Backend", (int32)EGgmlBackend::Auto);
	SetProcessorParam("Language", TEXT(""));
	SetProcessorParam("Instruct", TEXT(""));
	SetProcessorParam("Speaker", TEXT(""));
	SetProcessorParam("ReferenceAudioPath", TEXT(""));
	SetProcessorParam("ReferenceTranscript", TEXT(""));
	SetProcessorParam("Streaming", 0);
	SetProcessorParam("GpuLayers", 0);
	SetProcessorParam("FlashAttention", 1);
	SetProcessorParam("MaxNewTokens", 500);
	SetProcessorParam("Temperature", 0.9f);
	SetProcessorParam("RepetitionPenalty", 1.5f);
	SetProcessorParam("Greedy", 0);
	SetProcessorParam("VoiceSpeed", 1.0f);
}

UHMITextToSpeech_QwenTTS::UHMITextToSpeech_QwenTTS(FVTableHelper& Helper) : Super(Helper)
{
}

UHMITextToSpeech_QwenTTS::~UHMITextToSpeech_QwenTTS()
{
}

#if HMI_WITH_QWENTTS

void UHMITextToSpeech_QwenTTS::StartOrWakeProcessor()
{
	Super::StartOrWakeProcessor();
}

bool UHMITextToSpeech_QwenTTS::Proc_Init()
{
	if (!Super::Proc_Init())
		return false;

	if (!QwenTTSApi)
	{
		ProcError(TEXT("QwenTTSApi is null"));
		return false;
	}

	ModelName = GetProcessorString("ModelName");
	HMI_GUARD_PARAM_NOT_EMPTY(ModelName);

	FString ModelPath = UHMIStatics::LocateDataFile(ModelName);
	if (!FPaths::FileExists(ModelPath))
	{
		ProcError(FString::Printf(TEXT("Model not found: %s"), *ModelName));
		return false;
	}

	CodecPath = GetProcessorString("CodecPath");
	if (CodecPath.IsEmpty())
	{
		CodecPath = TEXT("qwen-tokenizer-F32.gguf");
	}
	FString FullCodecPath = UHMIStatics::LocateDataFile(CodecPath);
	if (!FPaths::FileExists(FullCodecPath))
	{
		ProcError(FString::Printf(TEXT("Codec not found: %s"), *CodecPath));
		return false;
	}

	Language = GetProcessorString("Language");
	Instruct = GetProcessorString("Instruct");
	Speaker = GetProcessorString("Speaker");

	ReferenceAudioPath = GetProcessorString("ReferenceAudioPath");
	if (!ReferenceAudioPath.IsEmpty())
	{
		FString FullRefPath = UHMIStatics::LocateDataFile(ReferenceAudioPath);
		if (!FPaths::FileExists(FullRefPath))
		{
			UE_LOG(LogHMI, Warning, TEXT(LOGPREFIX "Reference audio not found: %s, ignoring"), *ReferenceAudioPath);
			ReferenceAudioPath.Empty();
		}
		else
		{
			ReferenceAudioPath = FullRefPath;
		}
	}

	ReferenceTranscript = GetProcessorString("ReferenceTranscript");
	bStreaming = GetProcessorInt("Streaming") != 0;
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Streaming: %d"), bStreaming ? 1 : 0);
	GpuLayers = GetProcessorInt("GpuLayers");

	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ModelPath: %s"), *ModelPath);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "CodecPath: %s"), *FullCodecPath);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Language: %s"), *Language);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Instruct: %s"), *Instruct);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Speaker: %s"), *Speaker);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ReferenceAudioPath: %s"), *ReferenceAudioPath);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ReferenceTranscript: %s"), *ReferenceTranscript);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "GpuLayers: %d"), GpuLayers);

	Backend = (EGgmlBackend)GetProcessorInt("Backend");
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Backend: %d"), (int32)Backend);

	// Set backend DLL search path to ThirdPartyBinDir so ggml_backend_load_all_from_path finds them
	_putenv_s("GGML_BACKEND_PATH", TCHAR_TO_ANSI(*UHMIStatics::GetPluginThirdPartyBinDir()));
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "GGML_BACKEND_PATH=%s"), *UHMIStatics::GetPluginThirdPartyBinDir());

	// Apply backend selection via GGML_BACKEND env var before qt_init.
	switch (Backend)
	{
		case EGgmlBackend::Cpu:
			_putenv_s("GGML_BACKEND", "CPU");
			UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Forcing CPU via GGML_BACKEND=CPU"));
			break;
		case EGgmlBackend::Vulkan:
			_putenv_s("GGML_BACKEND", "Vulkan0");
			UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Forcing Vulkan via GGML_BACKEND=Vulkan0"));
			break;
		case EGgmlBackend::Cuda:
			_putenv_s("GGML_BACKEND", "CUDA0");
			UE_LOG(LogHMI, Warning, TEXT(LOGPREFIX "CUDA backend may not be compiled"));
			break;
		default:
			_putenv_s("GGML_BACKEND", nullptr);
			UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Auto backend (ggml picks best)"));
			break;
	}

	// Set up log callback
	QT_CALL(qt_log_set)([](enum qt_log_level level, const char* msg, void* user_data) {
		if (level == QT_LOG_ERROR)
		{
			UE_LOG(LogHMI, Error, TEXT(LOGPREFIX "%s"), ANSI_TO_TCHAR(msg));
		}
		else if (level == QT_LOG_WARN)
		{
			UE_LOG(LogHMI, Warning, TEXT(LOGPREFIX "%s"), ANSI_TO_TCHAR(msg));
		}
		else if (level == QT_LOG_INFO)
		{
			UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "%s"), ANSI_TO_TCHAR(msg));
		}
		else
		{
			UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "%s"), ANSI_TO_TCHAR(msg));
		}
	}, nullptr);

	// Initialize context
	std::string ModelPathUtf8(TCHAR_TO_UTF8(*ModelPath));
	std::string CodecPathUtf8(TCHAR_TO_UTF8(*FullCodecPath));

	struct qt_init_params IParams;
	QT_CALL(qt_init_default_params)(&IParams);
	IParams.talker_path = ModelPathUtf8.c_str();
	IParams.codec_path = CodecPathUtf8.c_str();
	IParams.use_fa = (GetProcessorInt("FlashAttention") != 0);
	IParams.clamp_fp16 = (Backend == EGgmlBackend::Vulkan);

	QtContext = QT_CALL(qt_init)(&IParams);
	if (!QtContext)
	{
		const char* ErrMsg = QT_CALL(qt_last_error)();
		ProcError(FString::Printf(TEXT("qt_init failed: %s"), ANSI_TO_TCHAR(ErrMsg ? ErrMsg : "unknown")));
		return false;
	}
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "qt_init OK"));

	ModelSampleRate = 24000; // QwenTTS outputs 24 kHz mono
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ModelSampleRate: %d"), ModelSampleRate);

	if (!UHMIBufferStatics::IsValidSampleRate(ModelSampleRate))
	{
		ProcError(FString::Printf(TEXT("Not supported: ModelSampleRate=%d"), ModelSampleRate));
		return false;
	}

	return true;
}

void UHMITextToSpeech_QwenTTS::Proc_Release()
{
#if HMI_WITH_QWENTTS
	if (QwenTTSApi && QtContext)
	{
		QT_CALL(qt_free)(QtContext);
		QtContext = nullptr;
	}
#endif

	Super::Proc_Release();
}

int64 UHMITextToSpeech_QwenTTS::ProcessInput(FHMITextToSpeechInput&& Input)
{
	const int64 OperationId = EnqueOperation(InputQueue, MoveTemp(Input), GetWorldContext());
	StartOrWakeProcessor();
	return OperationId;
}

void UHMITextToSpeech_QwenTTS::CancelOperation(bool PurgeQueue)
{
	Super::CancelOperation(PurgeQueue);

	if (PurgeQueue)
		PurgeInputQueue(InputQueue);

	QtCancelFlag = true;
}

bool UHMITextToSpeech_QwenTTS::Proc_DoWork(int& QueueLength)
{
	FHMITextToSpeech_QwenTTS_Operation Operation;

	if (!DequeOperation(InputQueue, Operation, QueueLength))
		return false;

	bool Success = false;
	FString ErrorText;
	FHMIWaveHandle OutputWave;

	do
	{
		QtCancelFlag = false;

		std::string InputTextUtf8(TCHAR_TO_UTF8(*Operation.Input.Text));
		if (InputTextUtf8.empty())
		{
			ErrorText = TEXT("Empty input");
			break;
		}

		HMI_PROC_PRE_WORK_STATS(TTS_QwenTTS)

		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "<<< \"%s\""), *Operation.Input.Text);

		// Load reference WAV if provided (for Base mode voice cloning)
		std::string RefAudioPathUtf8;
		std::string RefTranscriptUtf8;

		if (!ReferenceAudioPath.IsEmpty())
		{
			RefAudioPathUtf8 = TCHAR_TO_UTF8(*ReferenceAudioPath);
			RefTranscriptUtf8 = TCHAR_TO_UTF8(*ReferenceTranscript);
		}

		TArray<float> RefSamples;

		if (!RefAudioPathUtf8.empty())
		{
			FString RefPath = UTF8_TO_TCHAR(RefAudioPathUtf8.c_str());

			// Read WAV using UE's FWaveModInfo
			TArray<uint8> RawFileData;
			if (!FFileHelper::LoadFileToArray(RawFileData, *RefPath))
			{
				ErrorText = FString::Printf(TEXT("Failed to read reference audio: %s"), *ReferenceAudioPath);
				break;
			}

			FWaveModInfo WaveInfo;
			if (!WaveInfo.ReadWaveInfo(RawFileData.GetData(), RawFileData.Num()))
			{
				ErrorText = FString::Printf(TEXT("Invalid WAV: %s"), *ReferenceAudioPath);
				break;
			}

			int32 RefSampleRate = (int32)*WaveInfo.pSamplesPerSec;
			int32 RefChannels = (int32)*WaveInfo.pChannels;
			int32 RefDataSize = WaveInfo.SampleDataSize;
			const uint8* SampleDataStart = WaveInfo.SampleDataStart;

			if (!SampleDataStart || RefDataSize <= 0)
			{
				ErrorText = FString::Printf(TEXT("No audio data in WAV: %s"), *ReferenceAudioPath);
				break;
			}

			int32 RefNumFrames = RefDataSize / (RefChannels * 2);

			const int16* SrcData = (const int16*)SampleDataStart;
			int32 TotalSamples = RefNumFrames * RefChannels;
			RefSamples.SetNumUninitialized(TotalSamples);
			for (int32 i = 0; i < TotalSamples; i++)
			{
				RefSamples[i] = (float)SrcData[i] / 32768.0f;
			}

			if (RefChannels > 1)
			{
				TArray<float> MonoSamples;
				MonoSamples.Reserve(RefNumFrames);
				for (int32 i = 0; i < RefSamples.Num(); i += RefChannels)
				{
					float Sum = 0.0f;
					for (int32 c = 0; c < RefChannels; c++)
					{
						Sum += RefSamples[i + c];
					}
					MonoSamples.Add(Sum / (float)RefChannels);
				}
				RefSamples = MoveTemp(MonoSamples);
			}

			if (RefSampleRate != 24000)
			{
				FHMIResampler RefResampler;
				RefResampler.InitDefaultVoice();
				TArray<float> Resampled;
				RefResampler.Convert(RefSamples, RefSampleRate, Resampled, 24000);
				RefSamples = MoveTemp(Resampled);
			}

			UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Reference audio loaded: %d frames, %d Hz -> %d samples at 24 kHz"),
				RefNumFrames, RefSampleRate, RefSamples.Num());
		}

		struct qt_tts_params Params;
		QT_CALL(qt_tts_default_params)(&Params);
		Params.text = InputTextUtf8.c_str();


	// // Debug: hex dump of input text bytes
	// {
	// 	FString HexStr;
	// 	for (size_t k = 0; k < InputTextUtf8.size() && k < 200; k++)
	// 	{
	// 		HexStr += FString::Printf(TEXT("%02X "), (unsigned char)InputTextUtf8[k]);
	// 	}
	// 	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Text UTF-8 hex [%d bytes]: %s"), (int32)InputTextUtf8.size(), *HexStr);
	// 	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Text UTF-8 raw: %s"), ANSI_TO_TCHAR(InputTextUtf8.c_str()));
	// }


		// Language: default to "auto" (language-agnostic mode).
		// The library converts a NULL lang to an empty std::string,
		// which fails the language table lookup in prompt_builder_build.
		std::string LangUtf8;
		if (!Language.IsEmpty())
		{
			LangUtf8 = TCHAR_TO_UTF8(*Language);
		}
		else if (!Operation.Input.VoiceId.IsEmpty())
		{
			LangUtf8 = TCHAR_TO_UTF8(*Operation.Input.VoiceId);
		}
		else
		{
			LangUtf8 = "auto";
		}
		Params.lang = LangUtf8.c_str();

		// Voice design instruct string
		std::string InstructUtf8;
		if (!Instruct.IsEmpty())
		{
			InstructUtf8 = TCHAR_TO_UTF8(*Instruct);
			Params.instruct = InstructUtf8.c_str();
		}

		// Speaker name (CustomVoice mode)
		std::string SpeakerUtf8;
		if (!Speaker.IsEmpty())
		{
			SpeakerUtf8 = TCHAR_TO_UTF8(*Speaker);
			Params.speaker = SpeakerUtf8.c_str();
		}

		// Reference audio for Base mode voice cloning
		if (RefSamples.Num() > 0)
		{
			Params.ref_audio_24k = RefSamples.GetData();
			Params.ref_n_samples = RefSamples.Num();
			if (!RefTranscriptUtf8.empty())
			{
				Params.ref_text = RefTranscriptUtf8.c_str();
			}
		}

		// Sampling control — exposed for tuning against token looping.
		int32 MaxNewTokens = GetProcessorInt("MaxNewTokens");
		if (MaxNewTokens > 0)
		{
			Params.max_new_tokens = MaxNewTokens;
		}

		float Temperature = GetProcessorFloat("Temperature");
		if (Temperature >= 0.0f)
		{
			Params.temperature = Temperature;
			Params.subtalker_temperature = Temperature;
		}

		float RepPen = GetProcessorFloat("RepetitionPenalty");
		if (RepPen >= 1.0f)
		{
			Params.repetition_penalty = RepPen;
		}

		int32 Greedy = GetProcessorInt("Greedy");
		if (Greedy != 0)
		{
			Params.do_sample = false;
			Params.subtalker_do_sample = false;
		}

		// Voice speed — apply via resampling post-synthesis
		float VoiceSpeed = 1.0f;
		if (Operation.Input.Speed > 0.0f)
			VoiceSpeed = Operation.Input.Speed;
		else
			VoiceSpeed = GetProcessorFloat("VoiceSpeed");
		VoiceSpeed = FMath::Clamp(VoiceSpeed, 0.01f, 10.0f);

		// Cancel callback
		Params.cancel = [](void* user_data) -> bool {
			UHMITextToSpeech_QwenTTS* Self = static_cast<UHMITextToSpeech_QwenTTS*>(user_data);
			return Self->CancelFlag || Self->QtCancelFlag;
		};
		Params.cancel_user_data = this;

		// Synthesize with optional streaming
		struct FQwenStreamingCtx { TArray<float>* Acc; UHMITextToSpeech_QwenTTS* Self; const FHMITextToSpeechInput* Input; };
		TArray<float> StreamingAccum;
		FQwenStreamingCtx ChunkCtx;
		if (bStreaming)
		{
			ChunkCtx.Acc = &StreamingAccum;
			ChunkCtx.Self = this;
			ChunkCtx.Input = &Operation.Input;

			Params.on_chunk = [](const float* samples, int n_samples, void* user_data) -> bool {
				auto* C = (FQwenStreamingCtx*)user_data;
				C->Acc->Append(samples, n_samples);
				TArray<float> ChunkSamples(samples, n_samples);
				FHMIWaveHandle ChunkWave(FHMIWaveFormat::Make_PCM_F32(C->Self->ModelSampleRate, 1), ChunkSamples);
				if (C->Input->TTSChunkFunc)
				{
					C->Input->TTSChunkFunc(C->Input->UserTag, ChunkWave, false);
				}
				return true;
			};
			Params.on_chunk_user_data = &ChunkCtx;
			UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Streaming mode enabled"));
		}

		struct qt_audio Audio = { 0 };
		enum qt_status Status = QT_CALL(qt_synthesize)(QtContext, &Params, &Audio);

		HMI_BREAK_ON_CANCEL();

		if (Status != QT_STATUS_OK)
		{
			const char* ErrMsg = QT_CALL(qt_last_error)();
			const char* Msg = (ErrMsg && ErrMsg[0]) ? ErrMsg : nullptr;
			if (Msg)
			{
				ErrorText = FString::Printf(TEXT("qt_synthesize failed (%d): %s"), (int32)Status, ANSI_TO_TCHAR(Msg));
			}
			else if (Status == QT_STATUS_GENERATE_FAILED)
			{
				// prompt_builder_build returns false without calling qt_set_error()
				// for several silent failures. Common causes:
				//   - unknown language: use full names (english, russian, chinese...) not ISO codes
				//   - tokenized prompt too short (<8 tokens)
				//   - no utterance text in prompt
				ErrorText = FString::Printf(TEXT("qt_synthesize failed (-3 / generate_failed). Check: language '%s' is a known full name? text non-empty?"), *Language);
			}
			else
			{
				const char* StatusName = "unknown";
				switch (Status) {
					case QT_STATUS_INVALID_PARAMS:  StatusName = "invalid_params"; break;
					case QT_STATUS_MODE_INVALID:    StatusName = "mode_invalid"; break;
					case QT_STATUS_GENERATE_FAILED: StatusName = "generate_failed"; break;
					case QT_STATUS_OOM:             StatusName = "oom"; break;
					case QT_STATUS_CANCELLED:       StatusName = "cancelled"; break;
				}
				ErrorText = FString::Printf(TEXT("qt_synthesize failed (%d / %s)"), (int32)Status, ANSI_TO_TCHAR(StatusName));
			}
			break;
		}

		// In streaming mode, Audio is empty; use accumulated chunks
		if (bStreaming && StreamingAccum.Num() > 0)
		{
			// Streaming skips peak normalisation; apply it manually
			float Peak = 0.0f;
			for (int32 i = 0; i < StreamingAccum.Num(); i++)
			{
				float AbsVal = FMath::Abs(StreamingAccum[i]);
				if (AbsVal > Peak) Peak = AbsVal;
			}
			if (Peak > 0.0f)
			{
				float Gain = 0.9f / Peak;
				for (int32 i = 0; i < StreamingAccum.Num(); i++)
				{
					StreamingAccum[i] *= Gain;
				}
				UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Streaming peak norm: peak=%.4f gain=%.2f"), Peak, Gain);
			}

			// Apply VoiceSpeed via resampling
			if (VoiceSpeed != 1.0f)
			{
				TArray<float> Stretched;
				if (!Resampler)
				{
					Resampler = MakeUnique<FHMIResampler>();
					Resampler->InitDefaultVoice();
				}
				int StretchedRate = FMath::RoundToInt((float)ModelSampleRate / VoiceSpeed);
				StretchedRate = FMath::Clamp(StretchedRate, 1000, 192000);
				Resampler->Convert(StreamingAccum, ModelSampleRate, Stretched, StretchedRate);
				StreamingAccum = MoveTemp(Stretched);
				UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "VoiceSpeed=%.2f, resampled %d->%d Hz"), VoiceSpeed, ModelSampleRate, StretchedRate);
			}

			OutputWave = GenerateOutputWave(
				TArrayView<const float>(StreamingAccum.GetData(), StreamingAccum.Num()),
				ModelSampleRate,
				HMI_VOICE_CHANNELS
			);
			UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Streaming complete: %d samples"), StreamingAccum.Num());

			// Signal end of stream to the node
			if (Operation.Input.TTSChunkFunc)
			{
				Operation.Input.TTSChunkFunc(Operation.Input.UserTag, OutputWave, true);
			}
		}
		else if (Audio.samples && Audio.n_samples > 0)
		{
			TArrayView<const float> OutSamples(Audio.samples, Audio.n_samples);

			// Apply VoiceSpeed via resampling
			if (VoiceSpeed != 1.0f)
			{
				if (!Resampler)
				{
					Resampler = MakeUnique<FHMIResampler>();
					Resampler->InitDefaultVoice();
				}
				int StretchedRate = FMath::RoundToInt((float)ModelSampleRate / VoiceSpeed);
				StretchedRate = FMath::Clamp(StretchedRate, 1000, 192000);
				Resampler->Convert(OutSamples, ModelSampleRate, TmpF32, StretchedRate);
				OutSamples = TmpF32;
				UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "VoiceSpeed=%.2f, resampled %d->%d Hz"), VoiceSpeed, ModelSampleRate, StretchedRate);
			}

			OutputWave = GenerateOutputWave(
				OutSamples,
				ModelSampleRate,
				HMI_VOICE_CHANNELS
			);
		}
		else
		{
			ErrorText = TEXT("Empty audio output");
			break;
		}

		QT_CALL(qt_audio_free)(&Audio);
		Success = true;
	}
	while (false);

	if (!CancelFlag)
	{
		HandleOperationComplete(Success, MoveTemp(ErrorText), MoveTemp(Operation.Input), MoveTemp(OutputWave));
	}

	return Success;
}

void UHMITextToSpeech_QwenTTS::Proc_PostWorkStats()
{
	HMI_PROC_POST_WORK_STATS(TTS_QwenTTS)
}

void UHMITextToSpeech_QwenTTS::HandleOperationComplete(bool Success, FString&& Error, FHMITextToSpeechInput&& Input, FHMIWaveHandle&& Wave)
{
	if (Success)
	{
		UE_LOG(LogHMI, Log, TEXT(LOGPREFIX ">>> Duration=%.4f"), Wave.GetDuration());
	}

	Wave.SetTag(Input.UserTag);

	FHMITextToSpeechResult Result(GetTimestamp(), Success, MoveTemp(Error), MoveTemp(Wave));

	BroadcastResult(MoveTemp(Input), MoveTemp(Result), OnTextToSpeechComplete);
}

#endif // HMI_WITH_QWENTTS
