#include "GGML/HMISpeechToText_Whisper.h"
#include "HMIProcessorImpl.h"
#include "HMISubsystemStatics.h"

#if HMI_WITH_WHISPER
	#include <string>

	#include "HMIThirdPartyBegin.h"
	#include "whisper.h"
	#include "HMIThirdPartyEnd.h"

	#include "Audio/HMIResampler.h"
#endif

HMI_PROC_DECLARE_STATS(STT_Whisper)

#define LOGPREFIX "[STT_Whisper] "

const FName UHMISpeechToText_Whisper::ImplName = TEXT("STT_Whisper");

UHMISpeechToText* UHMISpeechToText_Whisper::GetOrCreateSTT_Whisper(UObject* WorldContextObject, 
	FName Name,
	FString ModelName,
	bool Accelerate,
	int NumThreads,
	FString DefaultLanguage,
	bool IsLanguageDetector
)
{
	UHMISpeechToText* Proc = UHMISubsystemStatics::GetOrCreateSTT(WorldContextObject, ImplName, Name);

	Proc->SetProcessorParam("ModelName", ModelName);
	Proc->SetProcessorParam("Accelerate", Accelerate);
	Proc->SetProcessorParam("NumThreads", NumThreads);

	Proc->SetProcessorParam("DefaultLanguage", DefaultLanguage);
	Proc->SetProcessorParam("IsLanguageDetector", IsLanguageDetector);

	return Proc;
}

UHMISpeechToText_Whisper::UHMISpeechToText_Whisper(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ProviderName = ImplName;
	ProcessorName = ImplName.ToString();

	SetProcessorParam("ModelName", TEXT("ggml-small-q8_0.bin"));
	SetProcessorParam("Accelerate", false);
	SetProcessorParam("NumThreads", 2); // 0 = default, -1 = max

	SetProcessorParam("DefaultLanguage", TEXT("")); // en
	SetProcessorParam("IsLanguageDetector", false);
}

UHMISpeechToText_Whisper::UHMISpeechToText_Whisper(FVTableHelper& Helper) : Super(Helper)
{
}

UHMISpeechToText_Whisper::~UHMISpeechToText_Whisper()
{
}

#if HMI_WITH_WHISPER

DEFINE_LOG_CATEGORY_STATIC(LogWhisper, Warning, All);

static void WhisperLogCallback(enum ggml_log_level level, const char* text, void* user_data)
{
	#define WHISPER_LOG(Verbosity) UE_LOG(LogWhisper, Verbosity, TEXT(LOGPREFIX "%s"), UTF8_TO_TCHAR(text))
	switch (level)
	{
		case GGML_LOG_LEVEL_DEBUG: WHISPER_LOG(VeryVerbose); break;
		case GGML_LOG_LEVEL_INFO: WHISPER_LOG(Verbose); break;
		case GGML_LOG_LEVEL_WARN: WHISPER_LOG(Warning); break;
		case GGML_LOG_LEVEL_ERROR: WHISPER_LOG(Error); break;
	}
	#undef WHISPER_LOG
}

static bool WhisperAbortCallback(void* data)
{
	auto* ctx = (UHMISpeechToText_Whisper*)data;
	return ctx->GetCancelFlag();
}

bool UHMISpeechToText_Whisper::Proc_Init()
{
	if (!Super::Proc_Init())
		return false;

	ModelName = GetProcessorString("ModelName");
	HMI_GUARD_PARAM_NOT_EMPTY(ModelName);

	FString ModelPath = UHMIStatics::LocateDataFile(ModelName);
	if (!FPaths::FileExists(ModelPath))
	{
		ProcError(FString::Printf(TEXT("Model not found: %s"), *ModelPath));
		return false;
	}

	NumProcessors = 1; // threads give better result
	NumThreads = GetNumBackendThreads();
	const bool Accelerate = GetProcessorBool("Accelerate");

	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "ModelPath: %s"), *ModelPath);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "NumThreads: %d"), NumThreads);
	UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Accelerate: %d"), Accelerate ? 1 : 0);

	// Pre-load whisper.dll with full path so the delay-load helper finds it.
	// The MSVC delay-load helper may not search AddDllDirectory paths,
	// so the PushDllDirectory call in module startup doesn't help.
	{
		const FString WhisperDllPath = UHMIStatics::GetPluginDllFilepath(TEXT("whisper"));
		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Pre-loading whisper DLL: %s"), *WhisperDllPath);
		void* PreloadHandle = FPlatformProcess::GetDllHandle(*WhisperDllPath);
		if (!PreloadHandle)
		{
			UE_LOG(LogHMI, Error, TEXT(LOGPREFIX "Failed to load whisper DLL. Missing GPU runtime (CUDA/Vulkan) or CPU-only build required."));
			return false;
		}
	}

	whisper_log_set(&WhisperLogCallback, nullptr);

	whisper_context_params ContextParams = whisper_context_default_params();
	ContextParams.use_gpu = Accelerate;

	FullParams = new whisper_full_params();
	*FullParams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
	if (NumThreads > 0)
		FullParams->n_threads = NumThreads;

	FullParams->abort_callback = &WhisperAbortCallback;
	FullParams->abort_callback_user_data = this;

	UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "whisper_init"));

	Context = whisper_init_from_file_with_params(TCHAR_TO_UTF8(*ModelPath), ContextParams);
	if (!Context)
	{
		ProcError(TEXT("whisper_init"));
		return false;
	}

	Resampler = MakeUnique<FHMIResampler>();
	Resampler->InitDefaultVoice();

	return true;
}

