#pragma once

#include "Modules/ModuleManager.h"

class FHMIBackendModule : public IModuleInterface
{
	public:

	static inline FHMIBackendModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FHMIBackendModule>("HMIBackend");
	}

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	bool HaveOpenCV() const { return bHaveOpenCV; }
	bool HaveOnnx() const { return bHaveOnnx; }
	bool HaveSherpa() const { return bHaveSherpa; }
	bool HavePiper() const { return bHavePiper; }
	bool HaveOVRLipSync() const { return bHaveOVRLipSync; }
	bool HaveOmniVoice() const { return bHaveOmniVoice; }
	bool HaveQwenTTS() const { return bHaveQwenTTS; }

	struct FSherpaAPI* GetSherpaAPI() { return SherpaAPI.Get(); }
	struct piper_api* GetPiperAPI() { return PiperAPI.Get(); }
	struct FOmniVoiceAPI* GetOmniVoiceAPI() { return OmniVoiceAPI.Get(); }
	struct FQwenTTSAPI* GetQwenTTSAPI() { return QwenTTSAPI.Get(); }

	private:

	void* OpenCVHandle = nullptr;
	void* OnnxHandle = nullptr;
	void* WhisperHandle = nullptr;
	void* LlamaHandle = nullptr;
	void* PiperHandle = nullptr;
	TUniquePtr<struct FSherpaAPI> SherpaAPI;
	TUniquePtr<struct piper_api> PiperAPI;
	TUniquePtr<struct FOmniVoiceAPI> OmniVoiceAPI;
	TUniquePtr<struct FQwenTTSAPI> QwenTTSAPI;

	bool bHaveOpenCV = false;
	bool bHaveOnnx = false;
	bool bHaveSherpa = false;
	bool bHavePiper = false;
	bool bHaveOVRLipSync = false;
	bool bHaveOmniVoice = false;
	bool bHaveQwenTTS = false;
};
