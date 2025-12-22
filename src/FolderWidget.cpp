#include "FolderWidget.h"
#include "Config.h"
#include <algorithm>
#include <cmath>

// Helper InputBox implementation
static std::wstring g_InputBuffer;
static LRESULT CALLBACK InputBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            CreateWindowW(L"STATIC", L"Enter new name:", WS_VISIBLE | WS_CHILD, 10, 10, 200, 20, hwnd, NULL, NULL, NULL);
            CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 10, 35, 260, 25, hwnd, (HMENU)100, NULL, NULL);
            CreateWindowW(L"BUTTON", L"OK", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 100, 70, 80, 25, hwnd, (HMENU)1, NULL, NULL);
            
            // Set init text if provided via lParam (optional, skipping for simplicity or need global)
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == 1) {
                wchar_t buf[256];
                GetDlgItemTextW(hwnd, 100, buf, 256);
                g_InputBuffer = buf;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static bool ShowInputBox(HWND parent, std::wstring& outName, const std::wstring& currentName) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = InputBoxProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"InputBoxClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);
    
    g_InputBuffer = L"";
    
    RECT rc; GetWindowRect(parent, &rc);
    int x = rc.left + (rc.right - rc.left)/2 - 150;
    int y = rc.top + (rc.bottom - rc.top)/2 - 75;
    
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"InputBoxClass", L"Rename", 
        WS_VISIBLE | WS_SYSMENU | WS_CAPTION, x, y, 300, 150, parent, NULL, GetModuleHandle(NULL), NULL);
    
    // Set text
    SetDlgItemTextW(hwnd, 100, currentName.c_str());
        
    EnableWindow(parent, FALSE);
    
    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        if (!IsWindow(hwnd)) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    
    if (!g_InputBuffer.empty()) {
        outName = g_InputBuffer;
        return true;
    }
    return false;
}

using namespace Gdiplus;

// GDI+ token
ULONG_PTR g_gdiplusToken;

// Window class name
const wchar_t* WIDGET_CLASS = L"FolderWidgetClass";
const wchar_t* TRAY_CLASS = L"FolderWidgetTrayClass";

// Tray icon ID
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_NEW_FOLDER 1001
#define ID_TRAY_EXIT 1002
#define ID_TRAY_SHOW_ALL 1003

// ============================================
// FolderWidget Implementation
// ============================================

FolderWidget::FolderWidget(const FolderData& data)
    : m_hwnd(nullptr)
    , m_hInstance(nullptr)
    , m_data(data)
    , m_isDragging(false)
    , m_isDragOver(false)
    , m_hoveredItemIndex(-1)
    , m_clickedItemIndex(-1)
    , m_isLaunchAnimating(false)
    , m_scrollOffset(0)
    , m_expandLeft(false)
{
    m_data.isExpanded = false;
}

FolderWidget::~FolderWidget() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
    // Cleanup cached icons
    for (auto& pair : m_iconCache) {
        delete pair.second;
    }
    m_iconCache.clear();
}

int FolderWidget::GetItemIndexAt(int x, int y) {
    if (!m_data.isExpanded) return -1;
    
    int padding = 12;
    int paneX = m_expandLeft ? ICON_PADDING : (ICON_SIZE + ICON_PADDING * 2 + 8);
    int itemWidth = (EXPANDED_WIDTH - padding * 2) / ITEMS_PER_ROW;
    int itemHeight = ITEM_SIZE + 8;
    int visibleHeight = EXPANDED_HEIGHT - padding * 2;
    
    for (size_t i = 0; i < m_data.items.size(); i++) {
        int col = i % ITEMS_PER_ROW;
        int row = i / ITEMS_PER_ROW;
        
        int ix = paneX + padding + col * itemWidth;
        int iy = ICON_PADDING + padding + row * itemHeight - m_scrollOffset;
        
        if (iy + itemHeight < ICON_PADDING || iy > ICON_PADDING + EXPANDED_HEIGHT) continue;
        
        if (x >= ix && x < ix + itemWidth && y >= iy && y < iy + itemHeight) {
            return (int)i;
        }
    }
    return -1;
}

void FolderWidget::ShowContextMenu(int x, int y, int itemIndex) {
    HMENU hMenu = CreatePopupMenu();
    
    if (itemIndex != -1) {
        // Item specific menu
        AppendMenuW(hMenu, MF_STRING, 10, L"📂 Open File Location");
        AppendMenuW(hMenu, MF_STRING, 11, L"❌ Remove Item");
    } else {
        // Folder menu
        AppendMenuW(hMenu, MF_STRING, 1, L"✏️ Rename Folder");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, 2, L"❌ Delete Widget");
    }
    
    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
    
    if (cmd == 1) {
        // Rename Dialog
        std::wstring newName;
        if (ShowInputBox(m_hwnd, newName, m_data.name)) {
            SetName(newName);
            WidgetManager::Instance().SaveToConfig();
        }
    } else if (cmd == 2) {
        // Delete Widget
        WidgetManager::Instance().RemoveFolder(m_data.id);
    } else if (cmd == 10) {
        // Open Location
        if (itemIndex >= 0 && itemIndex < m_data.items.size()) {
            std::wstring path = m_data.items[itemIndex].path;
            std::wstring folder = path.substr(0, path.find_last_of(L"\\/"));
            ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    } else if (cmd == 11) {
        // Remove Item
        if (itemIndex >= 0 && itemIndex < m_data.items.size()) {
            m_data.items.erase(m_data.items.begin() + itemIndex);
            WidgetManager::Instance().SaveToConfig();
            Render();
        }
    }
}

