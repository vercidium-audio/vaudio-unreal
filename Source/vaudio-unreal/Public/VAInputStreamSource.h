// Human-created

#pragma once

#include "CoreMinimal.h"
#include "AudioCaptureCore.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Queue.h"
#include "VAStreamSource.h"
#include "VAInputStreamSource.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVAOnAudioCaptured, const TArray<uint8>&, Data);

// A source that plays a data stream, either from microphone or networking
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture")
	EVAStreamFormat Format = EVAStreamFormat::Mono16;

	// Requested capture sample rate. Set to 0 to use the device's default rate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture", meta = (ClampMin = "0", UIMax = "96000"))
	int32 SampleRate = 0;

	// Name of the capture device. Set to an empty string or "System Default" to use the system default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture", meta = (GetOptions = "GetCaptureDeviceNames"))
	FString DeviceName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture", meta = (ClampMin = "1", UIMax = "65536"))
	int32 BufferSizeFrames = 1024;

	// If the peak amplitude is below this value, the data will be ignored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture", meta = (ClampMin = "0", UIMax = "32767"))
	int32 MicrophoneThreshold = 0;

	// Whether to start capturing on BeginPlay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Capture")
	bool bAutoCapture = true;

	// Callback containing audio data, for sending over networking
	UPROPERTY(BlueprintAssignable, Category = "Vercidium Audio|Capture")
	FVAOnAudioCaptured OnAudioCaptured;

	// Open a capture device, closing any open one first. Set InSampleRate to 0 to use the device's default rate. Returns false if the device couldn't be opened
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Capture")
	bool OpenCapture(const FString& InDeviceName, EVAStreamFormat InFormat = EVAStreamFormat::Mono16, int32 InSampleRate = 0, int32 InBufferSizeFrames = 1024);

	// Start capturing, e.g. for push to talk
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Capture")
	void StartCapture();

	// Stop capturing, e.g. for push to talk
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Capture")
	void StopCapture();

	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Capture")
	void CloseCapture();

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Capture")
	bool IsCaptureOpen() const { return Capture.IsValid(); }

	// The rate the capture device actually captures at. Returns 0 when not open
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Capture")
	int32 GetCaptureSampleRate() const { return CaptureSampleRate; }

	// The names of all available capture devices
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Capture")
	static TArray<FString> GetCaptureDeviceNames();

	// Get the greatest amplitude, scaled to a 16-bit range so MicrophoneThreshold means the same thing in every format
	static int32 PeakAmplitude(const uint8* Data, int32 NumBytes, EVAStreamFormat InFormat);

	// Convert float data from the capture device to CaptureFormat and queues them for the next tick. Called on the capture thread
	void ReceiveCapturedAudio(const float* Audio, int32 NumFrames, int32 NumChannels);

	// Number of bytes in chunks that are above MicrophoneThreshold and were forwarded since OpenCapture
	int64 GetForwardedByteCount() const { return ForwardedByteCount; }

	// Number of bytes in chunks that are below MicrophoneThreshold since OpenCapture
	int64 GetBelowThresholdByteCount() const { return BelowThresholdByteCount; }

	// For testing without a capture device
	void SetCaptureFormatForTesting(EVAStreamFormat InFormat) { CaptureFormat = InFormat; }

private:
	// Forward the audio chunks queued on the capture thread
	void FlushCapturedAudio();

	TUniquePtr<Audio::FAudioCapture> Capture;

	EVAStreamFormat CaptureFormat = EVAStreamFormat::Mono16;
	int32 CaptureSampleRate = 0;

	TQueue<TArray<uint8>, EQueueMode::Spsc> CapturedChunks;

	// Number of queued audio chunks that aren't forwarded yet. If it exceeds MaxPendingChunks, it is ignored. This prevents memory leaks if this actor stops ticking
	FThreadSafeCounter PendingChunkCount;

	int64 ForwardedByteCount = 0;
	int64 BelowThresholdByteCount = 0;
};
