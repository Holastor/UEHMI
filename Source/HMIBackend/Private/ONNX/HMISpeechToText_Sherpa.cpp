#include "ONNX/HMISpeechToText_Sherpa.h"
#include "HMIProcessorImpl.h"
#include "HMISubsystemStatics.h"

#if HMI_WITH_SHERPA
	#include "SherpaHelper.h"
	#include "Audio/HMIResampler.h"
#endif

HMI_PROC_DECLARE_STATS(STT_Sherpa)

#define LOGPREFIX "[STT_Sherpa] "

const FName UHMISpeechToText_Sherpa::ImplName = TEXT("STT_Sherpa");

UHMISpeechToText* UHMISpeechToText_Sherpa::GetOrCreateSTT_Sherpa(UObject* WorldContextObject, 
	FName Name,
	FString ModelName,
	EOnnxProvider Provider,
	int NumThreads,
	FString DefaultLanguage,
	bool IsLanguageDetector
)
{
	UHMISpeechToText* Proc = UHMISubsystemStatics::GetOrCreateSTT(WorldContextObject, ImplName, Name);

	Proc->SetProcessorParam("ModelName", ModelName);
	Proc->SetProcessorParam("NumThreads", NumThreads);

	Proc->SetProcessorParam("DefaultLanguage", DefaultLanguage);
	Proc->SetProcessorParam("IsLanguageDetector", IsLanguageDetector);

	Proc->SetProcessorParam("provider", GetSherpaProviderString(Provider));

	return Proc;
}