bool FolderWidget::Create(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    
    // Register window class
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = FolderWidget::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WIDGET_CLASS;
    
    RegisterClassExW(&wc);
    
    // Calculate window size based on expanded state
    int width = m_data.isExpanded ? (ICON_SIZE + ICON_PADDING * 2 + EXPANDED_WIDTH) : (ICON_SIZE + ICON_PADDING * 2);
    int height = m_data.isExpanded ? EXPANDED_HEIGHT : (ICON_SIZE + ICON_PADDING * 2 + 15);
    
    // Find desktop WorkerW (the window behind desktop icons)
    HWND desktopWorker = nullptr;
    HWND progman = FindWindowW(L"Progman", nullptr);
    
    // Send magic message to spawn WorkerW
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    
    // Find the WorkerW window
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        HWND shellDefView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
        if (shellDefView) {
            // Found the right WorkerW, get the one behind it
            *reinterpret_cast<HWND*>(lParam) = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&desktopWorker));
    
    // Create layered window WITHOUT WS_EX_TOPMOST - it will sit on desktop level
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        WIDGET_CLASS,
        L"FolderWidget",
        WS_POPUP,
        m_data.posX, m_data.posY,
        width, height,
        desktopWorker,  // Parent to desktop WorkerW
        nullptr, hInstance, this
    );
    
    if (!m_hwnd) return false;
    
    // Enable drag & drop
    DragAcceptFiles(m_hwnd, TRUE);
    
    // Initial render
    Render();
    
    return true;
}

void FolderWidget::Show() {
    ShowWindow(m_hwnd, SW_SHOW);
}

void FolderWidget::Hide() {
    ShowWindow(m_hwnd, SW_HIDE);
}

