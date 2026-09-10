#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <vector>
#include <string>

#pragma comment(lib, "shlwapi.lib")

#define FOO_CLSID_STR "FF111111-2222-3333-4444-555555555555"
class __declspec(uuid(FOO_CLSID_STR)) CFooContextMenuDummy;
const CLSID CLSID_FooContextMenu = __uuidof(CFooContextMenuDummy);
LONG g_cRefModule = 0; // ref count for dll
#define MY_MAX_PATH 3000
class FooContextMenu : public IContextMenu, public IShellExtInit {
private:
    ULONG m_cRef;
    std::vector<std::wstring> m_selectedFiles;
    wchar_t       _szTargetFolder[MY_MAX_PATH] = L"";

public:
    FooContextMenu() : m_cRef(1) { InterlockedIncrement(&g_cRefModule); }
    ~FooContextMenu() { InterlockedDecrement(&g_cRefModule); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        static const QITAB qit[] = {
            QITABENT(FooContextMenu, IContextMenu),
            QITABENT(FooContextMenu, IShellExtInit),
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
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder0, IDataObject* pdtobj, HKEY hkeyProgID) override {
        SHGetPathFromIDList(pidlFolder0, _szTargetFolder);
        if (!pdtobj) {
            return S_OK;
        };

        FORMATETC fmt = {
            static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_SHELLIDLIST)),
            NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL
        };
        STGMEDIUM medium;

        if (FAILED(pdtobj->GetData(&fmt, &medium))) return E_FAIL;

        LPIDA pida = static_cast<LPIDA>(GlobalLock(medium.hGlobal));
        if (!pida) {
            ReleaseStgMedium(&medium);
            return E_FAIL;
        }

        LPCITEMIDLIST pidlFolder = reinterpret_cast<LPCITEMIDLIST>(
            reinterpret_cast<LPBYTE>(pida) + pida->aoffset[0]);

        bool hasFoo = false;
        m_selectedFiles.clear();

        for (UINT i = 0; i < pida->cidl; ++i) {
            LPCITEMIDLIST pidlRel = reinterpret_cast<LPCITEMIDLIST>(
                reinterpret_cast<LPBYTE>(pida) + pida->aoffset[i + 1]);

            PIDLIST_ABSOLUTE pidlFull = ILCombine(pidlFolder, pidlRel);
            if (!pidlFull) continue;

            IShellItem* psi = nullptr;
            if (SUCCEEDED(SHCreateItemFromIDList(pidlFull, IID_PPV_ARGS(&psi)))) {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    m_selectedFiles.push_back(pszPath);

                    // check if the filename contain "foo"
                    PCWSTR fileName = PathFindFileNameW(pszPath);
                    if (StrStrI(fileName, L"foo") != NULL) {
                        hasFoo = true;
                    }

                    CoTaskMemFree(pszPath);
                }
                psi->Release();
            }

            ILFree(pidlFull);
        }

        GlobalUnlock(medium.hGlobal);
        ReleaseStgMedium(&medium);

        return hasFoo ? S_OK : E_FAIL;
    }

    // IContextMenu
    IFACEMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override {
        if (uFlags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

        // add menu
        InsertMenuW(hmenu, indexMenu, MF_BYPOSITION | MF_STRING, idCmdFirst, L"Foo Context Menu");
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1); // added 1 command
    }

    IFACEMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO pici) override {
        if (HIWORD(pici->lpVerb) != 0) return E_FAIL;
        if (LOWORD(pici->lpVerb) == 0) {
            if (m_selectedFiles.size() == 0) {
                std::wstring message = L"background dir path:\n\n";
                message = message + +_szTargetFolder;
                MessageBoxW(pici->hwnd, message.c_str(), L"Foo Context Menu Dialog", MB_OK | MB_ICONINFORMATION);
            }
            else {
                std::wstring message = L"list of selected files:\n\n";
                for (const auto& file : m_selectedFiles) {
                    message += file + L"\n";
                }
                MessageBoxW(pici->hwnd, message.c_str(), L"Foo Context Menu Dialog", MB_OK | MB_ICONINFORMATION);
            }
            return S_OK;
        }
        return E_FAIL;
    }

    IFACEMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType, UINT* pReserved, LPSTR pszName, UINT cchMax) override {
        return E_NOTIMPL;
    }
};


class FooClassFactory : public IClassFactory {
private:
    ULONG m_cRef;
public:
    FooClassFactory() : m_cRef(1) {}
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
        FooContextMenu* pExt = new FooContextMenu();
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

_Check_return_
STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID FAR* ppv) {
    if (IsEqualCLSID(rclsid, CLSID_FooContextMenu)) {
        FooClassFactory* pFactory = new FooClassFactory();
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