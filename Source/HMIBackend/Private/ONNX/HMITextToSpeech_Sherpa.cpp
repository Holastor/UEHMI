#include "ONNX/HMITextToSpeech_Sherpa.h"
#include "HMIProcessorImpl.h"
#include "HMISubsystemStatics.h"

#if HMI_WITH_SHERPA
	#include "SherpaHelper.h"
#endif

HMI_PROC_DECLARE_STATS(TTS_Sherpa)

#define LOGPREFIX "[TTS_Sherpa] "

const FName UHMITextToSpeech_Sherpa::ImplName = TEXT("TTS_Sherpa");

class UHMITextToSpeech* UHMITextToSpeech_Sherpa::GetOrCreateTTS_Sherpa(UObject* WorldContextObject, 
	FName Name,
	FString ModelName,
	EOnnxProvider Provider,
	int NumThreads,
	FString VoiceId,
	float VoiceSpeed
)
{
	UHMITextToSpeech* Proc = UHMISubsystemStatics::GetOrCreateTTS(WorldContextObject, ImplName, Name);

	Proc->SetProcessorParam("ModelName", ModelName);
	Proc->SetProcessorParam("NumThreads", NumThreads);

	Proc->SetProcessorParam("VoiceId", VoiceId);
	Proc->SetProcessorParam("VoiceSpeed", VoiceSpeed);

	Proc->SetProcessorParam("provider", GetSherpaProviderString(Provider));

	return Proc;
}

UHMITextToSpeech_Sherpa::UHMITextToSpeech_Sherpa(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ProviderName = ImplName;
	ProcessorName = ImplName.ToString();

	SetProcessorParam("ModelName", TEXT("vits-piper-en_US-libritts_r-medium-int8"));
	SetProcessorParam("NumThreads", 1);

	SetProcessorParam("VoiceId", TEXT("0")); // string
	SetProcessorParam("VoiceSpeed", 1.0f); // larger -> faster

	// sherpa specific

	SetProcessorParam("quant", TEXT("int8")); // "", "int8", "fp16"
	SetProcessorParam("provider", TEXT("cpu")); // def:"cpu", "cpu", "cuda", "coreml", "xnnpack", "nnapi", "trt", "directml" (see provider.cc)

	SetProcessorParam("max_num_sentences", 1);
	SetProcessorParam("silence_scale", 0.2f);

	FString DefModel(TEXT("*.onnx"));
	FString DefVoices(TEXT("*voice*.bin"));
	FString DefTokens(TEXT("*token*.txt"));

	SetProcessorParam("vits.model", DefModel);
	SetProcessorParam("vits.tokens", DefTokens);
	SetProcessorParam("vits.lexicon", TEXT("us-en")); // "us-en,zh"
	SetProcessorParam("vits.noise_scale", 0.667f);
	SetProcessorParam("vits.noise_scale_w", 0.8f);
	SetProcessorParam("vits.length_scale", 1.0f);

	SetProcessorParam("matcha.acoustic_model", DefModel);
	SetProcessorParam("matcha.vocoder", TEXT("*voco*.onnx")); // vocos-22khz-univ.onnx
	SetProcessorParam("matcha.tokens", DefTokens);
	SetProcessorParam("matcha.lexicon", TEXT("us-en")); // "us-en,zh"
	SetProcessorParam("matcha.dict_dir", TEXT("dict"));
	SetProcessorParam("matcha.noise_scale", 0.667f);
	SetProcessorParam("matcha.length_scale", 1.0f);

	SetProcessorParam("kokoro.model", DefModel);
	SetProcessorParam("kokoro.voices", DefVoices);
	SetProcessorParam("kokoro.tokens", DefTokens);
	SetProcessorParam("kokoro.lexicon", TEXT("us-en")); // "us-en,zh"
	SetProcessorParam("kokoro.dict_dir", TEXT("dict"));
	SetProcessorParam("kokoro.lang", TEXT(""));
	SetProcessorParam("kokoro.length_scale", 1.0f);

	SetProcessorParam("kitten.model", DefModel);
	SetProcessorParam("kitten.voices", DefVoices);
	SetProcessorParam("kitten.tokens", DefTokens);
	SetProcessorParam("kitten.length_scale", 1.0f);

	SetProcessorParam("supertonic.duration_predictor", TEXT("duration_predictor.int8.onnx"));
	SetProcessorParam("supertonic.text_encoder", TEXT("text_encoder.int8.onnx"));
	SetProcessorParam("supertonic.vector_estimator", TEXT("vector_estimator.int8.onnx"));
	SetProcessorParam("supertonic.vocoder", TEXT("vocoder.int8.onnx"));
	SetProcessorParam("supertonic.tts_json", TEXT("tts.json"));
	SetProcessorParam("supertonic.unicode_indexer", TEXT("unicode_indexer.bin"));
	SetProcessorParam("supertonic.voice_style", TEXT("voice.bin"));
}

