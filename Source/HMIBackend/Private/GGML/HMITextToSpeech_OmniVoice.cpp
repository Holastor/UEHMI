#include "GGML/HMITextToSpeech_OmniVoice.h"
#include "HMIProcessorImpl.h"
#include "HMISubsystemStatics.h"
#include "HMIBuffer.h"
#include "Audio/HMIResampler.h"
#include "Misc/FileHelper.h"

#if HMI_WITH_OMNIVOICE
	#include <string>
	#include "HMIThirdPartyBegin.h"
	#include "GGML/OmniVoiceLoader.h"
	#include "HMIThirdPartyEnd.h"

	#define OV_CALL(name) OmniVoiceApi->name##_fp
#endif

HMI_PROC_DECLARE_STATS(TTS_OmniVoice)

#define LOGPREFIX "[TTS_OmniVoice] "

const FName UHMITextToSpeech_OmniVoice::ImplName = TEXT("TTS_OmniVoice");

//
// GetOrCreateTTS_OmniVoice
//

class UHMITextToSpeech* UHMITextToSpeech_OmniVoice::GetOrCreateTTS_OmniVoice(UObject* WorldContextObject,
	FName Name,
	FString ModelName,
	FString CodecPath,
	EGgmlBackend Backend,
	FString Language,
	FString Instruct,
	FString ReferenceAudioPath,
	FString ReferenceTranscript,
	bool bCacheReference,
	bool bStreaming,
	int32 MaskGITSteps,
	int32 GpuLayers,
	float VoiceSpeed
)
{
	UHMITextToSpeech* Proc = UHMISubsystemStatics::GetOrCreateTTS(WorldContextObject, ImplName, Name);

	Proc->SetProcessorParam("ModelName", ModelName);
	Proc->SetProcessorParam("CodecPath", CodecPath);
	Proc->SetProcessorParam("Backend", (int32)Backend);
	Proc->SetProcessorParam("Language", Language);
	Proc->SetProcessorParam("Instruct", Instruct);
	Proc->SetProcessorParam("ReferenceAudioPath", ReferenceAudioPath);
	Proc->SetProcessorParam("ReferenceTranscript", ReferenceTranscript);
	Proc->SetProcessorParam("CacheReference", bCacheReference ? 1 : 0);
	Proc->SetProcessorParam("Streaming", bStreaming ? 1 : 0);
	Proc->SetProcessorParam("MaskGITSteps", MaskGITSteps);
	Proc->SetProcessorParam("GpuLayers", GpuLayers);
	Proc->SetProcessorParam("VoiceSpeed", VoiceSpeed);

	return Proc;
}

UHMITextToSpeech_OmniVoice::UHMITextToSpeech_OmniVoice(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ProviderName = ImplName;
	ProcessorName = ImplName.ToString();

	SetProcessorParam("ModelName", TEXT("omnivoice-base-Q8_0.gguf"));
	SetProcessorParam("CodecPath", TEXT("omnivoice-tokenizer-F32.gguf"));
	SetProcessorParam("Backend", (int32)EGgmlBackend::Auto);
	SetProcessorParam("Language", TEXT(""));
	SetProcessorParam("Instruct", TEXT(""));
	SetProcessorParam("ReferenceAudioPath", TEXT(""));
	SetProcessorParam("ReferenceTranscript", TEXT(""));
	SetProcessorParam("CacheReference", 0);
	SetProcessorParam("Streaming", 0);
	SetProcessorParam("MaskGITSteps", 32);
	SetProcessorParam("FlashAttention", 1);
	SetProcessorParam("GpuLayers", 0);
	SetProcessorParam("VoiceSpeed", 1.0f);
}

UHMITextToSpeech_OmniVoice::UHMITextToSpeech_OmniVoice(FVTableHelper& Helper) : Super(Helper)
{
}

UHMITextToSpeech_OmniVoice::~UHMITextToSpeech_OmniVoice()
{
}

#if HMI_WITH_OMNIVOICE

void UHMITextToSpeech_OmniVoice::StartOrWakeProcessor()
{
	Super::StartOrWakeProcessor();
}