void FolderWidget::UpdatePosition(int x, int y) {
    // Clamp to screen (Anchor point)
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > screenW - ICON_SIZE) x = screenW - ICON_SIZE;
    if (y > screenH - ICON_SIZE) y = screenH - ICON_SIZE;

    m_data.posX = x;
    m_data.posY = y;
    
    int windowX = x;
    if (m_data.isExpanded && m_expandLeft) {
        windowX = x - EXPANDED_WIDTH - 8;
    }
    SetWindowPos(m_hwnd, nullptr, windowX, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void FolderWidget::SetExpanded(bool expanded) {
    if (m_data.isExpanded == expanded) return;
    
    m_data.isExpanded = expanded;
    
    // Check screen position for smart expansion
    if (expanded) {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        m_expandLeft = (m_data.posX > screenW / 2);
    }
    
    // Resize window
    int width = expanded ? (ICON_SIZE + ICON_PADDING * 2 + EXPANDED_WIDTH + 16) : (ICON_SIZE + ICON_PADDING * 2);
    int height = expanded ? (EXPANDED_HEIGHT + 50) : (ICON_SIZE + ICON_PADDING * 2 + 15);
    
    // If expanding left, we need to shift the WINDOW position to the left
    // But Render() expects the folder icon to be at (0,0) offset inside the window usually
    // We will adjust Render() to draw at right-aligned offset instead
    
    // The window X position itself defines the top-left corner.
    // If we expand left, our visual "anchor" (the folder icon) should effectively stay in place.
    // So the Window X needs to move to the LEFT by the expansion amount.
    
    int newX = m_data.posX;
    if (expanded && m_expandLeft) {
        newX = m_data.posX - EXPANDED_WIDTH - 8;
    } else if (!expanded && m_expandLeft) {
        // When collapsing, we reset position back to the anchor
        // actually m_data.posX stores the top-left of the FOLDER ICON essentially
        // Wait, m_data.posX is the Window's top-left.
        // Let's assume m_data.posX is always the Folder Icon's intended position.
        // So window X = m_data.posX if Right, or m_data.posX - ExpandedWidth if Left.
    }
    
    // Actually, simplifying: Let m_data.posX always be the top-left of the FOLDER ICON visual.
    // So:
    int windowX = m_data.posX;
    if (expanded && m_expandLeft) {
        windowX = m_data.posX - EXPANDED_WIDTH - 8;
    }
    
    SetWindowPos(m_hwnd, nullptr, windowX, m_data.posY, width, height, SWP_NOZORDER);
    Render();
}

void FolderWidget::AddItem(const WidgetItem& item) {
    // Check if already exists
    for (const auto& existing : m_data.items) {
        if (existing.path == item.path) return;
    }
    m_data.items.push_back(item);
    Render();
}

void FolderWidget::RemoveItem(const std::wstring& path) {
    m_data.items.erase(
        std::remove_if(m_data.items.begin(), m_data.items.end(),
            [&path](const WidgetItem& item) { return item.path == path; }),
        m_data.items.end()
    );
    Render();
}

void FolderWidget::LaunchItem(const std::wstring& path) {
    // Use ShellExecute with error checking
    HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        // If direct open fails, try running with explorer
        ShellExecuteW(nullptr, L"open", L"explorer.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

void FolderWidget::SetColor(COLORREF color) {
    m_data.color = color;
    Render();
}

void FolderWidget::SetName(const std::wstring& name) {
    m_data.name = name;
    Render();
}

void FolderWidget::Render() {
    if (!m_hwnd) return;
    
    RECT rect;
    GetWindowRect(m_hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    
    // Create memory DC and bitmap
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    void* bits;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
    
    // Create GDI+ graphics
    Graphics g(hdcMem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    
    // Clear with transparent
    g.Clear(Color(0, 0, 0, 0));
    
    // Draw folder icon
    int folderX = ICON_PADDING;
    if (m_data.isExpanded && m_expandLeft) {
        folderX = ICON_PADDING + EXPANDED_WIDTH + 8;
    }
    RenderFolderIcon(g, folderX, ICON_PADDING);
    
    // Draw expanded pane if open
    if (m_data.isExpanded) {
        int paneX = m_expandLeft ? ICON_PADDING : (ICON_SIZE + ICON_PADDING * 2 + 8);
        RenderExpandedPane(g, paneX, ICON_PADDING);
    }
    
    // Update layered window
    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { width, height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT ptDst = { rect.left, rect.top };
    
    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    
    // Cleanup
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void FolderWidget::RenderFolderIcon(Graphics& g, int x, int y) {
    BYTE r = GetRValue(m_data.color);
    BYTE gb = GetGValue(m_data.color);
    BYTE b = GetBValue(m_data.color);
    
    // Folder body
    LinearGradientBrush bodyBrush(
        Point(x, y),
        Point(x + ICON_SIZE, y + ICON_SIZE),
        Color(230, r, gb, b),
        Color(200, (BYTE)(r * 0.7), (BYTE)(gb * 0.7), (BYTE)(b * 0.7))
    );
    
    // Folder shape
    GraphicsPath path;
    int tabWidth = ICON_SIZE / 3;
    int tabHeight = 8;
    int cornerRadius = 8;
    
    // Tab
    path.AddLine(x + cornerRadius, y, x + tabWidth, y);
    path.AddLine(x + tabWidth, y, x + tabWidth + 8, y + tabHeight);
    path.AddLine(x + tabWidth + 8, y + tabHeight, x + ICON_SIZE - cornerRadius, y + tabHeight);
    
    // Right side
    path.AddArc(x + ICON_SIZE - cornerRadius * 2, y + tabHeight, cornerRadius * 2, cornerRadius * 2, 270, 90);
    path.AddLine(x + ICON_SIZE, y + tabHeight + cornerRadius, x + ICON_SIZE, y + ICON_SIZE - 12 - cornerRadius);
    path.AddArc(x + ICON_SIZE - cornerRadius * 2, y + ICON_SIZE - 12 - cornerRadius * 2, cornerRadius * 2, cornerRadius * 2, 0, 90);
    
    // Bottom
    path.AddLine(x + ICON_SIZE - cornerRadius, y + ICON_SIZE - 12, x + cornerRadius, y + ICON_SIZE - 12);
    path.AddArc(x, y + ICON_SIZE - 12 - cornerRadius * 2, cornerRadius * 2, cornerRadius * 2, 90, 90);
    
    // Left side
    path.AddLine(x, y + ICON_SIZE - 12 - cornerRadius, x, y + cornerRadius);
    path.AddArc(x, y, cornerRadius * 2, cornerRadius * 2, 180, 90);
    
    path.CloseFigure();
    
    g.FillPath(&bodyBrush, &path);
    
    // Folder lines (content indicator)
    Pen linePen(Color(80, 255, 255, 255), 2);
    g.DrawLine(&linePen, x + 16, y + 28, x + ICON_SIZE - 16, y + 28);
    g.DrawLine(&linePen, x + 20, y + 36, x + ICON_SIZE - 20, y + 36);
    g.DrawLine(&linePen, x + 18, y + 44, x + ICON_SIZE - 18, y + 44);
    
    // Drop shadow effect
    if (m_isDragOver) {
        Pen glowPen(Color(150, r, gb, b), 3);
        g.DrawPath(&glowPen, &path);
    }
    
    // Item count badge
    if (!m_data.items.empty()) {
        SolidBrush badgeBrush(Color(255, r, gb, b));
        g.FillEllipse(&badgeBrush, x + ICON_SIZE - 16, y - 4, 20, 20);
        
        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, 10, FontStyleBold, UnitPixel);
        SolidBrush textBrush(Color(255, 255, 255, 255));
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        
        std::wstring countStr = std::to_wstring(m_data.items.size());
        RectF badgeRect((float)(x + ICON_SIZE - 16), (float)(y - 4), 20.0f, 20.0f);
        g.DrawString(countStr.c_str(), -1, &font, badgeRect, &sf, &textBrush);
    }
    
    // Folder name label
    FontFamily fontFamily(L"Segoe UI");
    Font font(&fontFamily, 11, FontStyleRegular, UnitPixel);
    SolidBrush textBrush(Color(255, 230, 230, 230));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    
    RectF textRect((float)x, (float)(y + ICON_SIZE - 8), (float)ICON_SIZE, 25.0f);
    g.DrawString(m_data.name.c_str(), -1, &font, textRect, &sf, &textBrush);
}

void FolderWidget::RenderExpandedPane(Graphics& g, int x, int y) {
    BYTE r = GetRValue(m_data.color);
    BYTE gb = GetGValue(m_data.color);
    BYTE b = GetBValue(m_data.color);
    
    // Glassmorphism background
    LinearGradientBrush bgBrush(
        Point(x, y),
        Point(x + EXPANDED_WIDTH, y + EXPANDED_HEIGHT),
        Color(200, (BYTE)(r * 0.15 + 30), (BYTE)(gb * 0.15 + 30), (BYTE)(b * 0.15 + 40)),
        Color(180, (BYTE)(r * 0.1 + 20), (BYTE)(gb * 0.1 + 20), (BYTE)(b * 0.1 + 30))
    );
    
    GraphicsPath bgPath;
    int radius = 16;
    bgPath.AddArc(x, y, radius * 2, radius * 2, 180, 90);
    bgPath.AddArc(x + EXPANDED_WIDTH - radius * 2, y, radius * 2, radius * 2, 270, 90);
    bgPath.AddArc(x + EXPANDED_WIDTH - radius * 2, y + EXPANDED_HEIGHT - radius * 2, radius * 2, radius * 2, 0, 90);
    bgPath.AddArc(x, y + EXPANDED_HEIGHT - radius * 2, radius * 2, radius * 2, 90, 90);
    bgPath.CloseFigure();
    
    g.FillPath(&bgBrush, &bgPath);
    
    // Border
    Pen borderPen(Color(60, r, gb, b), 1);
    g.DrawPath(&borderPen, &bgPath);
    
    // Items grid and Scrollbar
    int padding = 12;
    int itemX = x + padding;
    int itemY = y + padding;
    int visibleHeight = EXPANDED_HEIGHT - padding * 2;
    int contentWidth = EXPANDED_WIDTH - padding * 2;
    int itemWidth = contentWidth / ITEMS_PER_ROW;
    int itemHeight = ITEM_SIZE;
    
    // Calculate total height
    int totalRows = (int)((m_data.items.size() + ITEMS_PER_ROW - 1) / ITEMS_PER_ROW);
    int totalHeight = totalRows * (itemHeight + 8);
    
    // Update scroll clamp
    int maxScroll = (std::max)(0, totalHeight - visibleHeight);
    if (m_scrollOffset > maxScroll) m_scrollOffset = maxScroll;
    if (m_scrollOffset < 0) m_scrollOffset = 0;
    
    // Clip to content area
    Region originalClip;
    g.GetClip(&originalClip);
    Rect clipRect(x, y + 4, EXPANDED_WIDTH, visibleHeight); // Slightly adjusted clip
    g.SetClip(clipRect);

    if (m_data.items.empty()) {
        // Empty state
        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, 13, FontStyleRegular, UnitPixel);
        SolidBrush textBrush(Color(200, 200, 200, 200));
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        
        RectF textRect((float)x, (float)y, (float)EXPANDED_WIDTH, (float)EXPANDED_HEIGHT);
        g.DrawString(L"Drag and drop files", -1, &font, textRect, &sf, &textBrush); // Removed emoji
    } else {
        for (size_t i = 0; i < m_data.items.size(); i++) {
            int col = i % ITEMS_PER_ROW;
            int row = i / ITEMS_PER_ROW;
            
            int ix = itemX + col * itemWidth + (itemWidth - 60) / 2;
            int iy = itemY + row * (itemHeight + 8) - m_scrollOffset;
            
            // Optimization: Don't render if out of view
            if (iy + itemHeight < y || iy > y + visibleHeight) continue;

            RenderItem(g, m_data.items[i], ix, iy, 60, itemHeight);
        }
    }
    
    g.SetClip(&originalClip);
    
    // Draw Scrollbar if needed
    if (maxScroll > 0) {
        int sbWidth = 6;
        int sbHeight = (int)((float)visibleHeight * ((float)visibleHeight / (float)totalHeight));
        if (sbHeight < 30) sbHeight = 30;
        
        int sbX = x + EXPANDED_WIDTH - sbWidth - 4;
        int sbY = y + padding + (int)((float)m_scrollOffset / (float)maxScroll * (visibleHeight - sbHeight));
        
        SolidBrush sbBrush(Color(100, 255, 255, 255));
        g.FillRectangle(&sbBrush, sbX, sbY, sbWidth, sbHeight);
    }
}

void FolderWidget::RenderItem(Graphics& g, const WidgetItem& item, int x, int y, int width, int height) {
    BYTE r = GetRValue(m_data.color);
    BYTE gb = GetGValue(m_data.color);
    BYTE b = GetBValue(m_data.color);
    
    // Launch Animation Scale
    float scale = 1.0f;
    if (m_isLaunchAnimating && m_clickedItemIndex != -1 && 
        m_clickedItemIndex < m_data.items.size() && 
        m_data.items[m_clickedItemIndex].path == item.path) {
        scale = 0.95f;
    }
    
    // Adjust rect based on scale (centered)
    int cx = x + width / 2;
    int cy = y + height / 2;
    int sw = (int)(width * scale);
    int sh = (int)(height * scale);
    int sx = cx - sw / 2;
    int sy = cy - sh / 2;
    x = sx; y = sy; width = sw; height = sh;

    // Item background
    SolidBrush itemBg(Color(40, r, gb, b));
    GraphicsPath itemPath;
    int radius = 8;
    itemPath.AddArc(x, y, radius * 2, radius * 2, 180, 90);
    itemPath.AddArc(x + width - radius * 2, y, radius * 2, radius * 2, 270, 90);
    itemPath.AddArc(x + width - radius * 2, y + height - 20 - radius * 2, radius * 2, radius * 2, 0, 90);
    itemPath.AddArc(x, y + height - 20 - radius * 2, radius * 2, radius * 2, 90, 90);
    itemPath.CloseFigure();
    
    g.FillPath(&itemBg, &itemPath);
    
    // Draw Real Icon
    Bitmap* icon = GetIconForFile(item.path);
    if (icon) {
        // Draw icon centered in the top part
        int iconSize = 32;
        int iconX = x + (width - iconSize) / 2;
        int iconY = y + (height - 20 - iconSize) / 2;
        g.DrawImage(icon, iconX, iconY, iconSize, iconSize);
    } else {
        // Fallback emoji
        FontFamily fontFamily(L"Segoe UI Emoji");
        Font iconFont(&fontFamily, 28, FontStyleRegular, UnitPixel);
        SolidBrush iconBrush(Color(255, 255, 255, 255));
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        RectF iconRect((float)x, (float)y, (float)width, (float)(height - 20));
        g.DrawString(L"📄", -1, &iconFont, iconRect, &sf, &iconBrush);
    }
    
    // Item name
    FontFamily fontFamily(L"Segoe UI");
    Font nameFont(&fontFamily, 10, FontStyleRegular, UnitPixel);
    SolidBrush nameBrush(Color(255, 220, 220, 220));
    RectF nameRect((float)x, (float)(y + height - 18), (float)width, 18.0f);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    
    // Truncate name if too long
    std::wstring name = item.name;
    if (name.length() > 10) {
        name = name.substr(0, 8) + L"...";
    }
    g.DrawString(name.c_str(), -1, &nameFont, nameRect, &sf, &nameBrush);
}

void FolderWidget::ShowContextMenu(int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    
    AppendMenuW(hMenu, MF_STRING, 1, L"✏️ Rename");
    AppendMenuW(hMenu, MF_STRING, 2, L"🎨 Change Color");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 3, L"❌ Delete Folder");
    
    POINT pt = { x, y };
    ClientToScreen(m_hwnd, &pt);
    
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
    
    DestroyMenu(hMenu);
    
    if (cmd == 1) {
        // Simple input dialog for renaming
        struct DialogData {
            wchar_t buffer[256];
        } data;
        wcscpy_s(data.buffer, m_data.name.c_str());

        // Create a simple dialog template in memory
        // This is a bit hacky but avoids needing a .rc file
        struct {
            DLGTEMPLATE template_header;
            WORD menu;
            WORD class_name;
            WORD title;
            WORD point_size;
            WCHAR font_name[14];
        } dlg_template = {0};
        
        dlg_template.template_header.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
        dlg_template.template_header.dwExtendedStyle = 0;
        dlg_template.template_header.cdit = 0;
        dlg_template.template_header.x = 0;
        dlg_template.template_header.y = 0;
        dlg_template.template_header.cx = 200;
        dlg_template.template_header.cy = 55; // Height for input + buttons
        dlg_template.point_size = 9;
        wcscpy_s(dlg_template.font_name, L"Segoe UI");

        // We can't easily create controls without a resource file or complex in-memory template construction
        // So for simplicity, let's use a very basic approach:
        // Use a MessageBox to ask users (temporary) OR implement a real InputBox function manually
        
        // Let's implement a proper InputBox approach by creating a modal window manually
        // Since in-memory dialog templates are complex for controls
        
        static wchar_t inputBuffer[256];
        wcscpy_s(inputBuffer, m_data.name.c_str());
        
        // Register input window class
        const wchar_t* INPUT_CLASS = L"FolderWidgetInput";
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            switch (msg) {
                case WM_CREATE: {
                    // Start centered
                    int screenW = GetSystemMetrics(SM_CXSCREEN);
                    int screenH = GetSystemMetrics(SM_CYSCREEN);
                    SetWindowPos(hwnd, NULL, (screenW - 300) / 2, (screenH - 120) / 2, 300, 120, SWP_NOZORDER);
                    
                    // Create Edit control
                    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", (const wchar_t*)((LPCREATESTRUCT)lParam)->lpCreateParams,
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 20, 240, 25, hwnd, (HMENU)101, GetModuleHandle(NULL), NULL);
                    
                    // Create OK button
                    CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        110, 60, 80, 25, hwnd, (HMENU)IDOK, GetModuleHandle(NULL), NULL);
                        
                    return 0;
                }
                case WM_COMMAND:
                    if (LOWORD(wParam) == IDOK) {
                        GetDlgItemTextW(hwnd, 101, inputBuffer, 256);
                        PostMessage(hwnd, WM_CLOSE, 0, 0);
                        return 0;
                    }
                    break;
                case WM_CLOSE:
                    DestroyWindow(hwnd);
                    return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        };
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = INPUT_CLASS;
        
        // Check if class registered, if not register it
        if (!GetClassInfoExW(GetModuleHandle(nullptr), INPUT_CLASS, &wc)) {
            RegisterClassExW(&wc);
        }
        
        // Create the window modal-like by disabling parent
        HWND hInput = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, INPUT_CLASS, L"Rename Folder",
            WS_VISIBLE | WS_POPUP | WS_CAPTION | WS_SYSMENU, 
            0, 0, 300, 120, m_hwnd, nullptr, GetModuleHandle(nullptr), (LPVOID)inputBuffer);
            
        // Message loop for this window specifically (to simulate modal)
        if (hInput) {
            EnableWindow(m_hwnd, FALSE); // Disable parent
            
            MSG msg;
            while (GetMessage(&msg, nullptr, 0, 0)) {
                if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
                    // Forward Enter key to OK button
                    SendMessage(hInput, WM_COMMAND, IDOK, 0);
                }
                
                if (!IsWindow(hInput)) break;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            
            EnableWindow(m_hwnd, TRUE); // Re-enable parent
            SetForegroundWindow(m_hwnd);
            
            // Apply rename if valid
            if (wcslen(inputBuffer) > 0) {
                SetName(inputBuffer);
                WidgetManager::Instance().SaveToConfig();
            }
        }
    } else if (cmd == 2) {
        // Color picker - cycle through colors
        COLORREF colors[] = {
            RGB(59, 130, 246),  // Blue
            RGB(34, 197, 94),   // Green
            RGB(239, 68, 68),   // Red
            RGB(245, 158, 11),  // Orange
            RGB(139, 92, 246),  // Purple
            RGB(236, 72, 153),  // Pink
            RGB(6, 182, 212),   // Cyan
        };
        int numColors = sizeof(colors) / sizeof(colors[0]);
        
        for (int i = 0; i < numColors; i++) {
            if (m_data.color == colors[i]) {
                SetColor(colors[(i + 1) % numColors]);
                return;
            }
        }
        SetColor(colors[0]);
    } else if (cmd == 3) {
        // Delete folder
        WidgetManager::Instance().RemoveFolder(m_data.id);
    }
}

LRESULT CALLBACK FolderWidget::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FolderWidget* widget = nullptr;
    
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        widget = reinterpret_cast<FolderWidget*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(widget));
    } else {
        widget = reinterpret_cast<FolderWidget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    
    if (!widget) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    
    switch (msg) {
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            widget->OnMouseDown(x, y, false);
            return 0;
        }
        
        case WM_LBUTTONUP: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            widget->OnMouseUp(x, y);
            return 0;
        }
        
        case WM_RBUTTONUP: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            
            int itemIndex = widget->GetItemIndexAt(x, y);
            
            POINT pt;
            GetCursorPos(&pt);
            widget->ShowContextMenu(pt.x, pt.y, itemIndex);
            return 0;
        }
        

        
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            widget->OnMouseMove(x, y);
            return 0;
        }
        
        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            UINT numFiles = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            
            std::vector<std::wstring> files;
            for (UINT i = 0; i < numFiles; i++) {
                wchar_t filePath[MAX_PATH];
                DragQueryFileW(hDrop, i, filePath, MAX_PATH);
                files.push_back(filePath);
            }
            
            DragFinish(hDrop);
            widget->OnDrop(files);
            return 0;
        }
        
        case WM_MOUSEWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (widget) {
                widget->OnMouseWheel(delta);
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == 999) {
                KillTimer(hwnd, 999);
                if (widget) {
                    widget->m_isLaunchAnimating = false;
                    if (widget->m_clickedItemIndex != -1 && widget->m_clickedItemIndex < widget->m_data.items.size()) {
                        widget->LaunchItem(widget->m_data.items[widget->m_clickedItemIndex].path);
                    }
                    widget->m_clickedItemIndex = -1;
                    widget->Render();
                }
            }
            return 0;
        }

        case WM_DESTROY: {
            return 0;
        }
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void FolderWidget::OnMouseDown(int x, int y, bool rightClick) {
    // Check click on folder icon
    int folderX = m_expandLeft && m_data.isExpanded ? (ICON_PADDING + EXPANDED_WIDTH + 8) : ICON_PADDING;
    int folderY = ICON_PADDING;
    
    if (x >= folderX && x < folderX + ICON_SIZE && y >= folderY && y < folderY + ICON_SIZE) {
        m_isDragging = true;
        m_dragStart = { x, y };
        SetCapture(m_hwnd);
    } else if (m_data.isExpanded) {
        // Check click on items
        int padding = 12;
        int paneX = m_expandLeft ? ICON_PADDING : (ICON_SIZE + ICON_PADDING * 2 + 8);
        int itemWidth = (EXPANDED_WIDTH - padding * 2) / ITEMS_PER_ROW;
        int itemHeight = ITEM_SIZE + 8;
        
        for (size_t i = 0; i < m_data.items.size(); i++) {
            int col = i % ITEMS_PER_ROW;
            int row = i / ITEMS_PER_ROW;
            
            int ix = paneX + padding + col * itemWidth;
            int iy = ICON_PADDING + padding + row * itemHeight - m_scrollOffset; // Apply scroll
            
            // Check visibility
            if (iy + itemHeight < ICON_PADDING || iy > ICON_PADDING + EXPANDED_HEIGHT) continue;

            if (x >= ix && x < ix + itemWidth && y >= iy && y < iy + itemHeight) {
                // Start launch animation
                m_clickedItemIndex = i;
                m_isLaunchAnimating = true;
                Render();
                
                // Set timer for actual launch
                SetTimer(m_hwnd, 999, 100, nullptr);
                return;
            }
        }
    }
}

