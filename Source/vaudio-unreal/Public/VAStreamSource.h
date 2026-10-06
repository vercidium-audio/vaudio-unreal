#pragma once

#include "CoreMinimal.h"
#include "VARaytracedSource.h"
#include "VAStreamSource.generated.h"

class USoundAttenuation;
class USoundWaveProcedural;

// Layout of the raw PCM data passed to PushAudioData. 8-bit samples are unsigned, 16-bit samples are signed, and stereo samples are interleaved
UENUM(BlueprintType)
enum class EVAStreamFormat : uint8
{
	Mono8,
	Mono16,
	Stereo8,
	Stereo16,
};

// A raytraced source that plays raw PCM audio pushed to it at runtime, e.g. a microphone or voice chat feed. Call OpenStream once, PushAudioData whenever a chunk is ready, and CloseStream when done. Plays silence while no data is queued, so it never stops on its own
UCLASS(DisplayName = "VAStreamSource")
class VAUDIOUNREAL_API AVAStreamSource : public AVARaytracedSource
{
	GENERATED_BODY()

protected:
	virtual void DeinitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	// Distance falloff for the stream. Without it the stream doesn't fall off with distance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	TObjectPtr<USoundAttenuation> Attenuation = nullptr;

	// Starts playing a new stream, closing any open one first. With no audio device (e.g. -nosound) the stream still opens, and is raytraced but not heard. Returns false if SampleRate is invalid or the sound couldn't be created
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Stream")
	bool OpenStream(EVAStreamFormat Format = EVAStreamFormat::Mono16, int32 SampleRate = 48000);

	// Queues a chunk of raw PCM audio in the format given to OpenStream. Data pushed before this source is first ready to play (see IsReadyToPlay) is dropped, so the stream never plays unmuffled or without reverb. Game thread only
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Stream")
	void PushAudioData(const TArray<uint8>& Data);

	void PushAudioData(const uint8* Data, int32 NumBytes);

	// Stops playback and releases the stream. Safe to call when no stream is open
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Stream")
	void CloseStream();

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Stream")
	bool IsStreamOpen() const { return bStreamOpen; }

	// Closes the stream too
	virtual void Stop() override;

	// Bytes accepted by PushAudioData since OpenStream, i.e. pushed after this source was first ready to play. Counted with no audio device too
	int64 GetAcceptedByteCount() const { return AcceptedByteCount; }

	// Bytes dropped by PushAudioData since OpenStream, because this source wasn't ready to play yet
	int64 GetDroppedByteCount() const { return DroppedByteCount; }

private:
	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> StreamWave = nullptr;

	EVAStreamFormat StreamFormat = EVAStreamFormat::Mono16;

	bool bStreamOpen = false;

	// Latched once this source is first ready to play, so a bRaytraceOnce stream keeps accepting data after its emitter leaves the world
	bool bStreamReady = false;

	int64 AcceptedByteCount = 0;
	int64 DroppedByteCount = 0;

	// 8-bit data converted to the procedural wave's 16-bit samples
	TArray<int16> ConvertedSamples;
};
