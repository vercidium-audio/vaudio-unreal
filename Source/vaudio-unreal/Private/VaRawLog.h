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
