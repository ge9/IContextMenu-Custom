#include "Config.h"
#include "Utils.h"
#include "Globals.h"
#include <algorithm>
#include <map>
#include <shlwapi.h>
#include <knownfolders.h>
#include <shlobj.h>

bool ParseClassName(const std::wstring& raw, MenuClass& outClass, std::wstring& outExtension)
{
    std::wstring c = Trim(raw);
    if (c.empty()) return false;

    if (c[0] == L'.')
    {
        outClass = MenuClass::Extension;
        outExtension = ToLowerCopy(c);
        return true;
    }
    if (c == L"*") { outClass = MenuClass::AnyFile; return true; }
    if (_wcsicmp(c.c_str(), L"AllFileSystemObjects") == 0) { outClass = MenuClass::AllFileSystemObjects; return true; }
    if (_wcsicmp(c.c_str(), L"Directory") == 0) { outClass = MenuClass::Directory; return true; }
    if (_wcsicmp(c.c_str(), L"Drive") == 0) { outClass = MenuClass::Drive; return true; }
    if (_wcsicmp(c.c_str(), L"Background") == 0) { outClass = MenuClass::Background; return true; }
    if (_wcsicmp(c.c_str(), L"DesktopBackground") == 0) { outClass = MenuClass::DesktopBackground; return true; }
    if (_wcsicmp(c.c_str(), L"DragDrop") == 0) { outClass = MenuClass::DragDrop; return true; }
    return false;
}

static void ParseIconSpec(const std::wstring& spec, std::wstring& path, int& index)
{
    index = 0;
    path.clear();
    if (spec.empty()) return;
    size_t comma = spec.find_last_of(L',');
    if (comma == std::wstring::npos) { path = spec; return; }
    path = spec.substr(0, comma);
    std::wstring idxStr = Trim(spec.substr(comma + 1));
    try { index = std::stoi(idxStr); }
    catch (...) { index = 0; }
}
static int ParseGroupPosition(const std::wstring& raw)
{
    std::wstring t = Trim(raw);
    bool neg = !t.empty() && t[0] == L'-';
    if (neg) t = t.substr(1);

    int v = 0;
    try { v = t.empty() ? 0 : std::stoi(t); }
    catch (...) { v = 0; }

    return neg ? -(v + 1) : v; // subtract 1 from negative indexes (like "-0")
}
ConfigStore::ConfigStore()
{
    InitializeCriticalSection(&m_cs);
    m_configPath = GetModuleDirectory(g_hModule) + L"\\contextmenu.ini";
}

ConfigStore& ConfigStore::Instance()
{
    static ConfigStore instance;
    return instance;
}

void ConfigStore::EnsureLoaded()
{
    if (m_everLoaded && !m_forceReloadEveryTime) return;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    bool haveStat = GetFileAttributesExW(m_configPath.c_str(), GetFileExInfoStandard, &fad) != 0;

    EnterCriticalSection(&m_cs);
    bool needLoad = !m_everLoaded; // 初回は無条件で読む
    if (m_everLoaded && m_forceReloadEveryTime && haveStat)
    {
        if (CompareFileTime(&fad.ftLastWriteTime, &m_lastLoadedWriteTime) != 0)
            needLoad = true;
    }
    if (needLoad)
    {
        LoadNow();
        if (haveStat) m_lastLoadedWriteTime = fad.ftLastWriteTime;
        m_everLoaded = true;
    }
    LeaveCriticalSection(&m_cs);
}