void FolderWidget::OnMouseWheel(int delta) {
    if (!m_data.isExpanded) return;
    
    m_scrollOffset -= delta / 3; // Adjust speed
    
    int padding = 12;
    int itemHeight = ITEM_SIZE + 8;
    int visibleHeight = EXPANDED_HEIGHT - padding * 2;
    int totalRows = (int)((m_data.items.size() + ITEMS_PER_ROW - 1) / ITEMS_PER_ROW);
    int totalHeight = totalRows * itemHeight;
    int maxScroll = (std::max)(0, totalHeight - visibleHeight);
    
    if (m_scrollOffset > maxScroll) m_scrollOffset = maxScroll;
    if (m_scrollOffset < 0) m_scrollOffset = 0;
    
    Render();
}

void FolderWidget::OnMouseUp(int x, int y) {
    if (m_isDragging) {
        m_isDragging = false;
        ReleaseCapture();
        
        // If didn't move much, treat as click
        if (std::abs(x - m_dragStart.x) < 5 && std::abs(y - m_dragStart.y) < 5) {
            SetExpanded(!m_data.isExpanded);
        }
        
        WidgetManager::Instance().SaveToConfig();
    }
}

void FolderWidget::OnMouseMove(int x, int y) {
    if (m_isDragging) {
        POINT cursorPos;
        GetCursorPos(&cursorPos);
        
        int newX = cursorPos.x - m_dragStart.x;
        int newY = cursorPos.y - m_dragStart.y;
        
        // Fix for Smart Expansion Dragging:
        // newX is the Window Top-Left. UpdatePosition expects Anchor position.
        int anchorX = newX;
        if (m_data.isExpanded && m_expandLeft) {
            anchorX += (EXPANDED_WIDTH + 8);
        }
        
        UpdatePosition(anchorX, newY);
    }
}

