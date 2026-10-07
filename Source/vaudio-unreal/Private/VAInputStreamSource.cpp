#include "VAInputStreamSource.h"
#include "Modules/ModuleManager.h"

#include "VALog.h"

static const TCHAR* DefaultDeviceLabel = TEXT("System Default");

// About 1.5 seconds of 1024-frame chunks at 44.1 kHz, so a capture with nothing draining it (e.g. this actor stopped ticking) doesn't grow forever
static constexpr int32 MaxPendingChunks = 64;

// The AudioCapture plugin loads the platform's capture backend (WASAPI, RtAudio, ...) from its StartupModule
static void LoadCaptureBackend()
{
	FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("AudioCapture"));
}

static bool IsStereo(EVAStreamFormat Format)
{
	return Format == EVAStreamFormat::Stereo8 || Format == EVAStreamFormat::Stereo16;
}

static bool Is8Bit(EVAStreamFormat Format)
{
	return Format == EVAStreamFormat::Mono8 || Format == EVAStreamFormat::Stereo8;
}

TArray<FString> AVAInputStreamSource::GetCaptureDeviceNames()
{
	LoadCaptureBackend();

	TArray<FString> names;
	names.Add(DefaultDeviceLabel);

	Audio::FAudioCapture capture;
	TArray<Audio::FCaptureDeviceInfo> devices;
	capture.GetCaptureDevicesAvailable(devices);

	for (const Audio::FCaptureDeviceInfo& device : devices)
		names.Add(device.DeviceName);

	return names;
}

// 8-bit samples are unsigned (silence = 128) and 16-bit are signed (silence = 0). 8-bit amplitudes are scaled up by 256 so MicrophoneThreshold means the same thing in every format
int32 AVAInputStreamSource::PeakAmplitude(const uint8* Data, int32 NumBytes, EVAStreamFormat InFormat)
{
	int32 peak = 0;

	if (Is8Bit(InFormat))
	{
		for (int32 i = 0; i < NumBytes; i++)
			peak = FMath::Max(peak, FMath::Abs((int32)Data[i] - 128) * 256);
	}
	else
	{
		const int16* samples = (const int16*)Data;
		int32 sampleCount = NumBytes / 2;

		for (int32 i = 0; i < sampleCount; i++)
			peak = FMath::Max(peak, FMath::Abs((int32)samples[i]));
	}

	return peak;
}

void AVAInputStreamSource::BeginPlay()
{
	Super::BeginPlay();

	if (!bAutoCapture)
		return;

	if (!OpenCapture(DeviceName, Format, SampleRate, BufferSizeFrames))
		return;

	// The stream plays at whatever rate the device actually captures at
	if (!OpenStream(Format, CaptureSampleRate))
	{
		CloseCapture();
		return;
	}

	StartCapture();
}

bool AVAInputStreamSource::OpenCapture(const FString& InDeviceName, EVAStreamFormat InFormat, int32 InSampleRate, int32 InBufferSizeFrames)
{
	CloseCapture();

	LoadCaptureBackend();

	TUniquePtr<Audio::FAudioCapture> capture = MakeUnique<Audio::FAudioCapture>();

	Audio::FAudioCaptureDeviceParams params;
	params.SampleRate = InSampleRate;
	params.PCMAudioEncoding = Audio::EPCMAudioEncoding::FLOATING_POINT_32;

	if (!InDeviceName.IsEmpty() && InDeviceName != DefaultDeviceLabel)
	{
		TArray<Audio::FCaptureDeviceInfo> devices;
		capture->GetCaptureDevicesAvailable(devices);

		params.DeviceIndex = devices.IndexOfByPredicate([&](const Audio::FCaptureDeviceInfo& device) { return device.DeviceName == InDeviceName; });

		if (params.DeviceIndex == INDEX_NONE)
		{
			VA_WARN_NAMED(TEXT("Capture device '%s' was not found"), *InDeviceName);
			return false;
		}
	}

	// Set before the callback can run, as the capture thread reads it
	CaptureFormat = InFormat;

	Audio::FOnAudioCaptureFunction onCapture = [this](const void* InAudio, int32 NumFrames, int32 NumChannels, int32 InCaptureSampleRate, double StreamTime, bool bOverflow)
	{
		ReceiveCapturedAudio((const float*)InAudio, NumFrames, NumChannels);
	};

	if (!capture->OpenAudioCaptureStream(params, MoveTemp(onCapture), FMath::Max(1, InBufferSizeFrames)))
	{
		VA_WARN_NAMED(TEXT("Failed to open capture device '%s'"), InDeviceName.IsEmpty() ? DefaultDeviceLabel : *InDeviceName);
		return false;
	}

	Capture = MoveTemp(capture);
	CaptureSampleRate = Capture->GetSampleRate();
	ForwardedByteCount = 0;
	BelowThresholdByteCount = 0;

	if (CaptureSampleRate != InSampleRate)
		VA_LOG_NAMED(TEXT("Capture device doesn't support %d Hz, capturing at %d Hz instead"), InSampleRate, CaptureSampleRate);

	return true;
}

