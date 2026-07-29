#include "VAudioSubmixEffectDirectionalPan.h"

FSubmixEffectDirectionalPan::FSubmixEffectDirectionalPan()
{
}

void FSubmixEffectDirectionalPan::Init(const FSoundEffectSubmixInitData& InitData)
{
	RenderThreadPan = 0.0f;
}

void FSubmixEffectDirectionalPan::OnPresetChanged()
{
	GET_EFFECT_SETTINGS(SubmixEffectDirectionalPan);
	PendingSettings.SetParams(Settings);
}

void FSubmixEffectDirectionalPan::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
	FSubmixEffectDirectionalPanSettings NewSettings;
	if (PendingSettings.GetParams(&NewSettings))
	{
		RenderThreadPan = FMath::Clamp(NewSettings.Pan, -1.0f, 1.0f);
	}

	Audio::FAlignedFloatBuffer& InAudioBuffer = *InData.AudioBuffer;
	Audio::FAlignedFloatBuffer& OutAudioBuffer = *OutData.AudioBuffer;

	// Equal-power pan law: left/right gains trace a quarter-circle so L^2 + R^2 stays constant as
	// pan sweeps from -1 to 1, avoiding the perceived volume dip a linear crossfade would cause.
	const float panAngle = (RenderThreadPan + 1.0f) * (PI / 4.0f);
	const float leftGain = FMath::Cos(panAngle);
	const float rightGain = FMath::Sin(panAngle);

	if (InData.NumChannels == 2)
	{
		for (int32 frameIndex = 0; frameIndex < InData.NumFrames; ++frameIndex)
		{
			const int32 sampleIndex = frameIndex * 2;
			OutAudioBuffer[sampleIndex] = InAudioBuffer[sampleIndex] * leftGain;
			OutAudioBuffer[sampleIndex + 1] = InAudioBuffer[sampleIndex + 1] * rightGain;
		}
	}
	else if (InData.NumChannels == 1)
	{
		// Mono submix: split the single channel out to a virtual left/right using the same gains,
		// then sum back down so downstream channel count is unaffected.
		for (int32 frameIndex = 0; frameIndex < InData.NumFrames; ++frameIndex)
		{
			const float inputSample = InAudioBuffer[frameIndex];
			OutAudioBuffer[frameIndex] = inputSample * leftGain + inputSample * rightGain;
		}
	}
	else
	{
		// Unknown channel layout - pass through unchanged rather than guessing at a layout.
		for (int32 sampleIndex = 0; sampleIndex < InAudioBuffer.Num(); ++sampleIndex)
		{
			OutAudioBuffer[sampleIndex] = InAudioBuffer[sampleIndex];
		}
	}
}

void USubmixEffectDirectionalPanPreset::SetSettings(const FSubmixEffectDirectionalPanSettings& InSettings)
{
	UpdateSettings(InSettings);
}

void USubmixEffectDirectionalPanPreset::SetPan(float NewPan)
{
	FSubmixEffectDirectionalPanSettings NewSettings = Settings;
	NewSettings.Pan = NewPan;
	SetSettings(NewSettings);
}
