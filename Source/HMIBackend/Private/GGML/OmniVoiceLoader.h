#pragma once

#include "omnivoice.h"

struct FOmniVoiceAPI
{
	typedef const char* (*ov_version_t)(void);
	ov_version_t ov_version_fp = nullptr;

	typedef const char* (*ov_last_error_t)(void);
	ov_last_error_t ov_last_error_fp = nullptr;

	typedef void (*ov_audio_free_t)(struct ov_audio*);
	ov_audio_free_t ov_audio_free_fp = nullptr;

	typedef void (*ov_init_default_params_t)(struct ov_init_params*);
	ov_init_default_params_t ov_init_default_params_fp = nullptr;

	typedef struct ov_context* (*ov_init_t)(const struct ov_init_params*);
	ov_init_t ov_init_fp = nullptr;

	typedef void (*ov_free_t)(struct ov_context*);
	ov_free_t ov_free_fp = nullptr;

	typedef void (*ov_tts_default_params_t)(struct ov_tts_params*);
	ov_tts_default_params_t ov_tts_default_params_fp = nullptr;

	typedef enum ov_status (*ov_synthesize_t)(struct ov_context*, const struct ov_tts_params*, struct ov_audio*);
	ov_synthesize_t ov_synthesize_fp = nullptr;

	typedef int (*ov_duration_sec_to_tokens_t)(const struct ov_context*, float);
	ov_duration_sec_to_tokens_t ov_duration_sec_to_tokens_fp = nullptr;

	typedef void (*ov_log_set_t)(ov_log_cb, void*);
	ov_log_set_t ov_log_set_fp = nullptr;

	typedef enum ov_status (*ov_encode_reference_audio_t)(struct ov_context*, const float*, int, int32_t**, int*);
	ov_encode_reference_audio_t ov_encode_reference_audio_fp = nullptr;

	typedef void (*ov_audio_tokens_free_t)(int32_t*);
	ov_audio_tokens_free_t ov_audio_tokens_free_fp = nullptr;
};

inline bool LoadOmniVoiceAPI(const FString& DllPath, FOmniVoiceAPI& OutAPI)
{
	void* DllHandle = FPlatformProcess::GetDllHandle(*DllPath);
	if (!DllHandle) return false;

	#define OV_LOAD(name) OutAPI.name##_fp = (FOmniVoiceAPI::name##_t)FPlatformProcess::GetDllExport(DllHandle, TEXT(#name))

	OV_LOAD(ov_version);
	OV_LOAD(ov_last_error);
	OV_LOAD(ov_audio_free);
	OV_LOAD(ov_init_default_params);
	OV_LOAD(ov_init);
	OV_LOAD(ov_free);
	OV_LOAD(ov_tts_default_params);
	OV_LOAD(ov_synthesize);
	OV_LOAD(ov_duration_sec_to_tokens);
	OV_LOAD(ov_log_set);
	OV_LOAD(ov_encode_reference_audio);
	OV_LOAD(ov_audio_tokens_free);

	#undef OV_LOAD

	return OutAPI.ov_init_fp != nullptr && OutAPI.ov_synthesize_fp != nullptr;
}