void ConfigStore::LoadNow()
{
    m_items.clear();
    m_placeholders = PlaceholderTokens{}; // デフォルトに戻す
    m_forceReloadEveryTime = false;       // デフォルトに戻す（下でnopersistent=trueなら立て直す）

    std::wstring text = ReadTextFileUtf8(m_configPath);
    if (text.empty()) return; // 設定ファイルが無い/空 → メニュー項目0件で正常動作

    std::vector<std::wstring> lines = SplitLines(text);

    bool inItem = false;
    std::map<std::wstring, std::wstring> globalKv;
    std::map<std::wstring, std::wstring> kv;
    int autoIndex = 0;

    auto flushItem = [&]()
        {
            if (!inItem) return;

            MenuClass cls{};
            std::wstring ext;
            auto itClass = kv.find(L"class");
            if (itClass == kv.end())
            {
                kv.clear();
                return; // class未指定 or 認識できない値の項目は無視
            }
            std::vector<std::pair<MenuClass, std::wstring>> classes;
            {
                const std::wstring& raw = itClass->second;
                size_t pos = 0;
                while (pos < raw.size())
                {
                    size_t start = raw.find_first_not_of(L" \t", pos);
                    if (start == std::wstring::npos) break;
                    size_t end = raw.find_first_of(L" \t", start);
                    std::wstring token = (end == std::wstring::npos) ? raw.substr(start) : raw.substr(start, end - start);
                    pos = (end == std::wstring::npos) ? raw.size() : end;

                    MenuClass c{};
                    std::wstring ext;
                    if (ParseClassName(token, c, ext))
                        classes.emplace_back(c, ext);
                }
            }
            if (classes.empty())
            {
                kv.clear();
                return; // 認識できるclassが1つも無ければ無視
            }
            MenuItemConfig item;
            item.classes = std::move(classes);
            item.title = kv.count(L"title") ? kv[L"title"] : L"(no title)";
            item.command = kv.count(L"command") ? kv[L"command"] : L"";
            item.extendedOnly = kv.count(L"extended") && kv[L"extended"] == L"1";
            item.separatorBefore = kv.count(L"separator_before") && kv[L"separator_before"] == L"1";
            item.separatorAfter = kv.count(L"separator_after") && kv[L"separator_after"] == L"1";
            item.verb = kv.count(L"verb") ? kv[L"verb"] : (L"item" + std::to_wstring(autoIndex));
            if (kv.count(L"position"))
            {
                item.position.isDefault = false;
                const std::wstring& pv = kv[L"position"];
                size_t colon = pv.find(L':');
                if (colon == std::wstring::npos)
                {
                    item.position.group = ParseGroupPosition(pv);
                    item.position.index = -1; // default = end
                }
                else
                {
                    item.position.group = ParseGroupPosition(pv.substr(0, colon));
                    item.position.index = ParseGroupPosition(pv.substr(colon + 1));
                }
            }
            if (kv.count(L"icon"))
                ParseIconSpec(kv[L"icon"], item.iconPath, item.iconIndex);

            m_items.push_back(item);
            ++autoIndex;
            kv.clear();
        };

    for (const auto& rawLine : lines)
    {
        std::wstring line = Trim(rawLine);
        if (line.empty() || line[0] == L';' || line[0] == L'#')
            continue;

        if (line.front() == L'[')
        {
            flushItem();
            inItem = true;
            continue;
        }

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;

        std::wstring key = ToLowerCopy(Trim(line.substr(0, eq)));
        std::wstring val = Trim(line.substr(eq + 1));

        if (!inItem)
            globalKv[key] = val; // 最初の[item]より前 = グローバル設定
        else
            kv[key] = val;
    }
    flushItem();

    if (globalKv.count(L"placeholder_files")) m_placeholders.files = globalKv[L"placeholder_files"];
    if (globalKv.count(L"placeholder_file"))  m_placeholders.file = globalKv[L"placeholder_file"];
    if (globalKv.count(L"placeholder_dir"))   m_placeholders.dir = globalKv[L"placeholder_dir"];
    if (globalKv.count(L"nopersistent"))
        m_forceReloadEveryTime = (globalKv[L"nopersistent"] == L"true" || globalKv[L"nopersistent"] == L"1");
}

std::vector<MenuItemConfig> ConfigStore::GetAllItems() const
{
    EnterCriticalSection(&m_cs);
    std::vector<MenuItemConfig> result = m_items;
    LeaveCriticalSection(&m_cs);
    return result;
}

PlaceholderTokens ConfigStore::GetPlaceholderTokens() const
{
    EnterCriticalSection(&m_cs);
    PlaceholderTokens result = m_placeholders;
    LeaveCriticalSection(&m_cs);
    return result;
}
