#pragma once

#include <cstdio>
#include <cstdarg>

inline void VaRawLog(const wchar_t* Fmt, ...)
{
	FILE* F = nullptr;
	_wfopen_s(&F, L"C:\\vaudio_debug.log", L"a, ccs=UTF-8");
	if (!F) return;

	va_list Args;
	va_start(Args, Fmt);
	vfwprintf(F, Fmt, Args);
	va_end(Args);

	fwprintf(F, L"\n");
	fclose(F);
}

// Matches VALogCallback = void(*)(const char*). Pass directly to vaWorldSetLogCallback,
// vaEmitterSetLogCallback, vaEmitterSetLogErrorCallback etc so SDK-internal diagnostics
// land in the same raw log file.
inline void VaSdkLogCallback(const char* Message)
{
	FILE* F = nullptr;
	_wfopen_s(&F, L"C:\\vaudio_debug.log", L"a, ccs=UTF-8");
	if (!F) return;

	fwprintf(F, L"[VA SDK] %hs\n", Message ? Message : "(null)");
	fclose(F);
}