void UHMISpeechToText_Whisper::Proc_Release()
{
	if (Context)
	{
		UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "whisper_free"));
		whisper_free(Context);
		Context = nullptr;
	}

	if (FullParams)
	{
		delete FullParams;
		FullParams = nullptr;
	}

	whisper_log_set(nullptr, nullptr);

	Super::Proc_Release();
}

int64 UHMISpeechToText_Whisper::ProcessInput(FHMISpeechToTextInput&& Input)
{
	const int64 OperationId = EnqueOperation(InputQueue, MoveTemp(Input), GetWorldContext());
	StartOrWakeProcessor();
	return OperationId;
}

void UHMISpeechToText_Whisper::CancelOperation(bool PurgeQueue)
{
	Super::CancelOperation(PurgeQueue);

	if (PurgeQueue)
		PurgeInputQueue(InputQueue);
}

bool UHMISpeechToText_Whisper::Proc_DoWork(int& QueueLength)
{
	FHMISpeechToText_Whisper_Operation Operation;

	if (!DequeOperation(InputQueue, Operation, QueueLength))
		return false;

	bool Success = false;
	FString ErrorText;
	FString OutputText;
	FName OutputLanguage;

	do
	{
		auto& Wave = Operation.Input.Wave;

		if (Wave.IsEmpty())
		{
			ErrorText = TEXT("Empty input");
			break;
		}

		if (!UHMIBufferStatics::IsValidVoice(Wave))
		{
			ErrorText = TEXT("Unsupported wave format");
			break;
		}

		HMI_PROC_PRE_WORK_STATS(STT_Whisper)

		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "<<< Duration=%.4f"), Operation.Input.Wave.GetDuration());

		const int SampleRate = Wave.GetSampleRate();
		const int NumInputSamples = Wave.GetNumSamples();
		const float Duration = Wave.GetDuration();
		const int EstimatedLetterCount = (int)(Duration * 20); // average lecturer speech rate is about 20 letters/sec (4 words/sec)

		// PAD to minimal buffer duration (1.0s) -> https://github.com/ggerganov/whisper.cpp/issues/39
		const int NumPaddedSamples = FMath::Max<int>(NumInputSamples, FMath::CeilToInt(SampleRate * 1.1f));
		FPcm::S16_F32(Wave, PaddedPcmF32, NumPaddedSamples);

		if (SampleRate != WHISPER_SAMPLE_RATE)
		{
			Resampler->Convert(PaddedPcmF32, SampleRate, ResampledPcmF32, WHISPER_SAMPLE_RATE);
		}
		HMI_BREAK_ON_CANCEL();

		const auto& InputPcmF32 = (SampleRate == WHISPER_SAMPLE_RATE) ? PaddedPcmF32 : ResampledPcmF32;

		const bool IsLanguageDetector = GetProcessorBool("IsLanguageDetector");
		std::string TmpLanguage;

		if (IsLanguageDetector) // detect only
		{
			FullParams->language = nullptr;
			FullParams->detect_language = true;
		}
		else // detect + transcribe
		{
			if (Operation.Input.Language != NAME_None)
				TmpLanguage.assign(TCHAR_TO_ANSI(*Operation.Input.Language.ToString()));
			else
				TmpLanguage.assign(TCHAR_TO_ANSI(*GetProcessorString("DefaultLanguage")));

			FullParams->language = TmpLanguage.c_str();
			FullParams->detect_language = false;
		}

		const int ErrorCode = whisper_full_parallel(Context, *FullParams, InputPcmF32.GetData(), InputPcmF32.Num(), NumProcessors);
		HMI_BREAK_ON_CANCEL();

		if (ErrorCode != 0)
		{
			ErrorText = FString::Printf(TEXT("whisper_full_parallel: %d"), ErrorCode);
			break;
		}

		const char* Language = whisper_lang_str(whisper_full_lang_id(Context));
		if (Language)
		{
			OutputLanguage = Language;
		}

		if (!IsLanguageDetector)
		{
			OutputText.Reserve(FMath::RoundUpToPowerOfTwo(EstimatedLetterCount));

			const int n_segments = whisper_full_n_segments(Context);
			for (int i = 0; i < n_segments; ++i)
			{
				const char* text = whisper_full_get_segment_text(Context, i);
				if (text)
				{
					OutputText.Append(UTF8_TO_TCHAR(text));
				}
			}

			if (OutputText.Contains(TEXT("BLANK_AUDIO"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogHMI, Warning, TEXT(LOGPREFIX "BLANK_AUDIO"));
				OutputText.Empty();
			}
		}
		
		Success = true;
	}
	while (false);

	if (!CancelFlag)
	{
		HandleOperationComplete(Success, MoveTemp(ErrorText), MoveTemp(Operation.Input), MoveTemp(OutputText), MoveTemp(OutputLanguage));
	}

	return Success;
}

void UHMISpeechToText_Whisper::Proc_PostWorkStats()
{
	HMI_PROC_POST_WORK_STATS(STT_Whisper)
}

void UHMISpeechToText_Whisper::HandleOperationComplete(bool Success, FString&& Error, FHMISpeechToTextInput&& Input, FString&& OutputText, FName&& OutputLanguage)
{
	if (Success)
	{
		UE_LOG(LogHMI, Log, TEXT(LOGPREFIX ">>> \"%s\""), *OutputText);
	}

	FHMISpeechToTextResult Result(GetTimestamp(), Success, MoveTemp(Error), MoveTemp(OutputText), MoveTemp(OutputLanguage));

	BroadcastResult(MoveTemp(Input), MoveTemp(Result), OnSpeechToTextComplete);
}

#endif // HMI_WITH_WHISPER

#undef LOGPREFIX