UHMISpeechToText_Sherpa::UHMISpeechToText_Sherpa(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ProviderName = ImplName;
	ProcessorName = ImplName.ToString();

	SetProcessorParam("ModelName", TEXT("sherpa-onnx-whisper-small"));
	SetProcessorParam("NumThreads", 2); // 0 = default, -1 = max

	SetProcessorParam("DefaultLanguage", TEXT("")); // en
	SetProcessorParam("IsLanguageDetector", false);

	// sherpa specific

	SetProcessorParam("quant", TEXT("int8")); // "", "int8", "fp16"
	SetProcessorParam("provider", TEXT("cpu")); // def:"cpu", "cpu", "cuda", "coreml", "xnnpack", "nnapi", "trt", "directml" (see provider.cc)
	SetProcessorParam("model_type", TEXT("")); // def:""
	SetProcessorParam("modeling_unit", TEXT("cjkchar")); // def:"cjkchar", "bpe", "cjkchar+bpe"

	SetProcessorParam("model_sample_rate", 16000); // def:16000
	SetProcessorParam("feature_dim", 80); // def:80

	SetProcessorParam("decoding_method", TEXT("greedy_search")); // def:"greedy_search", "modified_beam_search"

	// works only with online/streaming mode
	// https://k2-fsa.github.io/sherpa/ncnn/endpoint.html
	SetProcessorParam("enable_endpoint", 1); // def:0
	SetProcessorParam("rule1_min_trailing_silence", 0.6f); // def:2.4
	SetProcessorParam("rule2_min_trailing_silence", 0.6f); // def:1.2
	SetProcessorParam("rule3_min_utterance_length", 6.0f); // def:20.0

	FString AnyModel(TEXT("*.onnx"));
	FString DefModel(TEXT("*model*.onnx"));
	FString DefTokens(TEXT("*token*.txt"));
	FString DefEncoder(TEXT("*encode*.onnx"));
	FString DefDecoder(TEXT("*decode*.onnx"));

	SetProcessorParam("transducer.tokens", DefTokens);
	SetProcessorParam("transducer.encoder", DefEncoder);
	SetProcessorParam("transducer.decoder", DefDecoder);
	SetProcessorParam("transducer.joiner", TEXT("*joiner*.onnx"));

	SetProcessorParam("paraformer.tokens", DefTokens);
	SetProcessorParam("paraformer.model", DefModel); // offline
	SetProcessorParam("paraformer.encoder", DefEncoder); // online
	SetProcessorParam("paraformer.decoder", DefDecoder); // online

	SetProcessorParam("nemo_ctc.tokens", DefTokens);
	SetProcessorParam("nemo_ctc.model", AnyModel);

	SetProcessorParam("whisper.tokens", DefTokens);
	SetProcessorParam("whisper.encoder", DefEncoder);
	SetProcessorParam("whisper.decoder", DefDecoder);
	SetProcessorParam("whisper.task", TEXT("transcribe")); // def:"transcribe"
	SetProcessorParam("whisper.tail_paddings", -1); // def:-1

	SetProcessorParam("sense_voice.tokens", DefTokens);
	SetProcessorParam("sense_voice.model", AnyModel);
	SetProcessorParam("sense_voice.use_itn", 0); // def:0 (inverse text normalization)

	SetProcessorParam("moonshine.tokens", DefTokens);
	SetProcessorParam("moonshine.preprocessor", TEXT("*preprocess*.onnx"));
	SetProcessorParam("moonshine.encoder", TEXT("*encode*.onnx"));
	SetProcessorParam("moonshine.uncached_decoder", TEXT("*uncached_decode*.onnx"));
	SetProcessorParam("moonshine.cached_decoder", TEXT("*cached_decode*.onnx"));

	SetProcessorParam("fire_red_asr.tokens", DefTokens);
	SetProcessorParam("fire_red_asr.encoder", DefEncoder);
	SetProcessorParam("fire_red_asr.decoder", DefDecoder);

	SetProcessorParam("dolphin.tokens", DefTokens);
	SetProcessorParam("dolphin.model", AnyModel);

	SetProcessorParam("zipformer_ctc.tokens", DefTokens);
	SetProcessorParam("zipformer_ctc.model", AnyModel);

	SetProcessorParam("canary.tokens", DefTokens);
	SetProcessorParam("canary.encoder", DefEncoder);
	SetProcessorParam("canary.decoder", DefDecoder);
	SetProcessorParam("canary.src_lang", TEXT("")); // def:"", "en"
	SetProcessorParam("canary.tgt_lang", TEXT("")); // def:"", "en"
	SetProcessorParam("canary.use_pnc", 1); // def:1 (output punctuations and cases)

	SetProcessorParam("wenet_ctc.tokens", DefTokens);
	SetProcessorParam("wenet_ctc.model", AnyModel);

		SetProcessorParam("qwen3_asr.conv_frontend", TEXT("*conv_frontend*.onnx"));
		SetProcessorParam("qwen3_asr.encoder", DefEncoder);
		SetProcessorParam("qwen3_asr.decoder", DefDecoder);
		SetProcessorParam("qwen3_asr.tokenizer", TEXT("tokenizer"));
		SetProcessorParam("qwen3_asr.max_total_len", 2048);
		SetProcessorParam("qwen3_asr.max_new_tokens", 512);
		SetProcessorParam("qwen3_asr.temperature", 0.0f);
		SetProcessorParam("qwen3_asr.top_p", 1.0f);
		SetProcessorParam("qwen3_asr.seed", -1);
		SetProcessorParam("qwen3_asr.hotwords", TEXT(""));

		SetProcessorParam("cohere_transcribe.encoder", DefEncoder);
		SetProcessorParam("cohere_transcribe.decoder", DefDecoder);
		SetProcessorParam("cohere_transcribe.language", TEXT(""));
		SetProcessorParam("cohere_transcribe.use_punct", 1);
		SetProcessorParam("cohere_transcribe.tokens", DefTokens);
		SetProcessorParam("cohere_transcribe.use_itn", 1);

		SetProcessorParam("funasr_nano.encoder_adaptor", TEXT("*encoder_adaptor*.onnx"));
		SetProcessorParam("funasr_nano.embedding", TEXT("*embedding*.onnx"));
		SetProcessorParam("funasr_nano.llm", TEXT("*llm*.onnx"));
		SetProcessorParam("funasr_nano.tokenizer", TEXT("Qwen3-0.6B"));
		SetProcessorParam("funasr_nano.itn", 1);

	SetProcessorParam("t_one_ctc.tokens", DefTokens); // online
	SetProcessorParam("t_one_ctc.model", AnyModel); // online
}