void FolderWidget::OnDrop(const std::vector<std::wstring>& files) {
    for (const auto& file : files) {
        // Get file name
        std::wstring name = file;
        size_t lastSlash = name.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            name = name.substr(lastSlash + 1);
        }
        
        // Remove extension for display
        size_t lastDot = name.find_last_of(L".");
        if (lastDot != std::wstring::npos) {
            name = name.substr(0, lastDot);
        }
        
        WidgetItem item;
        item.name = name;
        item.path = file;
        item.icon = L""; // Icon will be loaded dynamically
        
        AddItem(item);
    }
    
    // Auto-expand when items are dropped
    if (!m_data.isExpanded) {
        SetExpanded(true);
    }
    
    WidgetManager::Instance().SaveToConfig();
}

// ============================================
// WidgetManager Implementation
// ============================================

WidgetManager& WidgetManager::Instance() {
    static WidgetManager instance;
    return instance;
}

void WidgetManager::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);
    
    // Ensure config directory exists
    EnsureConfigDirectory();
    
    // Add to Windows startup (Registry)
    AddToStartup();
    
    LoadFromConfig();
    CreateSystemTray();
}

void WidgetManager::Shutdown() {
    SaveToConfig();
    RemoveSystemTray();
    m_folders.clear();
    GdiplusShutdown(g_gdiplusToken);
}

