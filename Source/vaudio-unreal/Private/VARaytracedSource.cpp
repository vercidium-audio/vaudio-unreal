#include "VARaytracedSource.h"

extern "C" {
#include "vaudio.h"
}

AVARaytracedSource::AVARaytracedSource()
{
	// Default value
	bAffectsGroupedEAX = true;
	bClampPosition = false;
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

	ApplyResultsToPlayback(Playback);
}

void AVARaytracedSource::Stop()
{
	Playback.Stop();
}
