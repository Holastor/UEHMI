#pragma once

#include "Processors/HMITextToSpeech.h"
#include "Engine/TimerHandle.h"
#include "HMITextToSpeech_Elevenlabs.generated.h"

USTRUCT()
struct HMIBACKEND_API FHMITextToSpeech_Elevenlabs_Operation
{
	GENERATED_BODY()

	UPROPERTY()
	FHMITextToSpeechInput Input;

	UPROPERTY()
	double RequestTimestamp = 0;

	UPROPERTY()
	double ResponseTimestamp = 0;
};

UCLASS()
class HMIBACKEND_API UHMITextToSpeech_Elevenlabs : 
	public UHMITextToSpeech,
	public THMIProcessorQueue<FHMITextToSpeech_Elevenlabs_Operation, FHMITextToSpeechInput>
{
	GENERATED_BODY()

	public:

	static const FName ImplName;

	UFUNCTION(BlueprintCallable, Category="HMI|Backend", meta=(WorldContext="WorldContextObject"))
	static class UHMITextToSpeech* GetOrCreateTTS_Elevenlabs(UObject* WorldContextObject, 
		FName Name = NAME_None,
		FString BackendKey = TEXT(""),
		FString VoiceId = TEXT(""),
		float VoiceSpeed = 1.0f
	);

	UHMITextToSpeech_Elevenlabs(const FObjectInitializer& ObjectInitializer);
	UHMITextToSpeech_Elevenlabs(FVTableHelper& Helper);
	~UHMITextToSpeech_Elevenlabs();

	public:

	#if HMI_WITH_ELEVENLABS_TTS

	virtual bool IsProcessorImplemented() const override { return true; }
	virtual int64 ProcessInput(FHMITextToSpeechInput&& Input) override;
	virtual void CancelOperation(bool PurgeQueue = false) override;
	
	protected:

	virtual bool Proc_Init() override;
	virtual bool Proc_DoWork(int& QueueLength) override;
	virtual void Proc_PostWorkStats() override;
	virtual void Proc_Release() override;

	virtual void HandleOperationComplete(bool Success, FString&& Error, FHMITextToSpeechInput&& Input, FHMIWaveHandle&& Wave);

	bool InitSocket();
	void ResetSocket();
	void SendInitContext();
	void SendCloseContext();
	void SendJson(const TSharedRef<class FJsonObject>& Obj);
	void SendMsg(const FString& Msg);
	void OnMsg(const FString& Msg);
	void OnOperationTimeout();

	FString BackendUrl;
	FString BackendKey;
	FString VoiceId;
	FString ModelId;
	FString BackendOutputFormat;
	int BackendSampleRate = 0;
	int BackendNumChannels = 0;
	float OperationTimeout = 0;

	mutable FCriticalSection WebSocketMux;
	TSharedPtr<class IWebSocket> WebSocket;
	FDelegateHandle CbConnected;
	FDelegateHandle CbConnectionError;
	FDelegateHandle CbClosed;
	FDelegateHandle CbMessage;
	bool IsConnectPending = false;
	bool IsContextInitialized = false;
	FString ConnectionErrorString;

	bool IsOperationPending = false;
	bool OperationSuccess = false;
	FString ErrorText;
	FEvent* CompletionEvent = nullptr;
	FTimerHandle PendingTimer;
	FHMIWaveHandle OutputWave;

	#endif // HMI_WITH_ELEVENLABS_TTS

	protected:

	UPROPERTY(Transient)
	TArray<FHMITextToSpeech_Elevenlabs_Operation> InputQueue;

	UPROPERTY(Transient)
	FHMITextToSpeech_Elevenlabs_Operation Operation;
};