FolderWidget* WidgetManager::CreateFolder(const FolderData& data) {
    auto widget = std::make_unique<FolderWidget>(data);
    if (widget->Create(m_hInstance)) {
        widget->Show();
        FolderWidget* ptr = widget.get();
        m_folders.push_back(std::move(widget));
        return ptr;
    }
    return nullptr;
}

void WidgetManager::RemoveFolder(const std::wstring& id) {
    m_folders.erase(
        std::remove_if(m_folders.begin(), m_folders.end(),
            [&id](const std::unique_ptr<FolderWidget>& w) { return w->GetData().id == id; }),
        m_folders.end()
    );
    SaveToConfig();
}

FolderWidget* WidgetManager::GetFolder(const std::wstring& id) {
    for (auto& folder : m_folders) {
        if (folder->GetData().id == id) {
            return folder.get();
        }
    }
    return nullptr;
}

// Helper to get cached icon
Bitmap* FolderWidget::GetIconForFile(const std::wstring& path) {
    if (m_iconCache.find(path) != m_iconCache.end()) {
        return m_iconCache[path];
    }
    
    // Load icon
    SHFILEINFOW sfi = {0};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
        Bitmap* bmp = Bitmap::FromHICON(sfi.hIcon);
        m_iconCache[path] = bmp;
        DestroyIcon(sfi.hIcon);
        return bmp;
    }
    
    return nullptr;
}