UHMITextToSpeech_Sherpa::~UHMITextToSpeech_Sherpa()
{
}

#if HMI_WITH_SHERPA

bool UHMITextToSpeech_Sherpa::Proc_Init()
{
	if (!Super::Proc_Init())
		return false;

	ModelName = GetProcessorString("ModelName");
	HMI_GUARD_PARAM_NOT_EMPTY(ModelName);

	FString ModelPath = UHMIStatics::LocateDataSubdir(ModelName);
	if (!FPaths::DirectoryExists(ModelPath))
	{
		ProcError(FString::Printf(TEXT("Model not found: %s"), *ModelPath));
		return false;
	}

	FString SpeakDataPath = UHMIStatics::LocateDataSubdir(TEXT("espeak-ng-data"));
	if (!FPaths::DirectoryExists(SpeakDataPath))
	{
		SpeakDataPath = FPaths::Combine(FPaths::GetPath(ModelPath), TEXT("espeak-ng-data"));
		if (!FPaths::DirectoryExists(SpeakDataPath))
		{
			ProcError(FString::Printf(TEXT("Directory not found: %s"), *SpeakDataPath));
			return false;
		}
	}

	const int NumThreads = GetNumBackendThreads();

	SherpaStrings.Reset();
	FSherpaHelper Helper(this, ModelPath, SherpaStrings,  GetProcessorString("quant"));

	SherpaOnnxOfflineTtsConfig config;
	memset(&config, 0, sizeof(config));

	config.model.num_threads = NumThreads;
	config.model.provider = Helper.GetStr("provider");
	config.max_num_sentences = Helper.GetInt("max_num_sentences");
	config.silence_scale = Helper.GetFloat("silence_scale");

	if (ModelName.Contains(TEXT("vits")))
	{
		SHERPA_REQUIRED_PATH(config.model.vits.model, "vits.model", Node_File);
		SHERPA_REQUIRED_PATH(config.model.vits.tokens, "vits.tokens", Node_File);

		config.model.vits.data_dir = Helper.GetStr("vits.data_dir", SpeakDataPath);
		config.model.vits.lexicon = Helper.GetLexicon("vits.lexicon");

		config.model.vits.noise_scale = Helper.GetFloat("vits.noise_scale");
		config.model.vits.noise_scale_w = Helper.GetFloat("vits.noise_scale_w");
		config.model.vits.length_scale = Helper.GetFloat("vits.length_scale");
	}
	else if (ModelName.Contains(TEXT("matcha")))
	{
		SHERPA_REQUIRED_PATH(config.model.matcha.acoustic_model, "matcha.acoustic_model", Node_File);
		SHERPA_REQUIRED_PATH(config.model.matcha.vocoder, "matcha.vocoder", Node_File);
		SHERPA_REQUIRED_PATH(config.model.matcha.tokens, "matcha.tokens", Node_File);

		config.model.matcha.data_dir = Helper.GetStr("matcha.data_dir", SpeakDataPath);
		config.model.matcha.lexicon = Helper.GetLexicon("matcha.lexicon");
		config.model.matcha.dict_dir = Helper.GetPath("matcha.dict_dir", Node_Dir);

		config.model.matcha.noise_scale = Helper.GetFloat("matcha.noise_scale");
		config.model.matcha.length_scale = Helper.GetFloat("matcha.length_scale");
	}
	else if (ModelName.Contains(TEXT("kokoro")))
	{
		SHERPA_REQUIRED_PATH(config.model.kokoro.model, "kokoro.model", Node_File);
		SHERPA_REQUIRED_PATH(config.model.kokoro.voices, "kokoro.voices", Node_File);
		SHERPA_REQUIRED_PATH(config.model.kokoro.tokens, "kokoro.tokens", Node_File);

		config.model.kokoro.data_dir = Helper.GetStr("kokoro.data_dir", SpeakDataPath);
		config.model.kokoro.lexicon = Helper.GetLexicon("kokoro.lexicon");
		config.model.kokoro.dict_dir = Helper.GetPath("kokoro.dict_dir", Node_Dir);
		config.model.kokoro.lang = Helper.GetStr("kokoro.lang", NullIfEmpty);

		config.model.kokoro.length_scale = Helper.GetFloat("kokoro.length_scale");
	}
	else if (ModelName.Contains(TEXT("kitten")))
	{
		SHERPA_REQUIRED_PATH(config.model.kitten.model, "kitten.model", Node_File);
		SHERPA_REQUIRED_PATH(config.model.kitten.voices, "kitten.voices", Node_File);
		SHERPA_REQUIRED_PATH(config.model.kitten.tokens, "kitten.tokens", Node_File);

		config.model.kitten.data_dir = Helper.GetStr("kitten.data_dir", SpeakDataPath);

		config.model.kitten.length_scale = Helper.GetFloat("kitten.length_scale");
	}
	else if (ModelName.Contains(TEXT("supertonic")))
	{
		SHERPA_REQUIRED_PATH(config.model.supertonic.duration_predictor, "supertonic.duration_predictor", Node_File);
		SHERPA_REQUIRED_PATH(config.model.supertonic.text_encoder, "supertonic.text_encoder", Node_File);
		SHERPA_REQUIRED_PATH(config.model.supertonic.vector_estimator, "supertonic.vector_estimator", Node_File);
		SHERPA_REQUIRED_PATH(config.model.supertonic.vocoder, "supertonic.vocoder", Node_File);
		SHERPA_REQUIRED_PATH(config.model.supertonic.tts_json, "supertonic.tts_json", Node_File);
		SHERPA_REQUIRED_PATH(config.model.supertonic.unicode_indexer, "supertonic.unicode_indexer", Node_File);

		config.model.supertonic.voice_style = Helper.GetPath("supertonic.voice_style", Node_File);
	}
	else
	{
		ProcError(FString::Printf(TEXT("Unsupported model: %s"), *ModelName));
		return false;
	}

	UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "SherpaOnnxCreateOfflineTts"));

	tts = SHERPA_CALL(SherpaOnnxCreateOfflineTts, (&config));
	if (!tts)
	{
		ProcError(TEXT("SherpaOnnxCreateOfflineTts"));
		return false;
	}

	return true;
}

