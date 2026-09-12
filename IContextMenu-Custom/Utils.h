#pragma once
#include <windows.h>
#include <string>
#include <vector>

std::wstring GetModuleDirectory(HMODULE hModule);
std::wstring Trim(const std::wstring& s);
std::wstring ToLowerCopy(const std::wstring& s);
std::wstring GetExtensionLower(const std::wstring& path);
bool ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to);
std::string  WideToUtf8(const std::wstring& w);
std::wstring ReadTextFileUtf8(const std::wstring& path);
std::vector<std::wstring> SplitLines(const std::wstring& text);

// アイコンファイル(exe/dll/ico等)から、メニュー用の小アイコンサイズの
// 32bpp ARGB ビットマップを作る。iconPath は環境変数展開前のパスでよい。
// 読み込めなければ nullptr。呼び出し側が DeleteObject() で破棄する責任を持つ。
// iconPath が空文字列なら何もせず nullptr を返す。
HBITMAP LoadIconAsMenuBitmap(const std::wstring& iconPath, int iconIndex);