void WidgetManager::LoadFromConfig() {
    std::wstring path = GetConfigPath();
    
    auto configs = Config::LoadFolders(path);
    
    for (const auto& cfg : configs) {
        FolderData data;
        data.id = cfg.id;
        data.name = cfg.name;
        data.color = HexToColorRef(cfg.color);
        data.posX = cfg.posX;
        data.posY = cfg.posY;
        data.isExpanded = false;
        
        for (const auto& item : cfg.items) {
            WidgetItem fi;
            fi.name = item.first;
            fi.path = item.second;
            fi.icon = L"";
            data.items.push_back(fi);
        }
        
        CreateFolder(data);
    }
}

void WidgetManager::SaveToConfig() {
    std::wstring path = GetConfigPath();
    
    std::vector<ConfigFolder> configs;
    
    for (const auto& folder : m_folders) {
        const auto& data = folder->GetData();
        ConfigFolder cfg;
        cfg.id = data.id;
        cfg.name = data.name;
        
        // Convert COLORREF to hex
        char hex[8];
        sprintf_s(hex, "#%02X%02X%02X", GetRValue(data.color), GetGValue(data.color), GetBValue(data.color));
        cfg.color = hex;
        
        cfg.posX = data.posX;
        cfg.posY = data.posY;
        
        for (const auto& item : data.items) {
            cfg.items.push_back({ item.name, item.path });
        }
        
        configs.push_back(cfg);
    }
    
    Config::SaveFolders(path, configs);
}