void UHMITextToSpeech_Sherpa::Proc_Release()
{
	if (tts)
	{
		UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "SherpaOnnxDestroyOfflineTts"));
		SHERPA_CALL(SherpaOnnxDestroyOfflineTts, (tts));
		tts = nullptr;
	}

	Super::Proc_Release();
}

int64 UHMITextToSpeech_Sherpa::ProcessInput(FHMITextToSpeechInput&& Input)
{
	const int64 OperationId = EnqueOperation(InputQueue, MoveTemp(Input), GetWorldContext());
	StartOrWakeProcessor();
	return OperationId;
}

void UHMITextToSpeech_Sherpa::CancelOperation(bool PurgeQueue)
{
	Super::CancelOperation(PurgeQueue);

	if (PurgeQueue)
		PurgeInputQueue(InputQueue);
}

static int32_t SherpaProgressCallback(const float *samples, int32_t num_samples, float progress)
{
	return 1; // 1 to continue, 0 to stop
}

bool UHMITextToSpeech_Sherpa::Proc_DoWork(int& QueueLength)
{
	FHMITextToSpeech_Sherpa_Operation Operation;

	if (!DequeOperation(InputQueue, Operation, QueueLength))
		return false;

	bool Success = false;
	FString ErrorText;
	FHMIWaveHandle OutputWave;
	const SherpaOnnxGeneratedAudio* audio = nullptr;

	do
	{
		std::string InputTextUtf8(TCHAR_TO_UTF8(*Operation.Input.Text));
		if (InputTextUtf8.empty())
		{
			ErrorText = TEXT("Empty input");
			break;
		}

		HMI_PROC_PRE_WORK_STATS(TTS_Sherpa)

		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "<<< \"%s\""), *Operation.Input.Text);

		int VoiceId = 0;
		float VoiceSpeed = 1.0f;

		if (!Operation.Input.VoiceId.IsEmpty())
			VoiceId = FCString::Atoi(*Operation.Input.VoiceId);
		else
			VoiceId = GetProcessorInt("VoiceId");

		if (Operation.Input.Speed > 0.0f)
			VoiceSpeed = Operation.Input.Speed;
		else
			VoiceSpeed = GetProcessorFloat("VoiceSpeed");

		VoiceId = FMath::Max(0, VoiceId); // TODO: limit to model voice count
		VoiceSpeed = FMath::Clamp(VoiceSpeed, 0.01f, 10.0f);

		audio = SHERPA_CALL(SherpaOnnxOfflineTtsGenerate, (tts, InputTextUtf8.c_str(), VoiceId, VoiceSpeed));
		//audio = SHERPA_CALL(SherpaOnnxOfflineTtsGenerateWithProgressCallback, (tts, InputTextUtf8.c_str(), VoiceId, VoiceSpeed, SherpaProgressCallback));
		HMI_BREAK_ON_CANCEL();

		if (!audio)
		{
			ErrorText = TEXT("TtsGenerate");
			break;
		}
		if (audio->n <= 0 || audio->sample_rate <= 0 || !audio->samples)
		{
			ErrorText = TEXT("TtsGenerate");
			break;
		}

		OutputWave = GenerateOutputWave({audio->samples, audio->n}, audio->sample_rate, HMI_VOICE_CHANNELS);
		Success = true;
	}
	while (false);

	if (audio)
	{
		SHERPA_CALL(SherpaOnnxDestroyOfflineTtsGeneratedAudio, (audio));
	}

	if (!CancelFlag)
	{
		HandleOperationComplete(Success, MoveTemp(ErrorText), MoveTemp(Operation.Input), MoveTemp(OutputWave));
	}

	return Success;
}

void UHMITextToSpeech_Sherpa::Proc_PostWorkStats()
{
	HMI_PROC_POST_WORK_STATS(TTS_Sherpa)
}

void UHMITextToSpeech_Sherpa::HandleOperationComplete(bool Success, FString&& Error, FHMITextToSpeechInput&& Input, FHMIWaveHandle&& Wave)
{
	if (Success)
	{
		UE_LOG(LogHMI, Log, TEXT(LOGPREFIX ">>> Duration=%.4f"), Wave.GetDuration());
	}

	Wave.SetTag(Input.UserTag);

	FHMITextToSpeechResult Result(GetTimestamp(), Success, MoveTemp(Error), MoveTemp(Wave));

	BroadcastResult(MoveTemp(Input), MoveTemp(Result), OnTextToSpeechComplete);
}

#endif // HMI_WITH_SHERPA

#undef LOGPREFIX
