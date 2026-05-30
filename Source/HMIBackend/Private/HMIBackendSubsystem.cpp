#include "HMIBackendSubsystem.h"
#include "HMIBackendModule.h"
#include "HMISubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

#include "Cloud/HMIChat_OpenAI.h"
#include "Cloud/HMITextToSpeech_Elevenlabs.h"

#include "GGML/HMIChat_Llama.h"
#include "GGML/HMISpeechToText_Whisper.h"
#if HMI_WITH_OMNIVOICE
#include "GGML/HMITextToSpeech_OmniVoice.h"
#include "GGML/HMITextToSpeech_QwenTTS.h"
#endif

#include "ONNX/HMITextToSpeech_Sherpa.h"
#include "ONNX/HMISpeechToText_Sherpa.h"
#include "ONNX/HMITextToSpeech_Piper.h"
#include "ONNX/HMIFaceDetector_FER.h"

#include "OVR/HMILipSync_OVR.h"

#if PLATFORM_WINDOWS
	#include "Windows/AllowWindowsPlatformTypes.h"
	#include <Psapi.h>
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

DECLARE_FLOAT_ACCUMULATOR_STAT(TEXT("WorkingSet(MB)"), STAT_HMI_WorkingSet, STATGROUP_HMI)
DECLARE_FLOAT_ACCUMULATOR_STAT(TEXT("PrivateUsage(MB)"), STAT_HMI_PrivateUsage, STATGROUP_HMI)

//
// GetInstance
//

UHMIBackendSubsystem* UHMIBackendSubsystem::GetInstance(UObject* WorldContextObject)
{
	if (WorldContextObject)
	{
		UWorld* World = WorldContextObject->GetWorld();
		if (World)
		{
			UGameInstance* GameInst = World->GetGameInstance();
			if (GameInst)
			{
				return GameInst->GetSubsystem<UHMIBackendSubsystem>();
			}
		}
	}
	return nullptr;
}

//
// UHMIBackendSubsystem
//

UHMIBackendSubsystem::UHMIBackendSubsystem()
{
}

UHMIBackendSubsystem::~UHMIBackendSubsystem()
{
}

void UHMIBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UE_LOG(LogHMI, Verbose, TEXT("UHMIBackendSubsystem::Initialize"));

	FHMIBackendModule& Module = FHMIBackendModule::Get();

	UHMISubsystem* HMI = GetGameInstance()->GetSubsystem<UHMISubsystem>();
	if (!HMI)
	{
		UE_LOG(LogHMI, Error, TEXT("UHMISubsystem::GetInstance"));
		return;
	}

	HMI->OnProcessorCreated.AddUObject(this, &UHMIBackendSubsystem::OnProcessorCreated);

	#define HMI_REGISTER_PROVIDER(Impl)\
		HMI->RegisterProvider(Impl::ImplName, Impl::StaticClass())

	// Cloud

	#if HMI_WITH_OPENAI_CHAT
		HMI_REGISTER_PROVIDER(UHMIChat_OpenAI);
	#endif

	#if HMI_WITH_ELEVENLABS_TTS
		HMI_REGISTER_PROVIDER(UHMITextToSpeech_Elevenlabs);
	#endif

	// GGML

	#if HMI_WITH_LLAMA
		HMI_REGISTER_PROVIDER(UHMIChat_Llama);
		HMI->SetDefaultProvider(EHMIProcessorType::Chat, UHMIChat_Llama::ImplName);
	#endif

	#if HMI_WITH_WHISPER
		HMI_REGISTER_PROVIDER(UHMISpeechToText_Whisper);
		HMI->SetDefaultProvider(EHMIProcessorType::STT, UHMISpeechToText_Whisper::ImplName);
	#endif

	#if HMI_WITH_OMNIVOICE
		if (Module.HaveOmniVoice())
		{
			HMI_REGISTER_PROVIDER(UHMITextToSpeech_OmniVoice);
		}
	#endif

	#if HMI_WITH_QWENTTS
		if (Module.HaveQwenTTS())
		{
			HMI_REGISTER_PROVIDER(UHMITextToSpeech_QwenTTS);
		}
	#endif

	// ONNX

	#if HMI_WITH_SHERPA
		if (Module.HaveSherpa())
		{
			HMI_REGISTER_PROVIDER(UHMITextToSpeech_Sherpa);
			HMI_REGISTER_PROVIDER(UHMISpeechToText_Sherpa);
		}
	#endif

	#if HMI_WITH_PIPER
		HMI_REGISTER_PROVIDER(UHMITextToSpeech_Piper);
		HMI->SetDefaultProvider(EHMIProcessorType::TTS, UHMITextToSpeech_Piper::ImplName);
	#endif

	#if HMI_WITH_FER
		if (Module.HaveOpenCV())
		{
			HMI_REGISTER_PROVIDER(UHMIFaceDetector_FER);
			HMI->SetDefaultProvider(EHMIProcessorType::FaceDetector, UHMIFaceDetector_FER::ImplName);
		}
	#endif

	// OVR

	#if HMI_WITH_OVRLIPSYNC
		if (Module.HaveOVRLipSync())
		{
			HMI_REGISTER_PROVIDER(UHMILipSync_OVR);
			HMI->SetDefaultProvider(EHMIProcessorType::LipSync, UHMILipSync_OVR::ImplName);
		}
	#endif

	#undef HMI_REGISTER_PROVIDER

	// Tick

	#if !UE_BUILD_SHIPPING
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("HMIBackendTicker"), 0.0f, [this](float DeltaTime)
		{
			TickSubsystem(DeltaTime);
			return true;
		});
	#endif

	UE_LOG(LogHMI, Verbose, TEXT("UHMIBackendSubsystem::Initialize DONE"));
}

