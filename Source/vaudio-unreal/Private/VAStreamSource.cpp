#include "VAStreamSource.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/World.h"

#include "VALog.h"

static int32 GetBytesPerFrame(EVAStreamFormat Format)
{
	switch (Format)
	{
	case EVAStreamFormat::Mono8: return 1;
	case EVAStreamFormat::Mono16: return 2;
	case EVAStreamFormat::Stereo8: return 2;
	case EVAStreamFormat::Stereo16: return 4;
	}

	return 2;
}

bool AVAStreamSource::OpenStream(EVAStreamFormat Format, int32 SampleRate)
{
	CloseStream();

	if (SampleRate <= 0)
	{
		VA_ERROR_NAMED(TEXT("OpenStream failed: invalid sample rate %d"), SampleRate);
		return false;
	}

	bool stereo = Format == EVAStreamFormat::Stereo8 || Format == EVAStreamFormat::Stereo16;

	StreamWave = NewObject<USoundWaveProcedural>(this);
	StreamWave->SetSampleRate(SampleRate);
	StreamWave->NumChannels = stereo ? 2 : 1;
	StreamWave->Duration = INDEFINITELY_LOOPING_DURATION;
	StreamWave->bLooping = false;
	StreamWave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	StreamWave->AttenuationSettings = Attenuation;

	StreamFormat = Format;
	bStreamOpen = true;
	bStreamReady = false;
	AcceptedByteCount = 0;
	DroppedByteCount = 0;

	// Raytracing doesn't need the sound, so the stream still opens without an audio device
	if (!GetWorld()->GetAudioDeviceRaw())
	{
		VA_LOG_NAMED(TEXT("No audio device, so the stream is raytraced but not heard"));
		return true;
	}

	UpdatePlayback();

	FAudioDevice::FCreateComponentParams Params(GetWorld(), this);
	Params.SetLocation(GetActorLocation());

	UAudioComponent* component = FAudioDevice::CreateComponent(StreamWave, Params);

	if (!component)
	{
		VA_WARN_NAMED(TEXT("OpenStream failed to create its sound. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
		CloseStream();
		return false;
	}

	component->SetWorldLocationAndRotation(GetActorLocation(), FRotator::ZeroRotator);
	component->bAllowSpatialization = true;
	component->bStopWhenOwnerDestroyed = false;

	// Plays silence until data is queued
	Playback.Play(component);
	return true;
}

void AVAStreamSource::PushAudioData(const TArray<uint8>& Data)
{
	PushAudioData(Data.GetData(), Data.Num());
}

void AVAStreamSource::PushAudioData(const uint8* Data, int32 NumBytes)
{
	if (!bStreamOpen)
	{
		VA_ERROR_NAMED(TEXT("PushAudioData called before OpenStream (or after CloseStream)"));
		return;
	}

	if (NumBytes <= 0)
		return;

	int32 bytesPerFrame = GetBytesPerFrame(StreamFormat);

	if (NumBytes % bytesPerFrame != 0)
	{
		VA_ERROR_NAMED(TEXT("PushAudioData got %d bytes, which isn't a whole number of %d-byte frames"), NumBytes, bytesPerFrame);
		return;
	}

	// Data that arrives before the muffling and reverb results is dropped, so the stream never plays unmuffled or without reverb
	if (!bStreamReady)
	{
		if (!IsReadyToPlay())
		{
			DroppedByteCount += NumBytes;
			return;
		}

		bStreamReady = true;
	}

	AcceptedByteCount += NumBytes;

	// Nothing consumes the queue without a playing sound, so don't grow it
	if (!Playback.IsPlaying())
		return;

	if (StreamFormat == EVAStreamFormat::Mono16 || StreamFormat == EVAStreamFormat::Stereo16)
	{
		StreamWave->QueueAudio(Data, NumBytes);
		return;
	}

	ConvertedSamples.SetNumUninitialized(NumBytes, EAllowShrinking::No);

	for (int32 i = 0; i < NumBytes; i++)
		ConvertedSamples[i] = (int16)(((int32)Data[i] - 128) << 8);

	StreamWave->QueueAudio((const uint8*)ConvertedSamples.GetData(), NumBytes * sizeof(int16));
}

void AVAStreamSource::CloseStream()
{
	if (!bStreamOpen)
		return;

	Playback.Stop();
	StreamWave = nullptr;
	bStreamOpen = false;
	bStreamReady = false;
}

void AVAStreamSource::Stop()
{
	CloseStream();
}

void AVAStreamSource::DeinitializeTypeSpecific()
{
	CloseStream();

	Super::DeinitializeTypeSpecific();
}

void AVAStreamSource::TickTypeSpecific(float DeltaTime)
{
	Super::TickTypeSpecific(DeltaTime);

	// Latched here too, since a bRaytraceOnce emitter leaves the world at the end of this tick
	if (bStreamOpen && !bStreamReady && IsReadyToPlay())
		bStreamReady = true;
}
