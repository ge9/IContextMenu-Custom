#pragma once
#include <windows.h>

#define MY_MAX_PATH 3000

extern HMODULE g_hModule;
extern LONG    g_cRefModule; // ref count for dll

extern const wchar_t* const kHandlerName;