bool UHMITextToSpeech_OmniVoice::Proc_Init()
{
	if (!Super::Proc_Init())
		return false;

	if (!OmniVoiceApi)
	{
		ProcError(TEXT("OmniVoiceApi is null"));
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
		CodecPath = TEXT("omnivoice-tokenizer-F32.gguf");
	}
	FString FullCodecPath = UHMIStatics::LocateDataFile(CodecPath);
	if (!FPaths::FileExists(FullCodecPath))
	{
		ProcError(FString::Printf(TEXT("Codec not found: %s"), *CodecPath));
		return false;
	}

	Language = GetProcessorString("Language");
	Instruct = GetProcessorString("Instruct");

	ReferenceAudioPath = GetProcessorString("ReferenceAudioPath");
	if (!ReferenceAudioPath.IsEmpty())
	{
		FString FullRefPath = UHMIStatics::LocateDataFile(ReferenceAudioPath);
		if (!FPaths::FileExists(FullRefPath))
		{
			UE_LOG(LogHMI, Warning, TEXT(LOGPREFIX "Reference audio not found: %s, will use voice design"), *ReferenceAudioPath);
			ReferenceAudioPath.Empty();
		}
		else
		{
			ReferenceAudioPath = FullRefPath;
		}
	}

	ReferenceTranscript = GetProcessorString("ReferenceTranscript");
	bCacheReference = GetProcessorInt("CacheReference") != 0;
	bStreaming = GetProcessorInt("Streaming") != 0;
	MaskGITSteps = GetProcessorInt("MaskGITSteps");
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "CacheReference: %d Streaming: %d"), bCacheReference ? 1 : 0, bStreaming ? 1 : 0);
	GpuLayers = GetProcessorInt("GpuLayers");

	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ModelPath: %s"), *ModelPath);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "CodecPath: %s"), *FullCodecPath);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Language: %s"), *Language);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Instruct: %s"), *Instruct);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ReferenceAudioPath: %s"), *ReferenceAudioPath);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ReferenceTranscript: %s"), *ReferenceTranscript);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "GpuLayers: %d"), GpuLayers);

	Backend = (EGgmlBackend)GetProcessorInt("Backend");
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Backend: %d"), (int32)Backend);

	// Set backend DLL search path to ThirdPartyBinDir so ggml_backend_load_all_from_path finds them
	_putenv_s("GGML_BACKEND_PATH", TCHAR_TO_ANSI(*UHMIStatics::GetPluginThirdPartyBinDir()));
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "GGML_BACKEND_PATH=%s"), *UHMIStatics::GetPluginThirdPartyBinDir());

	// Apply backend selection via GGML_BACKEND env var before ov_init.
	// omnivoice/backend.h reads GGML_BACKEND to pick a specific device.
	// Valid values: CPU, Vulkan0, CUDA0 (see ggml_backend_dev_name).
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
	OV_CALL(ov_log_set)([](enum ov_log_level level, const char* msg, void* user_data) {
		if (level == OV_LOG_ERROR)
		{
			UE_LOG(LogHMI, Error, TEXT(LOGPREFIX "%s"), ANSI_TO_TCHAR(msg));
		}
		else if (level == OV_LOG_WARN)
		{
			UE_LOG(LogHMI, Warning, TEXT(LOGPREFIX "%s"), ANSI_TO_TCHAR(msg));
		}
		else if (level == OV_LOG_INFO)
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

	struct ov_init_params IParams;
	OV_CALL(ov_init_default_params)(&IParams);
	IParams.model_path = ModelPathUtf8.c_str();
	IParams.codec_path = CodecPathUtf8.c_str();
	IParams.use_fa = (GetProcessorInt("FlashAttention") != 0);
	IParams.clamp_fp16 = (Backend == EGgmlBackend::Vulkan);

	OvContext = OV_CALL(ov_init)(&IParams);
	if (!OvContext)
	{
		const char* ErrMsg = OV_CALL(ov_last_error)();
		ProcError(FString::Printf(TEXT("ov_init failed: %s"), ANSI_TO_TCHAR(ErrMsg ? ErrMsg : "unknown")));
		return false;
	}
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ov_init OK"));

	ModelSampleRate = 24000; // OmniVoice outputs 24 kHz mono
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ModelSampleRate: %d"), ModelSampleRate);

	if (!UHMIBufferStatics::IsValidSampleRate(ModelSampleRate))
	{
		ProcError(FString::Printf(TEXT("Not supported: ModelSampleRate=%d"), ModelSampleRate));
		return false;
	}

	return true;
}

void UHMITextToSpeech_OmniVoice::Proc_Release()
{
#if HMI_WITH_OMNIVOICE
	if (OmniVoiceApi && OvContext)
	{
		OV_CALL(ov_free)(OvContext);
		OvContext = nullptr;
	}
#endif

	Super::Proc_Release();
}

int64 UHMITextToSpeech_OmniVoice::ProcessInput(FHMITextToSpeechInput&& Input)
{
	const int64 OperationId = EnqueOperation(InputQueue, MoveTemp(Input), GetWorldContext());
	StartOrWakeProcessor();
	return OperationId;
}

void UHMITextToSpeech_OmniVoice::CancelOperation(bool PurgeQueue)
{
	Super::CancelOperation(PurgeQueue);

	if (PurgeQueue)
		PurgeInputQueue(InputQueue);

	OvCancelFlag = true;
}

bool UHMITextToSpeech_OmniVoice::Proc_DoWork(int& QueueLength)
{
	FHMITextToSpeech_OmniVoice_Operation Operation;

	if (!DequeOperation(InputQueue, Operation, QueueLength))
		return false;

	bool Success = false;
	FString ErrorText;
	FHMIWaveHandle OutputWave;

	do
	{
		OvCancelFlag = false;

		std::string InputTextUtf8(TCHAR_TO_UTF8(*Operation.Input.Text));
		if (InputTextUtf8.empty())
		{
			ErrorText = TEXT("Empty input");
			break;
		}

		HMI_PROC_PRE_WORK_STATS(TTS_OmniVoice)

		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "<<< \"%s\""), *Operation.Input.Text);

		// Resolve reference audio for voice cloning
		std::string RefAudioPathUtf8;
		std::string RefTranscriptUtf8;

		if (!ReferenceAudioPath.IsEmpty())
		{
			RefAudioPathUtf8 = TCHAR_TO_UTF8(*ReferenceAudioPath);
			RefTranscriptUtf8 = TCHAR_TO_UTF8(*ReferenceTranscript);
		}

		// Load reference WAV if provided (for voice cloning)
		// Use cache if enabled and already loaded
				// Load reference WAV if provided (for voice cloning)
		// Static cache keyed by path survives processor recreation
		TArray<float> RefSamples;

		if (!RefAudioPathUtf8.empty())
		{
			FString RefPath = UTF8_TO_TCHAR(RefAudioPathUtf8.c_str());

			// Static cache: path -> resampled samples
			static TMap<FString, TArray<float>> StaticRefCache;

			if (bCacheReference)
			{
				TArray<float>* Cached = StaticRefCache.Find(RefPath);
				if (Cached && Cached->Num() > 0)
				{
					RefSamples = *Cached;
					UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Using cached reference: %d samples"), RefSamples.Num());
				}
			}

			if (RefSamples.Num() == 0)
			{
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

				if (bCacheReference)
				{
					StaticRefCache.Add(RefPath, RefSamples);
					UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Reference cached for reuse"));
				}
			}
		}

		
		// Cached encoded reference tokens (static, survives processor recreation)
		struct FCachedRefTokens {
			TArray<int32> Tokens;
			int32 T = 0;
		};
		static TMap<FString, FCachedRefTokens> StaticTokenCache;

		// Pass reference as pre-encoded tokens if available, otherwise raw audio
		FCachedRefTokens* CachedTokens = nullptr;
		if (bCacheReference && !RefAudioPathUtf8.empty())
		{
			FString CacheKey = UTF8_TO_TCHAR(RefAudioPathUtf8.c_str());
			CachedTokens = StaticTokenCache.Find(CacheKey);
			if (CachedTokens && CachedTokens->Tokens.Num() > 0)
			{
				UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Using cached tokens: T=%d"), CachedTokens->T);
			}
			else if (RefSamples.Num() > 0)
			{
				// Encode reference audio to tokens once and cache them
				int32_t* EncodedTokens = nullptr;
				int T = 0;
				enum ov_status EncStatus = OV_CALL(ov_encode_reference_audio)(OvContext,
					RefSamples.GetData(), RefSamples.Num(), &EncodedTokens, &T);
				if (EncStatus == OV_STATUS_OK && EncodedTokens && T > 0)
				{
					int32 TotalCodes = T * 8; // K=8 codebooks
					FCachedRefTokens NewCache;
					NewCache.Tokens.SetNum(TotalCodes);
					FMemory::Memcpy(NewCache.Tokens.GetData(), EncodedTokens, TotalCodes * sizeof(int32_t));
					NewCache.T = T;
					StaticTokenCache.Add(CacheKey, MoveTemp(NewCache));
					CachedTokens = StaticTokenCache.Find(CacheKey);
					OV_CALL(ov_audio_tokens_free)(EncodedTokens);
					UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Tokens encoded and cached: T=%d"), T);
				}
				else
				{
					UE_LOG(LogHMI, Warning, TEXT(LOGPREFIX "Token encoding failed: %d"), (int32)EncStatus);
				}
			}
		}
struct ov_tts_params Params;
		OV_CALL(ov_tts_default_params)(&Params);
		Params.mg_num_step = MaskGITSteps;
		Params.text = InputTextUtf8.c_str();

		// Language: map from Unreal convention or pass through
		std::string LangUtf8;
		if (!Operation.Input.VoiceId.IsEmpty() && Language.IsEmpty())
		{
			// Use VoiceId as language hint if not explicitly set
			LangUtf8 = TCHAR_TO_UTF8(*Operation.Input.VoiceId);
			Params.lang = LangUtf8.c_str();
		}
		else if (!Language.IsEmpty())
		{
			LangUtf8 = TCHAR_TO_UTF8(*Language);
			Params.lang = LangUtf8.c_str();
		}
		// else: auto-detect

		// Voice design instruct string
		std::string InstructUtf8;
		if (!Instruct.IsEmpty())
		{
			InstructUtf8 = TCHAR_TO_UTF8(*Instruct);
			Params.instruct = InstructUtf8.c_str();
		}

				// Reference audio for voice cloning: use cached tokens if available, raw audio otherwise
		if (CachedTokens && CachedTokens->T > 0)
		{
			Params.ref_audio_tokens = CachedTokens->Tokens.GetData();
			Params.ref_T = CachedTokens->T;
			if (!RefTranscriptUtf8.empty())
			{
				Params.ref_text = RefTranscriptUtf8.c_str();
			}
			UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Using pre-encoded tokens: T=%d"), CachedTokens->T);
		}
		else if (RefSamples.Num() > 0)
		{
			Params.ref_audio_24k = RefSamples.GetData();
			Params.ref_n_samples = RefSamples.Num();
			if (!RefTranscriptUtf8.empty())
			{
				Params.ref_text = RefTranscriptUtf8.c_str();
			}
		}

		// Speed control// Speed control: OmniVoice doesn't have a direct speed param,
		// but we can use T_override with duration estimation
		float VoiceSpeed = 1.0f;
		if (Operation.Input.Speed > 0.0f)
			VoiceSpeed = Operation.Input.Speed;
		else
			VoiceSpeed = GetProcessorFloat("VoiceSpeed");
		VoiceSpeed = FMath::Clamp(VoiceSpeed, 0.01f, 10.0f);

		// Cancel callback: OmniVoice polls this between chunks
		Params.cancel = [](void* user_data) -> bool {
			UHMITextToSpeech_OmniVoice* Self = static_cast<UHMITextToSpeech_OmniVoice*>(user_data);
			return Self->CancelFlag || Self->OvCancelFlag;
		};
		Params.cancel_user_data = this;

		// Synthesize with optional streaming
		struct FChunkCtx { TArray<float>* Acc; UHMITextToSpeech_OmniVoice* Self; const FHMITextToSpeechInput* Input; };
		TArray<float> StreamingAccum;
		FChunkCtx ChunkCtx;
		if (bStreaming)
		{
			ChunkCtx.Acc = &StreamingAccum;
			ChunkCtx.Self = this;
			ChunkCtx.Input = &Operation.Input;

			Params.on_chunk = [](const float* samples, int n_samples, void* user_data) -> bool {
				auto* C = (FChunkCtx*)user_data;
				C->Acc->Append(samples, n_samples);
				TArray<float> ChunkSamples(samples, n_samples);
				FHMIWaveHandle ChunkWave(FHMIWaveFormat::Make_PCM_F32(C->Self->ModelSampleRate, 1), ChunkSamples);
				C->Self->OnOmniVoiceTTSChunk.Broadcast(*C->Input, ChunkWave);
				return true;
			};
			Params.on_chunk_user_data = &ChunkCtx;
			UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Streaming mode enabled"));
		}

		struct ov_audio Audio = { 0 };
		enum ov_status Status = OV_CALL(ov_synthesize)(OvContext, &Params, &Audio);

		HMI_BREAK_ON_CANCEL();

		if (Status != OV_STATUS_OK)
		{
			const char* ErrMsg = OV_CALL(ov_last_error)();
			ErrorText = FString::Printf(TEXT("ov_synthesize failed: %s"), ANSI_TO_TCHAR(ErrMsg ? ErrMsg : "unknown"));
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

		OV_CALL(ov_audio_free)(&Audio);
		Success = true;
	}
	while (false);

	if (!CancelFlag)
	{
		HandleOperationComplete(Success, MoveTemp(ErrorText), MoveTemp(Operation.Input), MoveTemp(OutputWave));
	}

	return Success;
}

void UHMITextToSpeech_OmniVoice::Proc_PostWorkStats()
{
	HMI_PROC_POST_WORK_STATS(TTS_OmniVoice)
}

void UHMITextToSpeech_OmniVoice::HandleOperationComplete(bool Success, FString&& Error, FHMITextToSpeechInput&& Input, FHMIWaveHandle&& Wave)
{
	if (Success)
	{
		UE_LOG(LogHMI, Log, TEXT(LOGPREFIX ">>> Duration=%.4f"), Wave.GetDuration());
	}

	Wave.SetTag(Input.UserTag);

	FHMITextToSpeechResult Result(GetTimestamp(), Success, MoveTemp(Error), MoveTemp(Wave));

	BroadcastResult(MoveTemp(Input), MoveTemp(Result), OnTextToSpeechComplete);
}

#endif // HMI_WITH_OMNIVOICE
