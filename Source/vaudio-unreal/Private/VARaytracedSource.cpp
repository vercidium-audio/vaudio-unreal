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
	// Set every tick, as Blueprints and C++ write these UPROPERTYs directly, and IsReverbOnly also follows the VAWorld's setting. Each setter returns early if the value is the same
	Playback.SetVolumeMultiplier(VolumeMultiplier);
	Playback.SetPitchMultiplier(PitchMultiplier);
	Playback.SetDryOutputEnabled(!AVAWorld::IsReverbOnly(GetAudioWorld(), bReverbOnly));

	ApplyResultsToPlayback(Playback);
}

void AVARaytracedSource::Stop()
{
	Playback.Stop();
}
