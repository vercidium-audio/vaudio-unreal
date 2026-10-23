#pragma once

#include "CoreMinimal.h"
#include "AudioCaptureCore.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Queue.h"
#include "VAStreamSource.h"
#include "VAInputStreamSource.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVAOnAudioCaptured, const TArray<uint8>&, Data);

// A stream source fed by a microphone or other capture device. On BeginPlay it opens the capture device and a stream in Format, and plays back what it captures (e.g. to hear yourself, or send it over voice chat via OnAudioCaptured)
UCLASS(DisplayName = "VAInputStreamSource")
class VAUDIOUNREAL_API AVAInputStreamSource : public AVAStreamSource
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;

protected:
	virtual void DeinitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	// Format of the captured data, i.e. what's pushed to the stream and passed to OnAudioCaptured
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture")
	EVAStreamFormat Format = EVAStreamFormat::Mono16;

	// Requested capture sample rate. Some devices only capture at their own rate, in which case the stream uses that instead (see GetCaptureSampleRate)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture", meta = (ClampMin = "8000", UIMax = "96000"))
	int32 SampleRate = 44100;

	// Capture device to open. Empty or "System Default" uses the system's default device
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture", meta = (GetOptions = "GetCaptureDeviceNames"))
	FString DeviceName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture", meta = (ClampMin = "1", UIMax = "65536"))
	int32 BufferSizeFrames = 1024;

	// Minimum peak amplitude (in a 16-bit range, 0-32767, whatever the Format) a captured chunk must reach to be played or passed to OnAudioCaptured. 0 forwards every chunk
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture", meta = (ClampMin = "0", UIMax = "32767"))
	int32 MicrophoneThreshold = 0;

	// Opens the stream and starts capturing on BeginPlay. Turn off to call OpenStream/OpenCapture/StartCapture yourself, e.g. for push-to-talk
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture")
	bool bAutoCapture = true;

	// Called on the game thread with each captured chunk that reaches MicrophoneThreshold, in Format. Use it to send the microphone over the network
	UPROPERTY(BlueprintAssignable, Category = "Vercidium Audio|Capture")
	FVAOnAudioCaptured OnAudioCaptured;

	// Opens a capture device, closing any open one first. Captured audio is pushed to this source's stream (if open) and broadcast via OnAudioCaptured. Returns false if the device couldn't be opened
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Capture")
	bool OpenCapture(const FString& InDeviceName, EVAStreamFormat InFormat = EVAStreamFormat::Mono16, int32 InSampleRate = 44100, int32 InBufferSizeFrames = 1024);

	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Capture")
	void StartCapture();

	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Capture")
	void StopCapture();

	// Safe to call when no capture device is open
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Capture")
	void CloseCapture();

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Capture")
	bool IsCaptureOpen() const { return Capture.IsValid(); }

	// The rate the open capture device actually captures at, or 0 when none is open
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Capture")
	int32 GetCaptureSampleRate() const { return CaptureSampleRate; }

	// "System Default" followed by the names of the available capture devices
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Capture")
	static TArray<FString> GetCaptureDeviceNames();

	// Greatest absolute sample amplitude in a chunk of PCM data, scaled to a 16-bit range so MicrophoneThreshold means the same thing in every format
	static int32 PeakAmplitude(const uint8* Data, int32 NumBytes, EVAStreamFormat InFormat);

	// Converts interleaved float frames from the capture device to CaptureFormat and queues them for the next tick. Called on the capture thread, and by tests to feed synthetic capture buffers
	void ReceiveCapturedAudio(const float* Audio, int32 NumFrames, int32 NumChannels);

	// Bytes that reached MicrophoneThreshold and were forwarded since OpenCapture
	int64 GetForwardedByteCount() const { return ForwardedByteCount; }

	// Bytes below MicrophoneThreshold since OpenCapture
	int64 GetBelowThresholdByteCount() const { return BelowThresholdByteCount; }

	// Lets tests feed ReceiveCapturedAudio without a capture device
	void SetCaptureFormatForTesting(EVAStreamFormat InFormat) { CaptureFormat = InFormat; }

private:
	// Forwards the chunks queued by the capture thread
	void FlushCapturedAudio();

	TUniquePtr<Audio::FAudioCapture> Capture;

	EVAStreamFormat CaptureFormat = EVAStreamFormat::Mono16;
	int32 CaptureSampleRate = 0;

	TQueue<TArray<uint8>, EQueueMode::Spsc> CapturedChunks;

	// Chunks queued but not yet forwarded, so the queue can't grow without bound if this actor stops ticking
	FThreadSafeCounter PendingChunkCount;

	int64 ForwardedByteCount = 0;
	int64 BelowThresholdByteCount = 0;
};