// Tray window procedure
LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                WidgetManager::Instance().ShowTrayMenu();
            }
            return 0;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_NEW_FOLDER: {
                    FolderData data;
                    data.id = L"folder-" + std::to_wstring(GetTickCount64());
                    data.name = L"New Folder";
                    data.color = RGB(59, 130, 246);
                    data.posX = 150;
                    data.posY = 150;
                    data.isExpanded = false;
                    WidgetManager::Instance().CreateFolder(data);
                    WidgetManager::Instance().SaveToConfig();
                    break;
                }
                case ID_TRAY_SHOW_ALL:
                    for (auto& folder : WidgetManager::Instance().GetFolders()) {
                        folder->Show();
                    }
                    break;
                case ID_TRAY_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void WidgetManager::CreateSystemTray() {
    // Create hidden window for tray messages
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = TRAY_CLASS;
    RegisterClassExW(&wc);
    
    m_trayHwnd = CreateWindowExW(0, TRAY_CLASS, L"TrayWindow", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, m_hInstance, nullptr);
    
    // Create tray icon
    ZeroMemory(&m_trayIcon, sizeof(m_trayIcon));
    m_trayIcon.cbSize = sizeof(m_trayIcon);
    m_trayIcon.hWnd = m_trayHwnd;
    m_trayIcon.uID = 1;
    m_trayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_trayIcon.uCallbackMessage = WM_TRAYICON;
    m_trayIcon.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(m_trayIcon.szTip, L"Folder Widgets");
    
    Shell_NotifyIconW(NIM_ADD, &m_trayIcon);
}

void WidgetManager::RemoveSystemTray() {
    Shell_NotifyIconW(NIM_DELETE, &m_trayIcon);
    DestroyWindow(m_trayHwnd);
}

void WidgetManager::ShowTrayMenu() {
    HMENU hMenu = CreatePopupMenu();
    
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_NEW_FOLDER, L"➕ New Folder");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW_ALL, L"📁 Show All Folders");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"❌ Exit");
    
    POINT pt;
    GetCursorPos(&pt);
    
    SetForegroundWindow(m_trayHwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, m_trayHwnd, nullptr);
    
    DestroyMenu(hMenu);
}

// ============================================
// Startup and Config Path Functions
// ============================================

void WidgetManager::AddToStartup() {
    // Registry key path for current user startup programs
    const wchar_t* REG_RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* APP_NAME = L"FolderWidget";
    
    // Get current exe path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey);
    
    if (result == ERROR_SUCCESS) {
        // Check if already exists
        wchar_t existingPath[MAX_PATH] = {0};
        DWORD size = sizeof(existingPath);
        DWORD type;
        
        result = RegQueryValueExW(hKey, APP_NAME, nullptr, &type, (LPBYTE)existingPath, &size);
        
        if (result != ERROR_SUCCESS || wcscmp(existingPath, exePath) != 0) {
            // Add or update registry entry
            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (LPBYTE)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
        }
        
        RegCloseKey(hKey);
    }
}

void WidgetManager::RemoveFromStartup() {
    const wchar_t* REG_RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* APP_NAME = L"FolderWidget";
    
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_SET_VALUE, &hKey);
    
    if (result == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, APP_NAME);
        RegCloseKey(hKey);
    }
}

bool WidgetManager::IsInStartup() {
    const wchar_t* REG_RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* APP_NAME = L"FolderWidget";
    
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_QUERY_VALUE, &hKey);
    
    if (result == ERROR_SUCCESS) {
        wchar_t existingPath[MAX_PATH] = {0};
        DWORD size = sizeof(existingPath);
        DWORD type;
        
        result = RegQueryValueExW(hKey, APP_NAME, nullptr, &type, (LPBYTE)existingPath, &size);
        RegCloseKey(hKey);
        
        return (result == ERROR_SUCCESS && wcslen(existingPath) > 0);
    }
    
    return false;
}

std::wstring WidgetManager::GetConfigPath() {
    wchar_t appDataPath[MAX_PATH];
    
    // Get %APPDATA% folder (e.g., C:\Users\Username\AppData\Roaming)
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        std::wstring configDir = std::wstring(appDataPath) + L"\\FolderWidget";
        std::wstring configPath = configDir + L"\\folders.json";
        return configPath;
    }
    
    // Fallback to exe directory if AppData fails
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    return path.substr(0, path.find_last_of(L"\\/") + 1) + L"folders.json";
}

void WidgetManager::EnsureConfigDirectory() {
    wchar_t appDataPath[MAX_PATH];
    
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        std::wstring configDir = std::wstring(appDataPath) + L"\\FolderWidget";
        
        // Create directory if it doesn't exist
        CreateDirectoryW(configDir.c_str(), nullptr);
        
        // Migrate old config if exists
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring oldConfigPath = std::wstring(exePath);
        oldConfigPath = oldConfigPath.substr(0, oldConfigPath.find_last_of(L"\\/") + 1) + L"folders.json";
        
        std::wstring newConfigPath = configDir + L"\\folders.json";
        
        // Check if new config doesn't exist but old one does - migrate it
        if (GetFileAttributesW(newConfigPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(oldConfigPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            CopyFileW(oldConfigPath.c_str(), newConfigPath.c_str(), TRUE);
        }
    }
}
