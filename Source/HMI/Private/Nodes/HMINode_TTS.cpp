#include "HMINode_TTS.h"
#include "HMISubsystem.h"

UHMINode_TTS* UHMINode_TTS::TTS_Async(UObject* WorldContextObject, UHMIProcessor* Processor, FName UserTag, FString Text, FString VoiceId, float Speed, bool Streaming)
{
	UHMINode_TTS* Node = NewObject<UHMINode_TTS>();
	Node->WorldContext = WorldContextObject;
	Node->Processor = Cast<UHMITextToSpeech>(Processor);
	Node->UserTag = MoveTemp(UserTag);
	Node->Text = MoveTemp(Text);
	Node->VoiceId = MoveTemp(VoiceId);
	Node->Speed = Speed;
	Node->Streaming = Streaming;
	return Node;
}

void UHMINode_TTS::Activate()
{
	if (Complete.IsBound() || (Streaming && Chunk.IsBound()))
	{
		UHMISubsystem* HMI = UHMISubsystem::GetInstance(WorldContext);
		if (ensure(HMI))
		{
			if (!Processor)
				Processor = HMI->GetOrCreateTTS();

			if (ensure(Processor))
			{
				FHMITextToSpeechInput Input(MoveTemp(UserTag), MoveTemp(Text), MoveTemp(VoiceId), Speed);

				if (Streaming)
				{
					Input.TTSChunkFunc = [this](const FName& InUserTag, const FHMIWaveHandle& ChunkWave, bool EndOfStream)
					{
						AsyncTask(ENamedThreads::GameThread, [this, InUserTag = FName(InUserTag), ChunkWave = FHMIWaveHandle(ChunkWave), EndOfStream]()
						{
							FHMITTSChunkOutput ChunkOutput;
							ChunkOutput.ChunkWave = ChunkWave;
							ChunkOutput.EndOfStream = EndOfStream;
							Chunk.Broadcast(InUserTag, ChunkOutput);
							if (EndOfStream)
								SetReadyToDestroy();
						});
					};

					Input.CompleteFunc = [this](FHMIProcessorInput& ProcInput, FHMIProcessorResult& ProcResult)
					{
						auto& Result = static_cast<FHMITextToSpeechResult&>(ProcResult);

						AsyncTask(ENamedThreads::GameThread, [this, Result = MoveTemp(Result)]() mutable
						{
							if (!Result.Success)
							{
								FHMITTSChunkOutput ChunkOutput;
								ChunkOutput.EndOfStream = true;
								Chunk.Broadcast(NAME_None, ChunkOutput);
							}
							SetReadyToDestroy();
						});
					};
				}
				else
				{
					Input.CompleteFunc = [this](FHMIProcessorInput& ProcInput, FHMIProcessorResult& ProcResult)
					{
						auto& Input = static_cast<FHMITextToSpeechInput&>(ProcInput);
						auto& Result = static_cast<FHMITextToSpeechResult&>(ProcResult);

						AsyncTask(ENamedThreads::GameThread, [this, Input = MoveTemp(Input), Result = MoveTemp(Result)]() mutable
						{
							Complete.Broadcast(Input, Result);
							SetReadyToDestroy();
						});
					};
				}

				RegisterWithGameInstance(WorldContext);
				Processor->ProcessInput(MoveTemp(Input));
			}
		}
	}
}
