#include "Config.h"
#include "Globals.h"
#include "Utils.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <vector>
#include <string>
#include <strsafe.h>
#include <algorithm>
#include <map>
#pragma comment(lib, "shlwapi.lib")

#define MYCONTEXT_CLSID_STR "FF111111-2222-3333-4444-666666666666"
class __declspec(uuid(MYCONTEXT_CLSID_STR)) CFooContextMenuDummy;
const CLSID CLSID_FooContextMenu = __uuidof(CFooContextMenuDummy);
LONG g_cRefModule = 0; 

// replace placeholders
static std::wstring BuildCommandLine(const std::wstring& tmpl,
    const std::vector<std::wstring>& files,
    const std::wstring& dir,
    const PlaceholderTokens& ph)
{
    std::wstring joined;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (i) joined += L' ';
        joined += L'"';
        joined += files[i];
        joined += L'"';
    }

    std::wstring result = tmpl;
    bool hadFilesPlaceholder = ReplaceAll(result, ph.files, joined);
    ReplaceAll(result, ph.dir, L'"' + dir + L'"');
    if (!files.empty())
        ReplaceAll(result, ph.file, L'"' + files[0] + L'"');

    return result;
}

static bool IsDirectoryPath(const std::wstring& p)
{
    DWORD attr = GetFileAttributesW(p.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool GetDesktopPath(std::wstring& out)
{
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path);
    bool ok = SUCCEEDED(hr);
    if (ok) out = path;
    if (path) CoTaskMemFree(path);
    return ok;
}

class MyContextMenu : public IContextMenu, public IShellExtInit {
private:
    ULONG m_cRef;
    std::vector<std::wstring> m_selectedFiles;
    wchar_t       _szTargetFolder[MY_MAX_PATH] = L"";
    std::vector<std::wstring> m_selectedPaths; // 選択されたファイル/フォルダの絶対パス（背景クリック時は空）
    std::wstring m_folderPath;                 // 右クリック時に表示中だったフォルダ（{dir} 用、背景クリック時に特に重要）
    bool m_isBackgroundInvoke = false;         // true = 選択なし（背景を右クリック）
    bool m_isDesktop = false;                  // true = m_folderPath がデスクトップそのもの

    std::vector<MenuItemConfig> m_activeItems; // 直近のQueryContextMenuで実際にメニューへ出した項目（idCmdの並びに対応）
    std::vector<HBITMAP> m_iconBitmaps;        // 作成したアイコン用ビットマップ（破棄用に保持）
    bool m_isDragDropTarget = false;
public:
    MyContextMenu() : m_cRef(1) { InterlockedIncrement(&g_cRefModule); }
    ~MyContextMenu() {
        for (HBITMAP hbmp : m_iconBitmaps)
            if (hbmp) DeleteObject(hbmp);
        InterlockedDecrement(&g_cRefModule); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        static const QITAB qit[] = {
            QITABENT(MyContextMenu, IContextMenu),
            QITABENT(MyContextMenu, IShellExtInit),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    // IShellExtInit
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* pdtobj, HKEY hkeyProgID) override {
        m_selectedPaths.clear();
        m_folderPath.clear();
        m_isDesktop = false;
        m_isDragDropTarget = (pdtobj != nullptr) && (pidlFolder != nullptr);
        if (pidlFolder)
        {
            IShellItem* psiFolder = nullptr;

            if (SUCCEEDED(SHCreateItemFromIDList(pidlFolder, IID_PPV_ARGS(&psiFolder))))
            {
                PWSTR pszFolderPath = nullptr;
                if (SUCCEEDED(psiFolder->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath)))
                    {
                        m_folderPath = pszFolderPath;
                        CoTaskMemFree(pszFolderPath);
                        std::wstring desktopPath;
                        if (GetDesktopPath(desktopPath) && _wcsicmp(desktopPath.c_str(), m_folderPath.c_str()) == 0) m_isDesktop = true;
                    }
                psiFolder->Release();
            }
        }

        // pdtobj is null for Background/DesktopBackground
        m_isBackgroundInvoke = (pdtobj == nullptr);
        if (!pdtobj)
            return S_OK;

        FORMATETC fmt = {
            static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_SHELLIDLIST)),
            nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL
        };
        STGMEDIUM medium{};

        if (FAILED(pdtobj->GetData(&fmt, &medium)))
            return S_OK; // 0 menus

        LPIDA pida = static_cast<LPIDA>(GlobalLock(medium.hGlobal));
        if (!pida)
        {
            ReleaseStgMedium(&medium);
            return S_OK;
        }

        LPCITEMIDLIST pidlSelFolder = reinterpret_cast<LPCITEMIDLIST>(
            reinterpret_cast<LPBYTE>(pida) + pida->aoffset[0]);

        for (UINT i = 0; i < pida->cidl; ++i)
        {
            LPCITEMIDLIST pidlRel = reinterpret_cast<LPCITEMIDLIST>(
                reinterpret_cast<LPBYTE>(pida) + pida->aoffset[i + 1]);

            PIDLIST_ABSOLUTE pidlFull = ILCombine(pidlSelFolder, pidlRel);
            if (!pidlFull) continue;

            IShellItem* psi = nullptr;
            if (SUCCEEDED(SHCreateItemFromIDList(pidlFull, IID_PPV_ARGS(&psi))))
            {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
                {
                    m_selectedPaths.push_back(pszPath);
                    CoTaskMemFree(pszPath);
                }
                psi->Release();
            }

            ILFree(pidlFull);
        }

        GlobalUnlock(medium.hGlobal);
        ReleaseStgMedium(&medium);

        return S_OK;
    }


    bool MatchesSingleClass(MenuClass cls, const std::wstring& ext) const
    {
        if (cls == MenuClass::DragDrop) return m_isDragDropTarget;
        if (m_isDragDropTarget) return false;

        if (cls == MenuClass::Background)
            return m_isBackgroundInvoke && !m_isDesktop;
        if (cls == MenuClass::DesktopBackground)
            return m_isBackgroundInvoke && m_isDesktop;

        if (m_isBackgroundInvoke || m_selectedPaths.empty())
            return false;

        const std::wstring& first = m_selectedPaths[0];

        switch (cls)
        {
        case MenuClass::AnyFile:
            return !IsDirectoryPath(first) && !PathIsRootW(first.c_str());
        case MenuClass::AllFileSystemObjects:
            return true;
        case MenuClass::Directory:
            return IsDirectoryPath(first);
        case MenuClass::Drive:
            return PathIsRootW(first.c_str());
        case MenuClass::Extension:
            return GetExtensionLower(first) == ext;
        default:
            return false;
        }
    }

    bool MatchesClass(const MenuItemConfig& item) const
    {
        for (const auto& c : item.classes)
            if (MatchesSingleClass(c.first, c.second))
                return true;
        return false;
    }
    struct MenuGroupRange { int start; int end; }; // [start, end) 区間、セパレータ自体は含まない

    // 現在のhMenuを、セパレータで区切られたグループの並びとして走査する。
    static std::vector<MenuGroupRange> ScanMenuGroups(HMENU hMenu)
    {
        std::vector<MenuGroupRange> groups;
        int count = GetMenuItemCount(hMenu);
        int groupStart = 0;
        for (int i = 0; i < count; ++i)
        {
            MENUITEMINFOW mii{ sizeof(mii) };
            mii.fMask = MIIM_FTYPE;
            GetMenuItemInfoW(hMenu, (UINT)i, TRUE, &mii);
            if (mii.fType & MFT_SEPARATOR)
            {
                groups.push_back({ groupStart, i });
                groupStart = i + 1;
            }
        }
        groups.push_back({ groupStart, count });
        return groups;
    }
    UINT InsertOneItem(HMENU hMenu, const MenuItemConfig& item, UINT pos, UINT& nextId)
    {
        UINT cursor = pos;
        if (item.separatorBefore)
            InsertMenuW(hMenu, cursor++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

        MENUITEMINFOW mii{ sizeof(mii) };
        mii.fMask = MIIM_STRING | MIIM_ID | MIIM_FTYPE;
        mii.fType = MFT_STRING;
        mii.wID = nextId;
        mii.dwTypeData = const_cast<LPWSTR>(item.title.c_str());

        HBITMAP hbmp = LoadIconAsMenuBitmap(item.iconPath, item.iconIndex);
        if (hbmp)
        {
            m_iconBitmaps.push_back(hbmp);
            mii.fMask |= MIIM_BITMAP;
            mii.hbmpItem = hbmp;
        }

        InsertMenuItemW(hMenu, cursor++, TRUE, &mii);

        if (item.separatorAfter)
            InsertMenuW(hMenu, cursor++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

        m_activeItems.push_back(item);
        ++nextId;
        return cursor - pos; // 消費した枠数
    }
    void InsertPositionedItems(HMENU hMenu, const std::vector<const MenuItemConfig*>& items, UINT& nextId)
    {
        std::vector<MenuGroupRange> groups = ScanMenuGroups(hMenu);
        int groupCount = (int)groups.size();

        std::map<int, std::vector<const MenuItemConfig*>> byGroup; // groupCount = 範囲外→絶対末尾
        for (const auto* item : items)
        {
            int g = ResolveGroupIndex(item->position.group, groupCount);
            if (g < 0 || g >= groupCount) g = groupCount;
            byGroup[g].push_back(item);
        }

        int shift = 0;
        for (auto& [g, groupItems] : byGroup)
        {
            int rangeStart, rangeEnd;
            if (g < groupCount) { rangeStart = groups[g].start + shift; rangeEnd = groups[g].end + shift; }
            else { rangeStart = rangeEnd = (int)GetMenuItemCount(hMenu); }

            int itemsInGroup = rangeEnd - rangeStart;

            std::stable_sort(groupItems.begin(), groupItems.end(), [&](const MenuItemConfig* a, const MenuItemConfig* b) {
                return ResolveInsertIndex(a->position.index, itemsInGroup) < ResolveInsertIndex(b->position.index, itemsInGroup);
                });

            int inserted = 0;
            for (const auto* item : groupItems)
            {
                int idx = ResolveInsertIndex(item->position.index, itemsInGroup);
                idx = std::clamp(idx, 0, itemsInGroup);
                UINT pos = (UINT)(rangeStart + idx + inserted);
                inserted += (int)InsertOneItem(hMenu, *item, pos, nextId);
            }
            shift += inserted;
        }
    }

    IFACEMETHODIMP QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override {
        {
            if (uFlags & CMF_DEFAULTONLY)
                return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

            ConfigStore::Instance().EnsureLoaded();
            std::vector<MenuItemConfig> allItems = ConfigStore::Instance().GetAllItems();

            m_activeItems.clear();
            for (HBITMAP hbmp : m_iconBitmaps) if (hbmp) DeleteObject(hbmp);
            m_iconBitmaps.clear();

            bool showExtended = (uFlags & CMF_EXTENDEDVERBS) != 0;
            UINT nextId = idCmdFirst;

            std::vector<const MenuItemConfig*> defaultItems, positionedItems;
            for (const auto& item : allItems)
            {
                if (!MatchesClass(item)) continue;
                if (item.extendedOnly && !showExtended) continue;
                (item.position.isDefault ? defaultItems : positionedItems).push_back(&item);
            }

            UINT cursor = indexMenu;
            for (const auto* item : defaultItems)
                cursor += InsertOneItem(hMenu, *item, cursor, nextId);

            InsertPositionedItems(hMenu, positionedItems, nextId);

            return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, (USHORT)(nextId - idCmdFirst));
        }
    }
    static int ResolveGroupIndex(int raw, int groupCount)
    {
        return raw >= 0 ? raw : (groupCount + raw);
    }

    static int ResolveInsertIndex(int raw, int itemsInGroup)
    {
        return raw >= 0 ? raw : (itemsInGroup + raw + 1);
    }
    IFACEMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO pici) override {
        int index = -1;
        if (IS_INTRESOURCE(pici->lpVerb))
        {
            index = LOWORD(pici->lpVerb);
        }
        else
        {
            std::string verbA = pici->lpVerb;
            for (size_t i = 0; i < m_activeItems.size(); ++i)
            {
                if (WideToUtf8(m_activeItems[i].verb) == verbA) { index = (int)i; break; }
            }
        }
        if (index < 0 || (size_t)index >= m_activeItems.size())
            return E_INVALIDARG;

        const MenuItemConfig& item = m_activeItems[index];
        if (item.command.empty())
            return E_FAIL;

        bool pipeMode = item.command.front() == L'|';

        PlaceholderTokens ph = ConfigStore::Instance().GetPlaceholderTokens();
        std::wstring cmdBody = pipeMode ? item.command.substr(1) : item.command;
        std::wstring cmdLine = BuildCommandLine(cmdBody, !m_selectedPaths.empty()? m_selectedPaths: std::vector<std::wstring>{ m_folderPath }, m_folderPath, ph);

        std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
        buf.push_back(L'\0');

        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        HANDLE hWrite = nullptr;

        if (pipeMode)
        {
            SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
            HANDLE hRead = nullptr;
            if (!CreatePipe(&hRead, &hWrite, &sa, 0))
                return HRESULT_FROM_WIN32(GetLastError());
            SetHandleInformation(hWrite, HANDLE_FLAG_INHERIT, 0); // 親側の書き込みハンドルは子に継承させない

            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = hRead;
            si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        }

        BOOL ok = CreateProcessW(
            nullptr, buf.data(), nullptr, nullptr, /*bInheritHandles*/ pipeMode ? TRUE : FALSE,
            CREATE_NEW_CONSOLE, nullptr, 
            //the max length of working directory is 259 (including the null terminator), not 260
            (m_folderPath.empty() || m_folderPath.size() + 1 >= MAX_PATH) ? nullptr : m_folderPath.c_str(), 
            &si, &pi);

        if (pipeMode)
        {
            if (si.hStdInput) CloseHandle(si.hStdInput); // 親はread側をもう使わない
            if (ok)
            {
                for (const auto& path : m_selectedPaths)
                {
                    std::string line = WideToUtf8(path) + "\n";
                    DWORD written = 0;
                    WriteFile(hWrite, line.data(), (DWORD)line.size(), &written, nullptr);
                }
            }
            CloseHandle(hWrite);
        }

        if (!ok)
            return HRESULT_FROM_WIN32(GetLastError());

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return S_OK;
    }

    IFACEMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType, UINT* pReserved, LPSTR pszName, UINT cchMax) override {
        if (idCmd >= m_activeItems.size())
            return E_INVALIDARG;

        const MenuItemConfig& item = m_activeItems[idCmd];

        if (uType == GCS_VERBW)
        {
            wcsncpy_s((wchar_t*)pszName, cchMax, item.verb.c_str(), _TRUNCATE);
            return S_OK;
        }
        if (uType == GCS_VERBA)
        {
            std::string a = WideToUtf8(item.verb);
            strncpy_s(pszName, cchMax, a.c_str(), _TRUNCATE);
            return S_OK;
        }
        return E_NOTIMPL;
    }
};


class MyClassFactory : public IClassFactory {
private:
    ULONG m_cRef;
public:
    MyClassFactory() : m_cRef(1) {}
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        MyContextMenu* pExt = new MyContextMenu();
        HRESULT hr = pExt->QueryInterface(riid, ppv);
        pExt->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) InterlockedIncrement(&g_cRefModule);
        else InterlockedDecrement(&g_cRefModule);
        return S_OK;
    }
};

HMODULE g_hModule = nullptr;
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

_Check_return_
STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID FAR* ppv) {
    if (IsEqualCLSID(rclsid, CLSID_FooContextMenu)) {
        MyClassFactory* pFactory = new MyClassFactory();
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
__control_entrypoint(DllExport)
STDAPI DllCanUnloadNow(void) {
    return (g_cRefModule == 0) ? S_OK : S_FALSE;
}
