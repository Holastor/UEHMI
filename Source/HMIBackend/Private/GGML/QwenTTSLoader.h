#pragma once

#include "qwen.h"

struct FQwenTTSAPI
{
	typedef const char* (*qt_version_t)(void);
	qt_version_t qt_version_fp = nullptr;

	typedef const char* (*qt_last_error_t)(void);
	qt_last_error_t qt_last_error_fp = nullptr;

	typedef void (*qt_audio_free_t)(struct qt_audio*);
	qt_audio_free_t qt_audio_free_fp = nullptr;

	typedef void (*qt_init_default_params_t)(struct qt_init_params*);
	qt_init_default_params_t qt_init_default_params_fp = nullptr;

	typedef struct qt_context* (*qt_init_t)(const struct qt_init_params*);
	qt_init_t qt_init_fp = nullptr;

	typedef void (*qt_free_t)(struct qt_context*);
	qt_free_t qt_free_fp = nullptr;

	typedef void (*qt_tts_default_params_t)(struct qt_tts_params*);
	qt_tts_default_params_t qt_tts_default_params_fp = nullptr;

	typedef enum qt_status (*qt_synthesize_t)(struct qt_context*, const struct qt_tts_params*, struct qt_audio*);
	qt_synthesize_t qt_synthesize_fp = nullptr;

	typedef int (*qt_duration_sec_to_tokens_t)(const struct qt_context*, float);
	qt_duration_sec_to_tokens_t qt_duration_sec_to_tokens_fp = nullptr;

	typedef void (*qt_log_set_t)(qt_log_cb, void*);
	qt_log_set_t qt_log_set_fp = nullptr;
};

inline bool LoadQwenTTSAPI(const FString& DllPath, FQwenTTSAPI& OutAPI)
{
	void* DllHandle = FPlatformProcess::GetDllHandle(*DllPath);
	if (!DllHandle) return false;

	#define QT_LOAD(name) OutAPI.name##_fp = (FQwenTTSAPI::name##_t)FPlatformProcess::GetDllExport(DllHandle, TEXT(#name))

	QT_LOAD(qt_version);
	QT_LOAD(qt_last_error);
	QT_LOAD(qt_audio_free);
	QT_LOAD(qt_init_default_params);
	QT_LOAD(qt_init);
	QT_LOAD(qt_free);
	QT_LOAD(qt_tts_default_params);
	QT_LOAD(qt_synthesize);
	QT_LOAD(qt_duration_sec_to_tokens);
	QT_LOAD(qt_log_set);

	#undef QT_LOAD

	return OutAPI.qt_init_fp != nullptr && OutAPI.qt_synthesize_fp != nullptr;
}