void UHMIBackendSubsystem::Deinitialize()
{
	UE_LOG(LogHMI, Verbose, TEXT("UHMIBackendSubsystem::Deinitialize"));

	#if !UE_BUILD_SHIPPING
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	#endif

	UHMISubsystem* HMI = GetGameInstance()->GetSubsystem<UHMISubsystem>();
	if (HMI)
	{
		HMI->OnProcessorCreated.RemoveAll(this);
		HMI->StopAllProcessors(true);
	}

	UE_LOG(LogHMI, Verbose, TEXT("UHMIBackendSubsystem::Deinitialize DONE"));
}

void UHMIBackendSubsystem::OnProcessorCreated(UHMIProcessor* Processor)
{
	FHMIBackendModule& Module = FHMIBackendModule::Get();

	#if HMI_WITH_OMNIVOICE
		if (Module.HaveOmniVoice())
		{
			if (auto* OVTTS = Cast<UHMITextToSpeech_OmniVoice>(Processor))
			{
				OVTTS->SetOmniVoiceAPI(Module.GetOmniVoiceAPI());
				return;
			}
		}
	#endif

	#if HMI_WITH_QWENTTS
		if (Module.HaveQwenTTS())
		{
			if (auto* QTTTS = Cast<UHMITextToSpeech_QwenTTS>(Processor))
			{
				QTTTS->SetQwenTTSAPI(Module.GetQwenTTSAPI());
				return;
			}
		}
	#endif

	#if HMI_WITH_SHERPA
		if (Module.HaveSherpa())
		{
			if (auto* SherpaTTS = Cast<UHMITextToSpeech_Sherpa>(Processor))
			{
				SherpaTTS->SetSherpaAPI(Module.GetSherpaAPI());
				return;
			}
			else if (auto* SherpaSTT = Cast<UHMISpeechToText_Sherpa>(Processor))
			{
				SherpaSTT->SetSherpaAPI(Module.GetSherpaAPI());
				return;
			}
		}
	#endif

	#if HMI_WITH_PIPER
		if (Module.HavePiper())
		{
			if (auto* PiperTTS = Cast<UHMITextToSpeech_Piper>(Processor))
			{
				PiperTTS->SetPiperAPI(Module.GetPiperAPI());
				return;
			}
		}
	#endif
}

void UHMIBackendSubsystem::TickSubsystem(float DeltaTime)
{
	#if PLATFORM_WINDOWS
		PROCESS_MEMORY_COUNTERS_EX Counters;
		ZeroMemory(&Counters, sizeof(Counters));
		Counters.cb = sizeof(Counters);
		GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&Counters, sizeof(Counters));

		SET_FLOAT_STAT(STAT_HMI_WorkingSet, Counters.WorkingSetSize * 1e-6f); // MB
		SET_FLOAT_STAT(STAT_HMI_PrivateUsage, Counters.PrivateUsage * 1e-6f); // MB
	#endif
}
