#pragma once
#include <windows.h>
#include <string>
#include <vector>

// 設定ファイルの class= に書ける値。
// レジストリの登録場所ではなく、「実際に選択された対象がどんなものか」に対する
// 絞り込み条件として扱う（Background/DesktopBackground を除く）。
enum class MenuClass
{
    AnyFile,               // "*"                     選択したアイテムが通常ファイル
    AllFileSystemObjects,  // "AllFileSystemObjects"  選択があれば常にマッチ(ファイル・フォルダ・ドライブ問わず)
    Directory,             // "Directory"             選択したアイテムがディレクトリ
    Drive,                 // "Drive"                 選択したアイテムがドライブルート("C:\"等)
    Background,            // "Background"            フォルダウィンドウの背景（デスクトップ以外）
    DesktopBackground,     // "DesktopBackground"     デスクトップの背景
    Extension,              // ".xxx"                 選択したアイテムがその拡張子
    DragDrop                // "DragDrop"             右ドラッグ&ドロップのポップアップメニュー
};

struct PositionSpec
{
    bool isDefault = true;
    int  group = 0;
    int  index = 0;
};
struct MenuItemConfig
{
    std::vector<std::pair<MenuClass, std::wstring>> classes;

    std::wstring verb;             // 内部識別用のverb名（省略時は自動採番）
    std::wstring title;            // メニューに表示する文字列
    std::wstring command;          // コマンドライン。プレースホルダーを置換する
    std::wstring iconPath;         // アイコンのパス（環境変数展開前）
    int          iconIndex = 0;
    bool         extendedOnly = false;   // Shiftキー押下時のみ表示 (CMF_EXTENDEDVERBS)
    bool         separatorBefore = false;
    bool         separatorAfter = false;
    PositionSpec position;
};
struct PlaceholderTokens
{
    std::wstring files = L"{files}";
    std::wstring file = L"{file}";
    std::wstring dir = L"{dir}";
};

bool ParseClassName(const std::wstring& raw, MenuClass& outClass, std::wstring& outExtension);

class ConfigStore
{
public:
    static ConfigStore& Instance();
    void EnsureLoaded();
    std::vector<MenuItemConfig> GetAllItems() const;

    PlaceholderTokens GetPlaceholderTokens() const;

    std::wstring GetConfigPath() const { return m_configPath; }

private:
    ConfigStore();
    void LoadNow();

    mutable CRITICAL_SECTION m_cs;
    std::wstring m_configPath;
    FILETIME     m_lastLoadedWriteTime{};
    bool         m_everLoaded = false;
    bool         m_forceReloadEveryTime = false; // nopersistent=true が最後に読んだ内容にあったかどうか

    std::vector<MenuItemConfig> m_items;
    PlaceholderTokens m_placeholders;
};
