#include "Utils.h"
#include <fstream>
#include <cwctype>
#include <shellapi.h>

std::wstring GetModuleDirectory(HMODULE hModule)
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(hModule, path, MAX_PATH);
    std::wstring s(path);
    size_t pos = s.find_last_of(L'\\');
    return (pos == std::wstring::npos) ? std::wstring() : s.substr(0, pos);
}

std::wstring Trim(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring ToLowerCopy(const std::wstring& s)
{
    std::wstring r = s;
    for (auto& c : r) c = (wchar_t)towlower(c);
    return r;
}

std::wstring GetExtensionLower(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return L"";
    return ToLowerCopy(path.substr(dot));
}

bool ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to)
{
    if (from.empty()) return false;
    bool any = false;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos)
    {
        s.replace(pos, from.length(), to);
        pos += to.length();
        any = true;
    }
    return any;
}

std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring ReadTextFileUtf8(const std::wstring& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return L"";
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // UTF-8 BOM ‚ª‚ ‚ê‚Î”‚ª‚·
    if (bytes.size() >= 3 &&
        (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB &&
        (unsigned char)bytes[2] == 0xBF)
    {
        bytes.erase(0, 3);
    }
    if (bytes.empty()) return L"";

    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), w.data(), n);
    return w;
}

std::vector<std::wstring> SplitLines(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        if (i == text.size() || text[i] == L'\n')
        {
            std::wstring line = text.substr(start, i - start);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            lines.push_back(line);
            start = i + 1;
        }
    }
    return lines;
}

static HBITMAP IconToArgbBitmap(HICON hIcon, int size)
{
    if (!hIcon) return nullptr;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);
    if (!hbmp) { DeleteDC(hdcMem); return nullptr; }

    HBITMAP hbmpOld = (HBITMAP)SelectObject(hdcMem, hbmp);
    if (bits) memset(bits, 0, (size_t)size * size * 4);
    DrawIconEx(hdcMem, 0, 0, hIcon, size, size, 0, nullptr, DI_NORMAL);
    SelectObject(hdcMem, hbmpOld);
    DeleteDC(hdcMem);
    return hbmp;
}

HBITMAP LoadIconAsMenuBitmap(const std::wstring& iconPath, int iconIndex)
{
    if (iconPath.empty()) return nullptr;

    wchar_t expanded[MAX_PATH]{};
    ExpandEnvironmentStringsW(iconPath.c_str(), expanded, MAX_PATH);

    HICON hIcon = nullptr;
    ExtractIconExW(expanded, iconIndex, nullptr, &hIcon, 1);
    if (!hIcon) return nullptr;

    int size = GetSystemMetrics(SM_CXSMICON);
    HBITMAP hbmp = IconToArgbBitmap(hIcon, size);
    DestroyIcon(hIcon);
    return hbmp;
}