void AVAInputStreamSource::StartCapture()
{
	if (!Capture)
	{
		VA_ERROR_NAMED(TEXT("StartCapture called before OpenCapture (or after CloseCapture)"));
		return;
	}

	Capture->StartStream();
}

void AVAInputStreamSource::StopCapture()
{
	if (Capture)
		Capture->StopStream();
}

void AVAInputStreamSource::CloseCapture()
{
	if (!Capture)
		return;

	// Stops and joins the capture thread first, so no callback runs after this
	Capture->CloseStream();
	Capture.Reset();
	CaptureSampleRate = 0;

	CapturedChunks.Empty();
	PendingChunkCount.Reset();
}

void AVAInputStreamSource::ReceiveCapturedAudio(const float* Audio, int32 NumFrames, int32 NumChannels)
{
	if (!Audio || NumFrames <= 0 || NumChannels <= 0)
		return;

	if (PendingChunkCount.GetValue() >= MaxPendingChunks)
		return;

	EVAStreamFormat format = CaptureFormat;
	int32 outChannels = IsStereo(format) ? 2 : 1;
	bool eightBit = Is8Bit(format);

	TArray<uint8> chunk;
	chunk.SetNumUninitialized(NumFrames * outChannels * (eightBit ? 1 : 2));

	uint8* out8 = chunk.GetData();
	int16* out16 = (int16*)chunk.GetData();

	for (int32 frame = 0; frame < NumFrames; frame++)
	{
		const float* in = Audio + frame * NumChannels;

		for (int32 channel = 0; channel < outChannels; channel++)
		{
			float sample;

			// Mono averages every input channel, stereo takes the first two (duplicating a mono device)
			if (outChannels == 1)
			{
				sample = 0.0f;

				for (int32 i = 0; i < NumChannels; i++)
					sample += in[i];

				sample /= NumChannels;
			}
			else
			{
				sample = in[FMath::Min(channel, NumChannels - 1)];
			}

			sample = FMath::Clamp(sample, -1.0f, 1.0f);
			int32 index = frame * outChannels + channel;

			if (eightBit)
				out8[index] = (uint8)FMath::RoundToInt(sample * 127.0f + 128.0f);
			else
				out16[index] = (int16)FMath::RoundToInt(sample * 32767.0f);
		}
	}

	PendingChunkCount.Increment();
	CapturedChunks.Enqueue(MoveTemp(chunk));
}

void AVAInputStreamSource::FlushCapturedAudio()
{
	TArray<uint8> chunk;

	while (CapturedChunks.Dequeue(chunk))
	{
		PendingChunkCount.Decrement();

		if (PeakAmplitude(chunk.GetData(), chunk.Num(), CaptureFormat) < MicrophoneThreshold)
		{
			BelowThresholdByteCount += chunk.Num();
			continue;
		}

		ForwardedByteCount += chunk.Num();

		if (IsStreamOpen())
			PushAudioData(chunk);

		OnAudioCaptured.Broadcast(chunk);

		// A handler may have destroyed this actor
		if (!IsValid(this))
			return;
	}
}

void AVAInputStreamSource::TickTypeSpecific(float DeltaTime)
{
	Super::TickTypeSpecific(DeltaTime);

	FlushCapturedAudio();
}

void AVAInputStreamSource::DeinitializeTypeSpecific()
{
	CloseCapture();

	Super::DeinitializeTypeSpecific();
}

void AVAInputStreamSource::BeginDestroy()
{
	CloseCapture();

	Super::BeginDestroy();
}
