#pragma once

#include <cstdio>
#include <cstdarg>

#include "CoreMinimal.h"

inline void VaRawLog(const wchar_t* Fmt, ...)
{
	va_list Args;
	va_start(Args, Fmt);

	wchar_t Buffer[1024];
	_vsnwprintf_s(Buffer, _TRUNCATE, Fmt, Args);
	va_end(Args);

	FILE* F = nullptr;
	_wfopen_s(&F, L"C:\\vaudio_debug.log", L"a, ccs=UTF-8");
	if (F)
	{
		fwprintf(F, L"%s\n", Buffer);
		fclose(F);
	}

	UE_LOG(LogTemp, Warning, TEXT("%s"), Buffer);
}

// Strips the directory from a __FILEW__ expansion (which is a full path, not just the file name).
inline const wchar_t* VaFileNameOnly(const wchar_t* Path)
{
	const wchar_t* Slash = wcsrchr(Path, L'\\');
	const wchar_t* ForwardSlash = wcsrchr(Path, L'/');
	if (ForwardSlash && (!Slash || ForwardSlash > Slash))
		Slash = ForwardSlash;
	return Slash ? Slash + 1 : Path;
}

// Logs "fileName: functionName(): actorName: message", prepending call-site context via
// __FILEW__/__FUNCTION__ (C++ has no runtime reflection for this - these are compile-time
// macros expanded by the preprocessor at the call site, e.g. in VAudioEmitter.cpp, not here -
// so the file/function reported is always the caller's, never VaRawLog.h). Must be a macro
// rather than a function since __FUNCTION__ needs to expand at the call site, and
// GetActorNameOrLabel() must resolve against the caller's `this` (an AActor). Uses the actor
// label (e.g. "Footsteps_1") instead of GetName()'s internal object name (e.g.
// "VAudioEmitter_UAID_...") so log output matches what's shown in the World Outliner.
#define VALog(Message, ...) VaRawLog(L"%s: %hs(): %s: " Message, VaFileNameOnly(__FILEW__), __FUNCTION__, *GetActorNameOrLabel(), ##__VA_ARGS__)

// Matches VALogCallback = void(*)(const char*). Pass directly to vaWorldSetLogCallback,
// vaEmitterSetLogCallback, vaEmitterSetLogErrorCallback etc so SDK-internal diagnostics
// land in the same raw log file.
inline void VaSdkLogCallback(const char* Message)
{
	FILE* F = nullptr;
	_wfopen_s(&F, L"C:\\vaudio_debug.log", L"a, ccs=UTF-8");
	if (F)
	{
		fwprintf(F, L"[VA SDK] %hs\n", Message ? Message : "(null)");
		fclose(F);
	}

	UE_LOG(LogTemp, Warning, TEXT("[VA SDK] %hs"), Message ? Message : "(null)");
}
