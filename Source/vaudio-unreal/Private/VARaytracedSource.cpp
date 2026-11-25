// Human-created

#include "VARaytracedSource.h"
#include "VAWorld.h"

extern "C" {
#include "vaudio.h"
}

AVARaytracedSource::AVARaytracedSource()
{
	// Set defaults
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
	// TODO - why set every frame? Can we get a callback when these properties change, or just invoke these Playback.* methods when the properties change?
	Playback.SetVolumeMultiplier(VolumeMultiplier);
	Playback.SetPitchMultiplier(PitchMultiplier);
	Playback.SetDryOutputEnabled(!AVAWorld::IsReverbOnly(GetAudioWorld(), bReverbOnly));

	ApplyResultsToPlayback(Playback);
}

void AVARaytracedSource::Stop()
{
	Playback.Stop();
}
