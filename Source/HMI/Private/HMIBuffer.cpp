#include "HMIBuffer.h"

#include "Audio.h"
#include "AudioResampler.h"
#include "AudioDecompress.h"
#include "Interfaces/IAudioFormat.h"
#include "Misc/FileHelper.h"

const FName FHMIWaveFormat::PCM_S16("PCM_S16");
const FName FHMIWaveFormat::PCM_F32("PCM_F32");

bool UHMIBufferStatics::SaveWaveBuffer(const FHMIWaveHandle& Wave, const FString& Filepath)
{
	UE_LOG(LogHMI, Verbose, TEXT("SaveWaveBuffer: %s"), *Filepath);

	TArray<uint8> OutWaveFileData;

	SerializeWaveFile(OutWaveFileData, 
		Wave.CastU8(), 
		Wave.GetSize(), 
		Wave.GetNumChannels(), 
		Wave.GetSampleRate());

	if (OutWaveFileData.IsEmpty())
	{
		UE_LOG(LogHMI, Error, TEXT("SerializeWaveFile: %s"), *Filepath);
		return false;
	}

	if (!FFileHelper::SaveArrayToFile(OutWaveFileData, *Filepath))
	{
		UE_LOG(LogHMI, Error, TEXT("SaveArrayToFile: %s"), *Filepath);
		return false;
	}

	return true;
}

FHMIWaveHandle UHMIBufferStatics::LoadWaveBuffer(const FString& Filepath)
{
	UE_LOG(LogHMI, Verbose, TEXT("LoadWaveBuffer: %s"), *Filepath);

    TArray<uint8> RawFileData;
    if (!FFileHelper::LoadFileToArray(RawFileData, *Filepath))
    {
        UE_LOG(LogHMI, Error, TEXT("LoadFileToArray: %s"), *Filepath);
        return {};
    }

    FWaveModInfo WaveInfo;
    if (!WaveInfo.ReadWaveInfo(RawFileData.GetData(), RawFileData.Num()))
    {
        UE_LOG(LogHMI, Error, TEXT("ReadWaveInfo: %s"), *Filepath);
        return {};
    }

    auto Wave = FHMIWaveHandle::Alloc_PCM_S16((uint32)*WaveInfo.pSamplesPerSec, (uint8)*WaveInfo.pChannels);
	Wave.CopyFrom(WaveInfo.SampleDataStart, WaveInfo.SampleDataSize);

	return Wave;
}

FHMIWaveHandle UHMIBufferStatics::ConvertSoundToWaveBuffer(class USoundWave* SoundWave)
{
	if (!SoundWave) 
		return {};

	UE_LOG(LogHMI, Verbose, TEXT("CreateWaveBuffer: %s"), *SoundWave->GetName());

	const FName FormatName = SoundWave->GetRuntimeFormat();
	if (FormatName.IsNone())
	{
		UE_LOG(LogHMI, Error, TEXT("USoundWave::GetRuntimeFormat"));
		return {};
	}

	if (!SoundWave->InitAudioResource(FormatName))
	{
		UE_LOG(LogHMI, Error, TEXT("InitAudioResource"));
		return {};
	}

	TUniquePtr<ICompressedAudioInfo> AudioInfo(IAudioInfoFactoryRegistry::Get().Create(FormatName));
	if (!AudioInfo)
	{
		UE_LOG(LogHMI, Error, TEXT("IAudioInfoFactoryRegistry::Create"));
		return {};
	}

	FSoundQualityInfo QualityInfo = {0};
	if (SoundWave->IsStreaming())
	{
		if (!AudioInfo->StreamCompressedInfo(SoundWave, &QualityInfo))
		{
			UE_LOG(LogHMI, Error, TEXT("StreamCompressedInfo"));
			return {};
		}
	}
	else
	{
		const int32 ResourceSize = SoundWave->GetResourceSize();
		if (ResourceSize <= 0)
		{
			UE_LOG(LogHMI, Error, TEXT("USoundWave::GetResourceSize"));
			return {};
		}

		const uint8* ResourceData = SoundWave->GetResourceData();
		if (!ResourceData)
		{
			UE_LOG(LogHMI, Error, TEXT("USoundWave::GetResourceData"));
			return {};
		}

		if (!AudioInfo->ReadCompressedInfo(ResourceData, ResourceSize, &QualityInfo))
		{
			UE_LOG(LogHMI, Error, TEXT("ReadCompressedInfo"));
			return {};
		}
	}

	if (!QualityInfo.SampleDataSize)
	{
		UE_LOG(LogHMI, Error, TEXT("SampleDataSize"));
		return {};
	}

	auto Wave = FHMIWaveHandle::Alloc_PCM_S16((uint32)SoundWave->GetSampleRateForCurrentPlatform(), (uint8)SoundWave->NumChannels);
	Wave.Resize(QualityInfo.SampleDataSize);
	AudioInfo->ExpandFile(Wave.CastU8(), &QualityInfo);

	return Wave;
}

bool UHMIBufferStatics::IsValidSampleRate(int SampleRate)
{
	//return SampleRate > 0 && SampleRate <= 48000;

	switch (SampleRate)
	{
		case 8000:  // Adequate for human speech but without sibilance. Used in telephone/walkie-talkie.
		case 11025: // Used for lower-quality PCM, MPEG audio and for audio analysis of subwoofer bandpasses.
		case 16000: // Used in most VoIP and VVoIP, extension of telephone narrowband.
		case 22050: // Used for lower-quality PCM and MPEG audio and for audio analysis of low frequency energy.
		case 24000: // Used by OmniVoice and other neural TTS codecs.
		case 44100: // Audio CD, most commonly used rate with MPEG-1 audio (VCD, SVCD, MP3). Covers the 20 kHz bandwidth.
		case 48000: // Standard sampling rate used by professional digital video equipment, could reconstruct frequencies up to 22 kHz.
			return true;
	}

	return false;
}

bool UHMIBufferStatics::IsValidFormat(const FHMIWaveFormat& Format)
{
	if (Format.FormatId == FHMIWaveFormat::PCM_S16)
	{
		return IsValidSampleRate(Format.SampleRate) 
			&& (Format.NumChannels == 1 || Format.NumChannels == 2)
			&& Format.SampleBits == 16;
	}
	else if (Format.FormatId == FHMIWaveFormat::PCM_F32)
	{
		return IsValidSampleRate(Format.SampleRate) 
			&& (Format.NumChannels == 1 || Format.NumChannels == 2)
			&& Format.SampleBits == 32;
	}

	return false;
}

bool UHMIBufferStatics::IsValidVoice(const FHMIWaveHandle& Wave)
{
	const auto Fmt = Wave.GetFormat();

	return Fmt.FormatId == FHMIWaveFormat::PCM_S16
		&& UHMIBufferStatics::IsValidSampleRate(Fmt.SampleRate)
		&& Fmt.SampleBits == 16
		&& Fmt.NumChannels == HMI_VOICE_CHANNELS
	;
}