UHMISpeechToText_Sherpa::UHMISpeechToText_Sherpa(FVTableHelper& Helper) : Super(Helper)
{
}

UHMISpeechToText_Sherpa::~UHMISpeechToText_Sherpa()
{
}

#if HMI_WITH_SHERPA

bool UHMISpeechToText_Sherpa::Proc_Init()
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

	ModelSampleRate = GetProcessorInt("model_sample_rate");
	if (!UHMIBufferStatics::IsValidSampleRate(ModelSampleRate))
	{
		ProcError(FString::Printf(TEXT("Invalid ModelSampleRate: %d"), ModelSampleRate));
		return false;
	}

	const int NumThreads = GetNumBackendThreads();
	const bool IsLanguageDetector = GetProcessorBool("IsLanguageDetector");
	const int IsOnline = ModelName.Contains(TEXT("streaming-"));
	const bool IsTransducer = (ModelName.Contains(TEXT("zipformer")) && !ModelName.Contains(TEXT("zipformer-ctc")));

	SherpaStrings.Reset();
	FSherpaHelper Helper(this, ModelPath, SherpaStrings, GetProcessorString("quant"));

	if (IsLanguageDetector)
	{
		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Mode: Spoken Language Identification"));

		if (!ModelName.Contains(TEXT("whisper")))
		{
			ProcError(TEXT("Spoken Language Identification requires whisper model"));
			return false;
		}

		SherpaOnnxSpokenLanguageIdentificationConfig config;
		memset(&config, 0, sizeof(config));

		config.num_threads = NumThreads;
		config.provider = Helper.GetStr("provider");

		SHERPA_REQUIRED_PATH(config.whisper.encoder, "whisper.encoder", Node_File);
		SHERPA_REQUIRED_PATH(config.whisper.decoder, "whisper.decoder", Node_File);
		config.whisper.tail_paddings = Helper.GetInt("whisper.tail_paddings");

		UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "SherpaOnnxCreateSpokenLanguageIdentification"));

		LanguageDetector = SHERPA_CALL(SherpaOnnxCreateSpokenLanguageIdentification, (&config));
		if (!LanguageDetector)
		{
			ProcError(TEXT("SherpaOnnxCreateSpokenLanguageIdentification"));
			return false;
		}
	}
	else if (IsOnline)
	{
		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Mode: Online Recognizer"));

		SherpaOnnxOnlineRecognizerConfig recognizer_config;
		SherpaOnnxOnlineModelConfig& config = recognizer_config.model_config;
		memset(&recognizer_config, 0, sizeof(recognizer_config));

		recognizer_config.model_config.num_threads = NumThreads;
		recognizer_config.model_config.provider = Helper.GetStr("provider");
		recognizer_config.model_config.model_type = Helper.GetStr("model_type");
		recognizer_config.model_config.modeling_unit = Helper.GetStr("modeling_unit");

		recognizer_config.feat_config.sample_rate = Helper.GetInt("model_sample_rate");
		recognizer_config.feat_config.feature_dim = Helper.GetInt("feature_dim");

		recognizer_config.decoding_method = Helper.GetStr("decoding_method");

		recognizer_config.enable_endpoint = Helper.GetInt("enable_endpoint");
		recognizer_config.rule1_min_trailing_silence = Helper.GetFloat("rule1_min_trailing_silence");
		recognizer_config.rule2_min_trailing_silence = Helper.GetFloat("rule2_min_trailing_silence");
		recognizer_config.rule3_min_utterance_length = Helper.GetFloat("rule3_min_utterance_length");

		if (IsTransducer || (ModelName.Contains(TEXT("nemo")) && ModelName.Contains(TEXT("transducer"))))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "transducer.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.transducer.encoder, "transducer.encoder", Node_File);
			SHERPA_REQUIRED_PATH(config.transducer.decoder, "transducer.decoder", Node_File);
			SHERPA_REQUIRED_PATH(config.transducer.joiner, "transducer.joiner", Node_File);
		}
		else if (ModelName.Contains(TEXT("paraformer")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "paraformer.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.paraformer.encoder, "paraformer.encoder", Node_File);
			SHERPA_REQUIRED_PATH(config.paraformer.decoder, "paraformer.decoder", Node_File);
		}
		else if (ModelName.Contains(TEXT("zipformer-ctc")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "zipformer_ctc.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.zipformer2_ctc.model, "zipformer_ctc.model", Node_File);
		}
		else if (ModelName.Contains(TEXT("nemo-ctc")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "nemo_ctc.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.nemo_ctc.model, "nemo_ctc.model", Node_File);
		}
		else if (ModelName.Contains(TEXT("t-one")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "t_one_ctc.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.nemo_ctc.model, "t_one_ctc.model", Node_File);
		}
		else
		{
			ProcError(FString::Printf(TEXT("Unsupported model: %s"), *ModelName));
			return false;
		}

		UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "SherpaOnnxCreateOnlineRecognizer"));

		OnlineRecognizer = SHERPA_CALL(SherpaOnnxCreateOnlineRecognizer, (&recognizer_config));
		if (!OnlineRecognizer)
		{
			ProcError(TEXT("SherpaOnnxCreateOnlineRecognizer"));
			return false;
		}

		OnlineStream = SHERPA_CALL(SherpaOnnxCreateOnlineStream, (OnlineRecognizer));
		if (!OnlineStream)
		{
			ProcError(TEXT("SherpaOnnxCreateOnlineStream"));
			return false;
		}
	}
	else // Offline
	{
		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "Mode: Offline Recognizer"));

		SherpaOnnxOfflineRecognizerConfig recognizer_config;
		SherpaOnnxOfflineModelConfig& config = recognizer_config.model_config;
		memset(&recognizer_config, 0, sizeof(recognizer_config));

		recognizer_config.model_config.num_threads = NumThreads;
		recognizer_config.model_config.provider = Helper.GetStr("provider");
		recognizer_config.model_config.model_type = Helper.GetStr("model_type");
		recognizer_config.model_config.modeling_unit = Helper.GetStr("modeling_unit");

		recognizer_config.feat_config.sample_rate = Helper.GetInt("model_sample_rate");
		recognizer_config.feat_config.feature_dim = Helper.GetInt("feature_dim");

		recognizer_config.decoding_method = Helper.GetStr("decoding_method");

		if (IsTransducer || ModelName.Contains(TEXT("nemo-parakeet")) || (ModelName.Contains(TEXT("nemo")) && ModelName.Contains(TEXT("transducer"))))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "transducer.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.transducer.encoder, "transducer.encoder", Node_File);
			SHERPA_REQUIRED_PATH(config.transducer.decoder, "transducer.decoder", Node_File);
			SHERPA_REQUIRED_PATH(config.transducer.joiner, "transducer.joiner", Node_File);
		}
		else if (ModelName.Contains(TEXT("paraformer")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "paraformer.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.paraformer.model, "paraformer.model", Node_File);
		}
		else if (ModelName.Contains(TEXT("nemo-ctc")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "nemo_ctc.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.nemo_ctc.model, "nemo_ctc.model", Node_File);
		}
		else if (ModelName.Contains(TEXT("whisper")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "whisper.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.whisper.encoder, "whisper.encoder", Node_File);
			SHERPA_REQUIRED_PATH(config.whisper.decoder, "whisper.decoder", Node_File);

			config.whisper.language = Helper.GetStr("DefaultLanguage", NullIfEmpty);
			config.whisper.task = Helper.GetStr("whisper.task", NullIfEmpty);
			config.whisper.tail_paddings = Helper.GetInt("whisper.tail_paddings");
		}
		//tdnn
		else if (ModelName.Contains(TEXT("sense-voice")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "sense_voice.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.sense_voice.model, "sense_voice.model", Node_File);

			config.sense_voice.language = Helper.GetStr("DefaultLanguage", NullIfEmpty);
			config.sense_voice.use_itn = Helper.GetInt("sense_voice.use_itn");
		}
		else if (ModelName.Contains(TEXT("moonshine")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "moonshine.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.moonshine.preprocessor, "moonshine.preprocessor", Node_File);
			SHERPA_REQUIRED_PATH(config.moonshine.encoder, "moonshine.encoder", Node_File);
			SHERPA_REQUIRED_PATH(config.moonshine.uncached_decoder, "moonshine.uncached_decoder", Node_File);
			SHERPA_REQUIRED_PATH(config.moonshine.cached_decoder, "moonshine.cached_decoder", Node_File);
		}
		else if (ModelName.Contains(TEXT("fire-red-asr")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "fire_red_asr.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.fire_red_asr.encoder, "fire_red_asr.encoder", Node_File);
			SHERPA_REQUIRED_PATH(config.fire_red_asr.decoder, "fire_red_asr.decoder", Node_File);
		}
		else if (ModelName.Contains(TEXT("dolphin")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "dolphin.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.dolphin.model, "dolphin.model", Node_File);
		}
		else if (ModelName.Contains(TEXT("zipformer-ctc")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "zipformer_ctc.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.zipformer_ctc.model, "zipformer_ctc.model", Node_File);
		}
		else if (ModelName.Contains(TEXT("canary")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "canary.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.canary.encoder, "canary.encoder", Node_File);
			SHERPA_REQUIRED_PATH(config.canary.decoder, "canary.decoder", Node_File);

			config.canary.src_lang = Helper.GetStr("canary.src_lang", NullIfEmpty);
			config.canary.tgt_lang = Helper.GetStr("canary.tgt_lang", NullIfEmpty);
			config.canary.use_pnc = Helper.GetInt("canary.use_pnc");
		}
		else if (ModelName.Contains(TEXT("funasr-nano")))
			{
				recognizer_config.feat_config.sample_rate = 0;
				recognizer_config.feat_config.feature_dim = 0;
				SHERPA_REQUIRED_PATH(config.funasr_nano.encoder_adaptor, "funasr_nano.encoder_adaptor", Node_File);
				SHERPA_REQUIRED_PATH(config.funasr_nano.embedding, "funasr_nano.embedding", Node_File);
				SHERPA_REQUIRED_PATH(config.funasr_nano.llm, "funasr_nano.llm", Node_File);
				SHERPA_REQUIRED_PATH(config.funasr_nano.tokenizer, "funasr_nano.tokenizer", Node_Dir);
				config.funasr_nano.itn = Helper.GetInt("funasr_nano.itn");
			}
			else if (ModelName.Contains(TEXT("cohere-transcribe")))
			{
				recognizer_config.feat_config.sample_rate = 0;
				recognizer_config.feat_config.feature_dim = 0;
				SHERPA_REQUIRED_PATH(config.tokens, "cohere_transcribe.tokens", Node_File);
				SHERPA_REQUIRED_PATH(config.cohere_transcribe.encoder, "cohere_transcribe.encoder", Node_File);
				SHERPA_REQUIRED_PATH(config.cohere_transcribe.decoder, "cohere_transcribe.decoder", Node_File);
				config.cohere_transcribe.language = Helper.GetStr("cohere_transcribe.language", NullIfEmpty);
				config.cohere_transcribe.use_punct = Helper.GetInt("cohere_transcribe.use_punct");
				config.cohere_transcribe.use_itn = Helper.GetInt("cohere_transcribe.use_itn");
			}
			else if (ModelName.Contains(TEXT("qwen3-asr")))
			{
				recognizer_config.feat_config.sample_rate = 0;
				recognizer_config.feat_config.feature_dim = 0;
				SHERPA_REQUIRED_PATH(config.qwen3_asr.conv_frontend, "qwen3_asr.conv_frontend", Node_File);
				SHERPA_REQUIRED_PATH(config.qwen3_asr.encoder, "qwen3_asr.encoder", Node_File);
				SHERPA_REQUIRED_PATH(config.qwen3_asr.decoder, "qwen3_asr.decoder", Node_File);
				SHERPA_REQUIRED_PATH(config.qwen3_asr.tokenizer, "qwen3_asr.tokenizer", Node_Dir);
				config.qwen3_asr.max_total_len = Helper.GetInt("qwen3_asr.max_total_len");
				config.qwen3_asr.max_new_tokens = Helper.GetInt("qwen3_asr.max_new_tokens");
				config.qwen3_asr.temperature = Helper.GetFloat("qwen3_asr.temperature");
				config.qwen3_asr.top_p = Helper.GetFloat("qwen3_asr.top_p");
				config.qwen3_asr.seed = Helper.GetInt("qwen3_asr.seed");
				const char* Hotwords = Helper.GetStr("qwen3_asr.hotwords", NullIfEmpty);
				if (Hotwords) config.qwen3_asr.hotwords = Hotwords;
			}
			else if (ModelName.Contains(TEXT("wenet")))
		{
			SHERPA_REQUIRED_PATH(config.tokens, "wenet_ctc.tokens", Node_File);
			SHERPA_REQUIRED_PATH(config.wenet_ctc.model, "wenet_ctc.model", Node_File);
		}
		else
		{
			ProcError(FString::Printf(TEXT("Unsupported model: %s"), *ModelName));
			return false;
		}

		UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "SherpaOnnxCreateOfflineRecognizer"));

		OfflineRecognizer = SHERPA_CALL(SherpaOnnxCreateOfflineRecognizer, (&recognizer_config));
		if (!OfflineRecognizer)
		{
			ProcError(TEXT("SherpaOnnxCreateOfflineRecognizer"));
			return false;
		}
	}

	Resampler = MakeUnique<FHMIResampler>();
	Resampler->InitDefaultVoice();

	return true;
}

void UHMISpeechToText_Sherpa::Proc_Release()
{
	if (LanguageDetector)
	{
		UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "SherpaOnnxDestroySpokenLanguageIdentification"));
		SHERPA_CALL(SherpaOnnxDestroySpokenLanguageIdentification, (LanguageDetector));
		LanguageDetector = nullptr;
	}

	if (OfflineRecognizer)
	{
		UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "SherpaOnnxDestroyOfflineRecognizer"));
		SHERPA_CALL(SherpaOnnxDestroyOfflineRecognizer, (OfflineRecognizer));
		OfflineRecognizer = nullptr;
	}

	if (OnlineStream)
	{
		UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "SherpaOnnxDestroyOnlineStream"));
		SHERPA_CALL(SherpaOnnxDestroyOnlineStream, (OnlineStream));
		OnlineStream = nullptr;
	}

	if (OnlineRecognizer)
	{
		UE_LOG(LogHMI, VeryVerbose, TEXT(LOGPREFIX "SherpaOnnxDestroyOnlineRecognizer"));
		SHERPA_CALL(SherpaOnnxDestroyOnlineRecognizer, (OnlineRecognizer));
		OnlineRecognizer = nullptr;
	}

	Super::Proc_Release();
}

