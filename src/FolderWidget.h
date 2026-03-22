#pragma once

// Include Windows headers WITHOUT WIN32_LEAN_AND_MEAN for GDI+ compatibility
#include <windows.h>
#include <objidl.h>      // Required for IStream
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

// Forward declarations
class FolderWidget;

// Item in a folder (renamed to avoid conflict with Windows SDK)
struct WidgetItem {
    std::wstring name;
    std::wstring path;
    std::wstring icon;
};

// Folder data structure
struct FolderData {
    std::wstring id;
    std::wstring name;
    COLORREF color;
    int posX;
    int posY;
    int iconSize;
    int paneWidth;
    int paneHeight;
    std::vector<WidgetItem> items;
    bool isExpanded;
};

// Color utility
inline COLORREF HexToColorRef(const std::string& hex) {
    if (hex.length() < 7) return RGB(59, 130, 246); // Default blue
    int r = std::stoi(hex.substr(1, 2), nullptr, 16);
    int g = std::stoi(hex.substr(3, 2), nullptr, 16);
    int b = std::stoi(hex.substr(5, 2), nullptr, 16);
    return RGB(r, g, b);
}

// Main widget class
class FolderWidget {
public:
    FolderWidget(const FolderData& data);
    ~FolderWidget();

    bool Create(HINSTANCE hInstance);
    void Show();
    void Hide();
    void UpdatePosition(int x, int y);
    void SetExpanded(bool expanded);
    void ShowContextMenu(int x, int y, int itemIndex = -1);
    
    // Hit test helper
    int GetItemIndexAt(int x, int y);
    bool IsExpanded() const { return m_data.isExpanded; }
    
    void AddItem(const WidgetItem& item);
    void RemoveItem(const std::wstring& path);
    void LaunchItem(const std::wstring& path);
    
    void SetColor(COLORREF color);
    void SetName(const std::wstring& name);
    void SetIconSize(int iconSize);
    void SetPaneSize(int paneWidth, int paneHeight);
    
    const FolderData& GetData() const { return m_data; }
    HWND GetHwnd() const { return m_hwnd; }

    // Static window procedure
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void Render();
    void RenderFolderIcon(Gdiplus::Graphics& g, int x, int y);
    void RenderExpandedPane(Gdiplus::Graphics& g, int x, int y);
    void RenderItem(Gdiplus::Graphics& g, const WidgetItem& item, int x, int y, int width, int height);
    void ShowContextMenu(int x, int y);
    
    void OnPaint();
    void OnMouseDown(int x, int y, bool rightClick);
    void OnMouseUp(int x, int y);
    void OnMouseMove(int x, int y);
    void OnDragEnter();
    void OnDragLeave();
    void OnMouseWheel(int delta);
    void OnDrop(const std::vector<std::wstring>& files);
    RECT GetPaneRect() const;
    int HitTestResizeHandle(int x, int y) const;
    int GetIconSize() const;
    int GetCollapsedWidth() const;
    int GetCollapsedHeight() const;
    int GetExpandedPaneWidth() const;
    int GetExpandedPaneHeight() const;
    int GetExpandedWidth() const;
    int GetExpandedHeight() const;
    int GetItemColumns() const;
    int GetItemSlotWidth() const;
    int GetItemRowHeight() const;
    int GetItemCardWidth() const;
    int GetItemCardHeight() const;

    HWND m_hwnd;
    HINSTANCE m_hInstance;
    FolderData m_data;
    
    bool m_isDragging;
    POINT m_dragStart;
    POINT m_dragStartScreen;
    POINT m_dragOriginPos;
    bool m_dragMoved;
    bool m_isResizing;
    POINT m_resizeStartScreen;
    int m_resizeStartPaneWidth;
    int m_resizeStartPaneHeight;
    int m_resizeEdges;
    bool m_isDragOver;
    int m_hoveredItemIndex;
    
    // Window dimensions
    static const int ICON_SIZE = 64;
    static const int ICON_PADDING = 16;

private:
    Gdiplus::Bitmap* GetIconForFile(const std::wstring& path);
    std::map<std::wstring, Gdiplus::Bitmap*> m_iconCache;
    
    // Launch animation
    int m_clickedItemIndex;
    bool m_isLaunchAnimating;

    // Scroll support
    int m_scrollOffset;
    
    // Smart positioning
    bool m_expandLeft;
};

// Widget manager
class WidgetManager {
public:
    static WidgetManager& Instance();
    
    void Initialize(HINSTANCE hInstance);
    void Shutdown();
    
    FolderWidget* CreateFolder(const FolderData& data);
    void RemoveFolder(const std::wstring& id);
    FolderWidget* GetFolder(const std::wstring& id);
    
    void LoadFromConfig();
    void SaveToConfig();
    
    void CreateSystemTray();
    void RemoveSystemTray();
    void ShowTrayMenu();
    void ShowTrayMenu(POINT anchor);
    
    const std::vector<std::unique_ptr<FolderWidget>>& GetFolders() const { return m_folders; }

private:
    WidgetManager() = default;
    
    // Startup and config helpers
    void AddToStartup();
    void RemoveFromStartup();
    bool IsInStartup();
    std::wstring GetConfigPath();
    void EnsureConfigDirectory();
    
    HINSTANCE m_hInstance;
    std::vector<std::unique_ptr<FolderWidget>> m_folders;
    NOTIFYICONDATAW m_trayIcon;
    HWND m_trayHwnd;
};
