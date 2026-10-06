#include "VARaytracedSource.h"

extern "C" {
#include "vaudio.h"
}

AVARaytracedSource::AVARaytracedSource()
{
	// Godot's VARaytracedSource default
	bAffectsGroupedEAX = true;
}

void AVARaytracedSource::DeinitializeTypeSpecific()
{
	Playback.Stop();

	Super::DeinitializeTypeSpecific();
}

void AVARaytracedSource::TickTypeSpecific(float DeltaTime)
{
	Super::TickTypeSpecific(DeltaTime);

	Playback.RemoveFinished();
	UpdatePlayback();
	Playback.SetLocation(GetActorLocation());
}

void AVARaytracedSource::UpdatePlayback()
{
	Playback.SetVolumeMultiplier(VolumeMultiplier);
	Playback.SetPitchMultiplier(PitchMultiplier);

	// Null until the main listener has raytraced this source, and after a bRaytraceOnce source leaves the world. The last result is kept
	if (VALowPassFilter* lowPassFilter = GetMufflingResult())
		Playback.SetFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);

	USoundSubmix* submix = nullptr;
	float sendLevel = 0.0f;

	// Set after the filter, as the send level is compensated by gainLF
	if (ResolveReverbSend(submix, sendLevel))
		Playback.SetReverbSend(submix, sendLevel);
}

void AVARaytracedSource::Stop()
{
	Playback.Stop();
}