int64 UHMISpeechToText_Sherpa::ProcessInput(FHMISpeechToTextInput&& Input)
{
	const int64 OperationId = EnqueOperation(InputQueue, MoveTemp(Input), GetWorldContext());
	StartOrWakeProcessor();
	return OperationId;
}

void UHMISpeechToText_Sherpa::CancelOperation(bool PurgeQueue)
{
	Super::CancelOperation(PurgeQueue);

	if (PurgeQueue)
		PurgeInputQueue(InputQueue);
}

bool UHMISpeechToText_Sherpa::Proc_DoWork(int& QueueLength)
{
	FHMISpeechToText_Sherpa_Operation Operation;

	if (!DequeOperation(InputQueue, Operation, QueueLength))
		return false;

	bool Success = false;
	FString ErrorText;
	FString OutputText;
	FName OutputLanguage;

	const SherpaOnnxOfflineStream* OfflineStream = nullptr;
	const SherpaOnnxOfflineRecognizerResult* OfflineResult = nullptr;
	const SherpaOnnxOnlineRecognizerResult* OnlineResult = nullptr;
	const SherpaOnnxSpokenLanguageIdentificationResult* LanguageResult = nullptr;

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

		HMI_PROC_PRE_WORK_STATS(STT_Sherpa)

		UE_LOG(LogHMI, Verbose, TEXT(LOGPREFIX "<<< Duration=%.4f"), Wave.GetDuration());

		const int SampleRate = Wave.GetSampleRate();
		const int NumInputSamples = Wave.GetNumSamples();

		const int NumPaddedSamples = FMath::Max<int>(NumInputSamples, SampleRate + (int)(SampleRate * 0.1f)); // PAD to 1.0s
		FPcm::S16_F32(Wave, PaddedPcmF32, NumPaddedSamples);

		if (SampleRate != ModelSampleRate)
		{
			Resampler->Convert(PaddedPcmF32, SampleRate, ResampledPcmF32, ModelSampleRate);
		}
		HMI_BREAK_ON_CANCEL();

		const auto& InputPcmF32 = (SampleRate == ModelSampleRate) ? PaddedPcmF32 : ResampledPcmF32;

		if (LanguageDetector)
		{
			OfflineStream = SHERPA_CALL(SherpaOnnxSpokenLanguageIdentificationCreateOfflineStream, (LanguageDetector)); // NICE NAMING!
			if (!OfflineStream)
			{
				ErrorText = TEXT("CreateOfflineStream");
				break;
			}

			SHERPA_CALL(SherpaOnnxAcceptWaveformOffline, (OfflineStream, ModelSampleRate, InputPcmF32.GetData(), InputPcmF32.Num()));
			HMI_BREAK_ON_CANCEL();

			LanguageResult = SHERPA_CALL(SherpaOnnxSpokenLanguageIdentificationCompute, (LanguageDetector, OfflineStream));
			HMI_BREAK_ON_CANCEL();

			if (!LanguageResult)
			{
				ErrorText = TEXT("SpokenLanguageIdentificationCompute");
				break;
			}

			OutputLanguage = LanguageResult->lang;
			Success = true;
		}
		else if (OfflineRecognizer)
		{
			OfflineStream = SHERPA_CALL(SherpaOnnxCreateOfflineStream, (OfflineRecognizer));
			if (!OfflineStream)
			{
				ErrorText = TEXT("CreateOfflineStream");
				break;
			}

			SHERPA_CALL(SherpaOnnxAcceptWaveformOffline, (OfflineStream, ModelSampleRate, InputPcmF32.GetData(), InputPcmF32.Num()));
			HMI_BREAK_ON_CANCEL();

			SHERPA_CALL(SherpaOnnxDecodeOfflineStream, (OfflineRecognizer, OfflineStream));
			HMI_BREAK_ON_CANCEL();

			OfflineResult = SHERPA_CALL(SherpaOnnxGetOfflineStreamResult, (OfflineStream));
			HMI_BREAK_ON_CANCEL();

			if (!OfflineResult)
			{
				ErrorText = TEXT("GetOfflineStreamResult");
				break;
			}

			if (Operation.Input.Language != NAME_None)
				OutputLanguage = Operation.Input.Language;
			else
				OutputLanguage = FName(GetProcessorString("DefaultLanguage"));

			OutputText = FString(UTF8_TO_TCHAR(OfflineResult->text));
			Success = true;
		}
		else if (OnlineRecognizer)
		{
			SHERPA_CALL(SherpaOnnxOnlineStreamAcceptWaveform, (OnlineStream, ModelSampleRate, InputPcmF32.GetData(), InputPcmF32.Num()));
			HMI_BREAK_ON_CANCEL();

			while (!CancelFlag && SHERPA_CALL(SherpaOnnxIsOnlineStreamReady, (OnlineRecognizer, OnlineStream)))
			{
				SHERPA_CALL(SherpaOnnxDecodeOnlineStream, (OnlineRecognizer, OnlineStream));
			}
			HMI_BREAK_ON_CANCEL();

			if (SHERPA_CALL(SherpaOnnxOnlineStreamIsEndpoint, (OnlineRecognizer, OnlineStream)))
			{
				OnlineResult = SHERPA_CALL(SherpaOnnxGetOnlineStreamResult, (OnlineRecognizer, OnlineStream));
				HMI_BREAK_ON_CANCEL();

				if (!OnlineResult)
				{
					ErrorText = TEXT("GetOnlineStreamResult");
					break;
				}

				if (Operation.Input.Language != NAME_None)
					OutputLanguage = Operation.Input.Language;
				else
					OutputLanguage = FName(GetProcessorString("DefaultLanguage"));

				OutputText = FString(UTF8_TO_TCHAR(OnlineResult->text));
				Success = true;

				SHERPA_CALL(SherpaOnnxOnlineStreamReset, (OnlineRecognizer, OnlineStream));
			}
		}
	}
	while (false);

	if (LanguageResult)
	{
		SHERPA_CALL(SherpaOnnxDestroySpokenLanguageIdentificationResult, (LanguageResult));
	}

	if (OnlineResult)
	{
		SHERPA_CALL(SherpaOnnxDestroyOnlineRecognizerResult, (OnlineResult));
	}

	if (OfflineResult)
	{
		SHERPA_CALL(SherpaOnnxDestroyOfflineRecognizerResult, (OfflineResult));
	}

	if (OfflineStream)
	{
		SHERPA_CALL(SherpaOnnxDestroyOfflineStream, (OfflineStream));
	}

	if (!CancelFlag)
	{
		if (OnlineRecognizer) // online mode accumulates data and fires callback only when endpoint has been detected
		{
			if (Success)
				HandleOperationComplete(Success, MoveTemp(ErrorText), {}, MoveTemp(OutputText), MoveTemp(OutputLanguage));
		}
		else // offline mode decodes operation input wave and reports success/failure
		{
			HandleOperationComplete(Success, MoveTemp(ErrorText), MoveTemp(Operation.Input), MoveTemp(OutputText), MoveTemp(OutputLanguage));
		}
	}

	return Success;
}

void UHMISpeechToText_Sherpa::Proc_PostWorkStats()
{
	HMI_PROC_POST_WORK_STATS(STT_Sherpa)
}

void UHMISpeechToText_Sherpa::HandleOperationComplete(bool Success, FString&&Error, FHMISpeechToTextInput&& Input, FString&& OutputText, FName&& OutputLanguage)
{
	if (Success)
	{
		UE_LOG(LogHMI, Log, TEXT(LOGPREFIX ">>> \"%s\""), *OutputText);
	}

	FHMISpeechToTextResult Result(GetTimestamp(), Success, MoveTemp(Error), MoveTemp(OutputText), MoveTemp(OutputLanguage));

	BroadcastResult(MoveTemp(Input), MoveTemp(Result), OnSpeechToTextComplete);
}

#endif // HMI_WITH_SHERPA

#undef LOGPREFIX
