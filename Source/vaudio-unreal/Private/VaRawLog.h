#pragma once

#include <cstdio>
#include <cstdarg>

#include "CoreMinimal.h"

inline void VARawLog(const wchar_t* Fmt, ...)
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

inline const wchar_t* VaFileNameOnly(const wchar_t* Path)
{
	const wchar_t* Slash = wcsrchr(Path, L'\\');
	const wchar_t* ForwardSlash = wcsrchr(Path, L'/');
	if (ForwardSlash && (!Slash || ForwardSlash > Slash))
		Slash = ForwardSlash;
	return Slash ? Slash + 1 : Path;
}

#define VALog(Message, ...) VARawLog(L"%s: %hs(): %s: " Message, VaFileNameOnly(__FILEW__), __FUNCTION__, *GetActorNameOrLabel(), ##__VA_ARGS__)
#define VALogObj(Message, ...) VARawLog(L"%s: %hs(): %s: " Message, VaFileNameOnly(__FILEW__), __FUNCTION__, *GetName(), ##__VA_ARGS__)

// This is the callback passed to each VAEmitter and VAWorld
inline void VASdkLogCallback(const char* Message)
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
