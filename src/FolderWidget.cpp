#include "FolderWidget.h"
#include "Config.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace {
    void EnsureUiAssets();
    HICON GetAppIcon(bool smallIcon);
    POINT FindNextFolderPosition(int iconSize);
    POINT ClampFolderPosition(int desiredX, int desiredY, int iconSize);
    bool FolderPositionOverlaps(const FolderWidget* movingWidget, int desiredX, int desiredY, int iconSize);
    POINT ResolveFolderPosition(const FolderWidget* movingWidget, int desiredX, int desiredY, int iconSize);
}

// Custom rename modal
namespace {
    constexpr UINT ID_RENAME_EDIT = 3001;
    constexpr UINT ID_RENAME_SAVE = 3002;
    constexpr UINT ID_RENAME_CANCEL = 3003;

    struct RenameDialogState {
        std::wstring initialName;
        std::wstring resultName;
        HWND editHwnd = nullptr;
        HWND saveHwnd = nullptr;
        HWND cancelHwnd = nullptr;
        HFONT titleFont = nullptr;
        HFONT bodyFont = nullptr;
        HFONT buttonFont = nullptr;
        HBRUSH editBrush = nullptr;
        bool accepted = false;
    };

    HFONT CreateDialogFont(int pixelHeight, LONG weight) {
        LOGFONTW font = {};
        NONCLIENTMETRICSW metrics = {};
        metrics.cbSize = sizeof(metrics);

        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            font = metrics.lfMessageFont;
        } else {
            wcscpy_s(font.lfFaceName, L"Segoe UI");
        }

        font.lfHeight = -pixelHeight;
        font.lfWeight = weight;
        font.lfQuality = CLEARTYPE_QUALITY;
        return CreateFontIndirectW(&font);
    }

    void ApplyDialogRegion(HWND hwnd, int width, int height) {
        HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, 28, 28);
        SetWindowRgn(hwnd, region, TRUE);
    }

    RECT GetRenameDialogWorkArea(HWND parent) {
        MONITORINFO monitorInfo = {};
        monitorInfo.cbSize = sizeof(monitorInfo);
        HMONITOR monitor = MonitorFromWindow(parent ? parent : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
        if (GetMonitorInfoW(monitor, &monitorInfo)) {
            return monitorInfo.rcWork;
        }

        RECT fallback = {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback, 0);
        return fallback;
    }
}

static LRESULT CALLBACK InputBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    constexpr COLORREF DIALOG_BG = RGB(11, 18, 32);
    constexpr COLORREF DIALOG_BORDER = RGB(71, 85, 105);
    constexpr COLORREF DIALOG_ACCENT = RGB(99, 102, 241);
    constexpr COLORREF DIALOG_TEXT = RGB(241, 245, 249);
    constexpr COLORREF DIALOG_MUTED = RGB(148, 163, 184);
    constexpr COLORREF DIALOG_EDIT_BG = RGB(17, 24, 39);
    constexpr COLORREF DIALOG_EDIT_BORDER = RGB(51, 65, 85);
    constexpr COLORREF DIALOG_EDIT_BORDER_FOCUS = RGB(71, 85, 105);
    constexpr COLORREF DIALOG_PRIMARY = RGB(79, 70, 229);
    constexpr COLORREF DIALOG_PRIMARY_PRESSED = RGB(67, 56, 202);
    constexpr COLORREF DIALOG_SECONDARY = RGB(30, 41, 59);
    constexpr COLORREF DIALOG_SECONDARY_PRESSED = RGB(51, 65, 85);

    auto* state = reinterpret_cast<RenameDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }

        case WM_CREATE: {
            state = reinterpret_cast<RenameDialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            state->titleFont = CreateDialogFont(20, FW_SEMIBOLD);
            state->bodyFont = CreateDialogFont(12, FW_NORMAL);
            state->buttonFont = CreateDialogFont(12, FW_SEMIBOLD);
            state->editBrush = CreateSolidBrush(DIALOG_EDIT_BG);

            state->editHwnd = CreateWindowExW(
                0,
                L"EDIT",
                state->initialName.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                36, 106, 228, 20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RENAME_EDIT)),
                GetModuleHandle(nullptr),
                nullptr
            );

            SendMessageW(state->editHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            SendMessageW(state->editHwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(12, 12));
            SendMessageW(state->editHwnd, EM_SETSEL, 0, -1);

            state->cancelHwnd = CreateWindowW(
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                116, 140, 72, 32,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RENAME_CANCEL)),
                GetModuleHandle(nullptr),
                nullptr
            );

            state->saveHwnd = CreateWindowW(
                L"BUTTON",
                L"Save",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                204, 140, 72, 32,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RENAME_SAVE)),
                GetModuleHandle(nullptr),
                nullptr
            );

            SendMessageW(state->cancelHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(state->buttonFont), TRUE);
            SendMessageW(state->saveHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(state->buttonFont), TRUE);

            RECT clientRect = {};
            GetClientRect(hwnd, &clientRect);
            ApplyDialogRegion(hwnd, clientRect.right, clientRect.bottom);
            SetFocus(state->editHwnd);
            return 0;
        }

        case WM_SIZE:
            ApplyDialogRegion(hwnd, LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, DIALOG_MUTED);
            return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, DIALOG_EDIT_BG);
            SetTextColor(hdc, DIALOG_TEXT);
            return reinterpret_cast<INT_PTR>(state->editBrush);
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!draw || draw->CtlType != ODT_BUTTON) {
                break;
            }

            const bool isPrimary = draw->CtlID == ID_RENAME_SAVE;
            const bool isPressed = (draw->itemState & ODS_SELECTED) != 0;

            COLORREF fill = isPrimary ? (isPressed ? DIALOG_PRIMARY_PRESSED : DIALOG_PRIMARY)
                                      : (isPressed ? DIALOG_SECONDARY_PRESSED : DIALOG_SECONDARY);
            COLORREF border = isPrimary ? DIALOG_PRIMARY : DIALOG_EDIT_BORDER;

            HDC hdc = draw->hDC;
            RECT rect = draw->rcItem;

            HBRUSH baseBrush = CreateSolidBrush(DIALOG_BG);
            FillRect(hdc, &rect, baseBrush);
            DeleteObject(baseBrush);

            HBRUSH fillBrush = CreateSolidBrush(fill);
            HPEN borderPen = CreatePen(PS_SOLID, 1, border);
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, fillBrush));
            HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));

            RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 14, 14);

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(fillBrush);
            DeleteObject(borderPen);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, DIALOG_TEXT);
            HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, state->buttonFont));
            DrawTextW(hdc, draw->CtlID == ID_RENAME_SAVE ? L"Save" : L"Cancel", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont);
            return TRUE;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT client = {};
            GetClientRect(hwnd, &client);

            HBRUSH bgBrush = CreateSolidBrush(DIALOG_BG);
            FillRect(hdc, &client, bgBrush);
            DeleteObject(bgBrush);

            HPEN borderPen = CreatePen(PS_SOLID, 1, DIALOG_BORDER);
            HBRUSH hollowBrush = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
            HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, hollowBrush));
            RoundRect(hdc, client.left, client.top, client.right, client.bottom, 28, 28);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(borderPen);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, DIALOG_TEXT);

            RECT titleRect = { 24, 26, client.right - 24, 54 };
            HFONT oldTitleFont = static_cast<HFONT>(SelectObject(hdc, state->titleFont));
            DrawTextW(hdc, L"Rename Folder", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldTitleFont);

            RECT subtitleRect = { 24, 58, client.right - 24, 76 };
            HFONT oldBodyFont = static_cast<HFONT>(SelectObject(hdc, state->bodyFont));
            SetTextColor(hdc, DIALOG_MUTED);
            DrawTextW(hdc, L"Give this folder a cleaner label.", -1, &subtitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT labelRect = { 24, 82, client.right - 24, 96 };
            SetTextColor(hdc, DIALOG_MUTED);
            DrawTextW(hdc, L"Folder name", -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT editRect = { 24, 98, client.right - 24, 134 };

            HBRUSH editPanelBrush = CreateSolidBrush(DIALOG_EDIT_BG);
            HPEN editBorderPen = CreatePen(PS_SOLID, 1, GetFocus() == state->editHwnd ? DIALOG_EDIT_BORDER_FOCUS : DIALOG_EDIT_BORDER);
            oldBrush = static_cast<HBRUSH>(SelectObject(hdc, editPanelBrush));
            oldPen = static_cast<HPEN>(SelectObject(hdc, editBorderPen));
            RoundRect(hdc, editRect.left, editRect.top, editRect.right, editRect.bottom, 10, 10);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(editPanelBrush);
            DeleteObject(editBorderPen);
            SelectObject(hdc, oldBodyFont);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_RENAME_SAVE) {
                wchar_t buffer[256] = {};
                GetWindowTextW(state->editHwnd, buffer, 256);
                if (wcslen(buffer) > 0) {
                    state->resultName = buffer;
                    state->accepted = true;
                    DestroyWindow(hwnd);
                } else {
                    SetFocus(state->editHwnd);
                    SendMessageW(state->editHwnd, EM_SETSEL, 0, -1);
                }
                return 0;
            }

            if (LOWORD(wParam) == ID_RENAME_CANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }

            if (LOWORD(wParam) == ID_RENAME_EDIT && (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool ShowInputBox(HWND parent, std::wstring& outName, const std::wstring& currentName) {
    EnsureUiAssets();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc = InputBoxProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"FoldRRenameDialogClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = GetAppIcon(false);
    wc.hIconSm = GetAppIcon(true);

    if (!GetClassInfoExW(wc.hInstance, wc.lpszClassName, &wc)) {
        RegisterClassExW(&wc);
    }

    RenameDialogState state = {};
    state.initialName = currentName;

    RECT parentRect = {};
    GetWindowRect(parent, &parentRect);
    const int dialogWidth = 300;
    const int dialogHeight = 180;
    RECT workArea = GetRenameDialogWorkArea(parent);
    int x = parentRect.left + ((parentRect.right - parentRect.left) - dialogWidth) / 2;
    int y = parentRect.top + ((parentRect.bottom - parentRect.top) - dialogHeight) / 2;
    const int minX = static_cast<int>(workArea.left) + 12;
    const int maxX = static_cast<int>(workArea.right) - dialogWidth - 12;
    const int minY = static_cast<int>(workArea.top) + 12;
    const int maxY = static_cast<int>(workArea.bottom) - dialogHeight - 12;
    x = (std::max)(minX, (std::min)(x, maxX));
    y = (std::max)(minY, (std::min)(y, maxY));

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        wc.lpszClassName,
        L"FoldR Rename",
        WS_POPUP | WS_CLIPCHILDREN,
        x,
        y,
        dialogWidth,
        dialogHeight,
        parent,
        nullptr,
        wc.hInstance,
        &state
    );

    if (!hwnd) {
        return false;
    }

    EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    MSG msg = {};
    while (IsWindow(hwnd) && GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.hwnd == state.editHwnd && msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) {
                SendMessageW(hwnd, WM_COMMAND, ID_RENAME_SAVE, 0);
                continue;
            }
            if (msg.wParam == VK_ESCAPE) {
                SendMessageW(hwnd, WM_CLOSE, 0, 0);
                continue;
            }
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    if (state.titleFont) DeleteObject(state.titleFont);
    if (state.bodyFont) DeleteObject(state.bodyFont);
    if (state.buttonFont) DeleteObject(state.buttonFont);
    if (state.editBrush) DeleteObject(state.editBrush);

    if (state.accepted) {
        outName = state.resultName;
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
#define ID_MENU_RENAME_FOLDER 2001
#define ID_MENU_DELETE_FOLDER 2002
#define ID_MENU_OPEN_LOCATION 2010
#define ID_MENU_REMOVE_ITEM 2011
#define ID_MENU_COLOR_AZURE 2101
#define ID_MENU_COLOR_MINT 2102
#define ID_MENU_COLOR_CRIMSON 2103
#define ID_MENU_COLOR_AMBER 2104
#define ID_MENU_COLOR_VIOLET 2105
#define ID_MENU_COLOR_ROSE 2106
#define ID_MENU_COLOR_CYAN 2107
#define ID_MENU_COLOR_SLATE 2108
#define ID_MENU_COLOR_LIME 2109
#define ID_MENU_COLOR_CORAL 2110
#define ID_MENU_SIZE_COMPACT 2201
#define ID_MENU_SIZE_SMALL 2202
#define ID_MENU_SIZE_MEDIUM 2203
#define ID_MENU_SIZE_LARGE 2204
#define ID_MENU_SIZE_XL 2205

namespace {
    constexpr wchar_t APP_TITLE[] = L"FoldR";
    constexpr COLORREF MENU_BG = RGB(246, 248, 252);
    constexpr COLORREF MENU_TEXT = RGB(30, 41, 59);
    constexpr COLORREF MENU_HOVER_BG = RGB(219, 234, 254);
    constexpr COLORREF MENU_HOVER_BORDER = RGB(147, 197, 253);
    constexpr COLORREF MENU_SELECTED_BG = RGB(232, 240, 254);
    constexpr COLORREF MENU_SELECTED_BORDER = RGB(96, 165, 250);
    constexpr COLORREF MENU_DANGER = RGB(190, 24, 93);
    constexpr COLORREF MENU_SWATCH_BORDER = RGB(203, 213, 225);
    constexpr COLORREF MENU_NO_COLOR = 0xFFFFFFFF;
    constexpr int DEFAULT_FOLDER_ICON_SIZE = 64;
    constexpr int DEFAULT_PANE_WIDTH = 360;
    constexpr int DEFAULT_PANE_HEIGHT = 280;
    constexpr int MIN_PANE_WIDTH = 220;
    constexpr int MIN_PANE_HEIGHT = 180;
    constexpr int RESIZE_HANDLE_SIZE = 10;
    constexpr int RESIZE_EDGE_NONE = 0;
    constexpr int RESIZE_EDGE_HORIZONTAL = 1;
    constexpr int RESIZE_EDGE_VERTICAL = 2;
    constexpr int FOLDER_ICON_PADDING = 16;
    constexpr int FOLDER_LABEL_EXTRA_HEIGHT = 15;
    constexpr int FOLDER_SLOT_GAP = 12;
    constexpr int FOLDER_SEARCH_STEP = 12;
    constexpr int FOLDER_ICON_SIZE_PRESETS[] = { 48, 56, 64, 72, 80 };
    constexpr int ITEM_MIN_SLOT_WIDTH = 84;
    constexpr int ITEM_MIN_CARD_WIDTH = 56;
    constexpr int ITEM_MAX_CARD_WIDTH = 72;
    constexpr int ITEM_MIN_ROW_HEIGHT = 80;
    constexpr int PANE_PADDING = 12;
    constexpr int PANE_GAP = 8;

    HICON g_appIconLarge = nullptr;
    HICON g_appIconSmall = nullptr;
    HFONT g_menuFont = nullptr;
    HBRUSH g_menuBackgroundBrush = nullptr;
    COLORREF g_activeFolderMenuColor = MENU_NO_COLOR;
    int g_activeFolderMenuIconSize = DEFAULT_FOLDER_ICON_SIZE;

    struct MenuVisualSpec {
        UINT id;
        const wchar_t* label;
        bool destructive;
        bool colorChoice;
        COLORREF swatchColor;
        bool sizeChoice;
        int iconSize;
    };

    int NormalizeFolderIconSize(int iconSize) {
        int bestSize = DEFAULT_FOLDER_ICON_SIZE;
        int bestDistance = INT_MAX;

        for (int preset : FOLDER_ICON_SIZE_PRESETS) {
            const int distance = std::abs(iconSize - preset);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestSize = preset;
            }
        }

        return bestSize;
    }

    int GetFolderSlotWidth(int iconSize) {
        return NormalizeFolderIconSize(iconSize) + FOLDER_ICON_PADDING * 2;
    }

    int GetFolderSlotHeight(int iconSize) {
        return NormalizeFolderIconSize(iconSize) + FOLDER_ICON_PADDING * 2 + FOLDER_LABEL_EXTRA_HEIGHT;
    }

    int ScaleByDpi(int value, int dpi) {
        return MulDiv(value, dpi, 96);
    }

    void AddRoundedRect(GraphicsPath& path, REAL x, REAL y, REAL width, REAL height, REAL radius) {
        REAL diameter = radius * 2.0f;
        path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
        path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
        path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.0f, 90.0f);
        path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
        path.CloseFigure();
    }

    void FillRoundedRect(Graphics& graphics, const RectF& rect, REAL radius, const Color& color) {
        GraphicsPath path;
        AddRoundedRect(path, rect.X, rect.Y, rect.Width, rect.Height, radius);
        SolidBrush brush(color);
        graphics.FillPath(&brush, &path);
    }

    HICON CreateFoldRIcon(int iconSize) {
        Bitmap bitmap(iconSize, iconSize, PixelFormat32bppARGB);
        Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.Clear(Color(0, 0, 0, 0));

        const REAL size = static_cast<REAL>(iconSize);
        FillRoundedRect(graphics, RectF(size * 0.06f, size * 0.08f, size * 0.88f, size * 0.84f), size * 0.20f, Color(255, 241, 245, 249));
        FillRoundedRect(graphics, RectF(size * 0.20f, size * 0.24f, size * 0.26f, size * 0.16f), size * 0.06f, Color(255, 37, 99, 235));
        FillRoundedRect(graphics, RectF(size * 0.14f, size * 0.34f, size * 0.72f, size * 0.40f), size * 0.10f, Color(255, 245, 158, 11));
        FillRoundedRect(graphics, RectF(size * 0.23f, size * 0.48f, size * 0.40f, size * 0.08f), size * 0.03f, Color(255, 37, 99, 235));

        SolidBrush detailBrush(Color(255, 30, 41, 59));
        graphics.FillEllipse(&detailBrush, size * 0.68f, size * 0.50f, size * 0.08f, size * 0.08f);

        HICON icon = nullptr;
        bitmap.GetHICON(&icon);
        return icon;
    }

    Bitmap* CreateBitmapFromIcon(HICON icon, int bitmapSize) {
        if (!icon) {
            return nullptr;
        }

        auto bitmap = std::make_unique<Bitmap>(bitmapSize, bitmapSize, PixelFormat32bppARGB);
        Graphics graphics(bitmap.get());
        graphics.Clear(Color(0, 0, 0, 0));

        HDC hdc = graphics.GetHDC();
        const BOOL drawn = DrawIconEx(hdc, 0, 0, icon, bitmapSize, bitmapSize, 0, nullptr, DI_NORMAL);
        graphics.ReleaseHDC(hdc);

        if (!drawn || bitmap->GetLastStatus() != Ok) {
            return nullptr;
        }

        return bitmap.release();
    }

    void EnsureUiAssets() {
        if (!g_menuBackgroundBrush) {
            g_menuBackgroundBrush = CreateSolidBrush(MENU_BG);
        }

        if (!g_menuFont) {
            NONCLIENTMETRICSW metrics = {};
            metrics.cbSize = sizeof(metrics);

            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
                g_menuFont = CreateFontIndirectW(&metrics.lfMenuFont);
            } else {
                LOGFONTW fallback = {};
                fallback.lfHeight = -13;
                wcscpy_s(fallback.lfFaceName, L"Segoe UI");
                g_menuFont = CreateFontIndirectW(&fallback);
            }
        }

        if (!g_appIconLarge) {
            g_appIconLarge = CreateFoldRIcon((std::max)(32, GetSystemMetrics(SM_CXICON)));
        }

        if (!g_appIconSmall) {
            g_appIconSmall = CreateFoldRIcon((std::max)(16, GetSystemMetrics(SM_CXSMICON)));
        }
    }

    void ReleaseUiAssets() {
        if (g_appIconLarge) {
            DestroyIcon(g_appIconLarge);
            g_appIconLarge = nullptr;
        }

        if (g_appIconSmall) {
            DestroyIcon(g_appIconSmall);
            g_appIconSmall = nullptr;
        }

        if (g_menuFont) {
            DeleteObject(g_menuFont);
            g_menuFont = nullptr;
        }

        if (g_menuBackgroundBrush) {
            DeleteObject(g_menuBackgroundBrush);
            g_menuBackgroundBrush = nullptr;
        }
    }

    HICON GetAppIcon(bool smallIcon) {
        EnsureUiAssets();
        return smallIcon ? g_appIconSmall : g_appIconLarge;
    }

    HFONT GetMenuFont() {
        EnsureUiAssets();
        return g_menuFont;
    }

    HBRUSH GetMenuBackgroundBrush() {
        EnsureUiAssets();
        return g_menuBackgroundBrush;
    }

    const MenuVisualSpec* GetMenuVisualSpec(UINT itemId) {
        static const MenuVisualSpec specs[] = {
            { ID_TRAY_NEW_FOLDER, L"New Folder", false, false, 0, false, 0 },
            { ID_TRAY_EXIT, L"Exit FoldR", true, false, 0, false, 0 },
            { ID_MENU_RENAME_FOLDER, L"Rename Folder", false, false, 0, false, 0 },
            { ID_MENU_DELETE_FOLDER, L"Delete Folder", true, false, 0, false, 0 },
            { ID_MENU_OPEN_LOCATION, L"Open File Location", false, false, 0, false, 0 },
            { ID_MENU_REMOVE_ITEM, L"Remove From Folder", true, false, 0, false, 0 },
            { ID_MENU_SIZE_COMPACT, L"Size Compact", false, false, 0, true, 48 },
            { ID_MENU_SIZE_SMALL, L"Size Small", false, false, 0, true, 56 },
            { ID_MENU_SIZE_MEDIUM, L"Size Medium", false, false, 0, true, 64 },
            { ID_MENU_SIZE_LARGE, L"Size Large", false, false, 0, true, 72 },
            { ID_MENU_SIZE_XL, L"Size XL", false, false, 0, true, 80 },
            { ID_MENU_COLOR_AZURE, L"Azure Blue", false, true, RGB(59, 130, 246), false, 0 },
            { ID_MENU_COLOR_MINT, L"Mint Green", false, true, RGB(34, 197, 94), false, 0 },
            { ID_MENU_COLOR_CRIMSON, L"Crimson Red", false, true, RGB(239, 68, 68), false, 0 },
            { ID_MENU_COLOR_AMBER, L"Amber Gold", false, true, RGB(245, 158, 11), false, 0 },
            { ID_MENU_COLOR_VIOLET, L"Soft Violet", false, true, RGB(139, 92, 246), false, 0 },
            { ID_MENU_COLOR_ROSE, L"Rose Pink", false, true, RGB(236, 72, 153), false, 0 },
            { ID_MENU_COLOR_CYAN, L"Arctic Cyan", false, true, RGB(6, 182, 212), false, 0 },
            { ID_MENU_COLOR_SLATE, L"Slate Steel", false, true, RGB(71, 85, 105), false, 0 },
            { ID_MENU_COLOR_LIME, L"Lime Pop", false, true, RGB(132, 204, 22), false, 0 },
            { ID_MENU_COLOR_CORAL, L"Coral Burst", false, true, RGB(249, 115, 22), false, 0 },
        };

        for (const auto& spec : specs) {
            if (spec.id == itemId) {
                return &spec;
            }
        }

        return nullptr;
    }

    HMENU CreateStyledPopupMenu() {
        HMENU menu = CreatePopupMenu();
        MENUINFO menuInfo = { sizeof(menuInfo) };
        menuInfo.fMask = MIM_BACKGROUND;
        menuInfo.hbrBack = GetMenuBackgroundBrush();
        SetMenuInfo(menu, &menuInfo);
        return menu;
    }

    void AppendStyledMenuItem(HMENU menu, UINT itemId) {
        const MenuVisualSpec* spec = GetMenuVisualSpec(itemId);
        if (!spec) {
            return;
        }

        AppendMenuW(menu, MF_OWNERDRAW | MF_STRING, itemId, spec->label);
    }

    bool HandleStyledMenuMeasure(MEASUREITEMSTRUCT* measureItem) {
        if (!measureItem || measureItem->CtlType != ODT_MENU) {
            return false;
        }

        const MenuVisualSpec* spec = GetMenuVisualSpec(measureItem->itemID);
        if (!spec) {
            return false;
        }

        HDC screenDc = GetDC(nullptr);
        const int dpi = GetDeviceCaps(screenDc, LOGPIXELSY);
        HFONT oldFont = static_cast<HFONT>(SelectObject(screenDc, GetMenuFont()));

        SIZE textSize = {};
        GetTextExtentPoint32W(screenDc, spec->label, static_cast<int>(wcslen(spec->label)), &textSize);

        SelectObject(screenDc, oldFont);
        ReleaseDC(nullptr, screenDc);

        const int textWidth = static_cast<int>(textSize.cx);
        const int textHeight = static_cast<int>(textSize.cy);
        measureItem->itemWidth = (std::max)(ScaleByDpi(196, dpi), textWidth + ScaleByDpi(40, dpi));
        measureItem->itemHeight = (std::max)(ScaleByDpi(34, dpi), textHeight + ScaleByDpi(14, dpi));
        return true;
    }

    bool HandleStyledMenuDraw(DRAWITEMSTRUCT* drawItem) {
        if (!drawItem || drawItem->CtlType != ODT_MENU) {
            return false;
        }

        const MenuVisualSpec* spec = GetMenuVisualSpec(drawItem->itemID);
        if (!spec) {
            return false;
        }

        HDC hdc = drawItem->hDC;
        const int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        RECT bounds = drawItem->rcItem;
        FillRect(hdc, &bounds, GetMenuBackgroundBrush());

        RECT pill = bounds;
        InflateRect(&pill, -ScaleByDpi(6, dpi), -ScaleByDpi(2, dpi));

        const bool isSelected = (drawItem->itemState & ODS_SELECTED) != 0;
        const bool isCurrentColor = spec->colorChoice && g_activeFolderMenuColor == spec->swatchColor;
        const bool isCurrentSize = spec->sizeChoice && NormalizeFolderIconSize(g_activeFolderMenuIconSize) == spec->iconSize;

        if (isSelected || isCurrentColor || isCurrentSize) {
            const COLORREF fillColor = isSelected ? MENU_HOVER_BG : MENU_SELECTED_BG;
            const COLORREF borderColor = isSelected ? MENU_HOVER_BORDER : MENU_SELECTED_BORDER;
            HPEN hoverPen = CreatePen(PS_SOLID, 1, borderColor);
            HBRUSH hoverBrush = CreateSolidBrush(fillColor);
            HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, hoverPen));
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, hoverBrush));

            RoundRect(hdc, pill.left, pill.top, pill.right, pill.bottom, ScaleByDpi(10, dpi), ScaleByDpi(10, dpi));

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(hoverBrush);
            DeleteObject(hoverPen);
        }

        RECT textRect = pill;
        textRect.left += ScaleByDpi(16, dpi);
        textRect.right -= ScaleByDpi(16, dpi);

        if (spec->colorChoice) {
            RECT swatchRect = pill;
            const int swatchSize = ScaleByDpi(16, dpi);
            swatchRect.left += ScaleByDpi(12, dpi);
            swatchRect.top += ((pill.bottom - pill.top) - swatchSize) / 2;
            swatchRect.right = swatchRect.left + swatchSize;
            swatchRect.bottom = swatchRect.top + swatchSize;

            HPEN swatchPen = CreatePen(PS_SOLID, isCurrentColor ? 2 : 1, isCurrentColor ? MENU_SELECTED_BORDER : MENU_SWATCH_BORDER);
            HBRUSH swatchBrush = CreateSolidBrush(spec->swatchColor);
            HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, swatchPen));
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, swatchBrush));

            RoundRect(
                hdc,
                swatchRect.left,
                swatchRect.top,
                swatchRect.right,
                swatchRect.bottom,
                ScaleByDpi(6, dpi),
                ScaleByDpi(6, dpi)
            );

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(swatchBrush);
            DeleteObject(swatchPen);

            textRect.left = swatchRect.right + ScaleByDpi(12, dpi);
        }

        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, GetMenuFont()));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, spec->destructive ? MENU_DANGER : ((isCurrentColor || isCurrentSize) ? MENU_SELECTED_BORDER : MENU_TEXT));
        DrawTextW(hdc, spec->label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oldFont);

        return true;
    }

    void ExecuteTrayCommand(UINT command) {
        switch (command) {
            case ID_TRAY_NEW_FOLDER: {
                const POINT nextPosition = FindNextFolderPosition(DEFAULT_FOLDER_ICON_SIZE);
                FolderData data;
                data.id = L"folder-" + std::to_wstring(GetTickCount64());
                data.name = L"New Folder";
                data.color = RGB(59, 130, 246);
                data.posX = nextPosition.x;
                data.posY = nextPosition.y;
                data.iconSize = DEFAULT_FOLDER_ICON_SIZE;
                data.paneWidth = DEFAULT_PANE_WIDTH;
                data.paneHeight = DEFAULT_PANE_HEIGHT;
                data.isExpanded = false;
                WidgetManager::Instance().CreateFolder(data);
                WidgetManager::Instance().SaveToConfig();
                break;
            }
            case ID_TRAY_EXIT:
                PostQuitMessage(0);
                break;
        }
    }

    void AppendFolderColorMenuItems(HMENU menu) {
        AppendStyledMenuItem(menu, ID_MENU_COLOR_AZURE);
        AppendStyledMenuItem(menu, ID_MENU_COLOR_MINT);
        AppendStyledMenuItem(menu, ID_MENU_COLOR_CRIMSON);
        AppendStyledMenuItem(menu, ID_MENU_COLOR_AMBER);
        AppendStyledMenuItem(menu, ID_MENU_COLOR_VIOLET);
        AppendStyledMenuItem(menu, ID_MENU_COLOR_ROSE);
        AppendStyledMenuItem(menu, ID_MENU_COLOR_CYAN);
        AppendStyledMenuItem(menu, ID_MENU_COLOR_SLATE);
        AppendStyledMenuItem(menu, ID_MENU_COLOR_LIME);
        AppendStyledMenuItem(menu, ID_MENU_COLOR_CORAL);
    }

    void AppendFolderSizeMenuItems(HMENU menu) {
        AppendStyledMenuItem(menu, ID_MENU_SIZE_COMPACT);
        AppendStyledMenuItem(menu, ID_MENU_SIZE_SMALL);
        AppendStyledMenuItem(menu, ID_MENU_SIZE_MEDIUM);
        AppendStyledMenuItem(menu, ID_MENU_SIZE_LARGE);
        AppendStyledMenuItem(menu, ID_MENU_SIZE_XL);
    }

    bool ApplyFolderColorCommand(FolderWidget& widget, UINT command) {
        const MenuVisualSpec* spec = GetMenuVisualSpec(command);
        if (!spec || !spec->colorChoice) {
            return false;
        }

        widget.SetColor(spec->swatchColor);
        WidgetManager::Instance().SaveToConfig();
        return true;
    }

    bool ApplyFolderSizeCommand(FolderWidget& widget, UINT command) {
        const MenuVisualSpec* spec = GetMenuVisualSpec(command);
        if (!spec || !spec->sizeChoice) {
            return false;
        }

        widget.SetIconSize(spec->iconSize);
        WidgetManager::Instance().SaveToConfig();
        return true;
    }

    POINT ClampFolderPosition(int desiredX, int desiredY, int iconSize) {
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int slotWidth = GetFolderSlotWidth(iconSize);
        const int slotHeight = GetFolderSlotHeight(iconSize);

        POINT point = { desiredX, desiredY };
        point.x = (std::max)(0L, (std::min)(point.x, static_cast<LONG>(screenWidth - slotWidth)));
        point.y = (std::max)(0L, (std::min)(point.y, static_cast<LONG>(screenHeight - slotHeight)));
        return point;
    }

    bool FolderPositionOverlaps(const FolderWidget* movingWidget, int desiredX, int desiredY, int iconSize) {
        const int movingSlotWidth = GetFolderSlotWidth(iconSize);
        const int movingSlotHeight = GetFolderSlotHeight(iconSize);
        const POINT clamped = ClampFolderPosition(desiredX, desiredY, iconSize);
        RECT candidate = {
            clamped.x - FOLDER_SLOT_GAP / 2,
            clamped.y - FOLDER_SLOT_GAP / 2,
            clamped.x + movingSlotWidth + FOLDER_SLOT_GAP / 2,
            clamped.y + movingSlotHeight + FOLDER_SLOT_GAP / 2
        };

        for (const auto& folder : WidgetManager::Instance().GetFolders()) {
            if (movingWidget && folder.get() == movingWidget) {
                continue;
            }

            const FolderData& folderData = folder->GetData();
            const int existingSlotWidth = GetFolderSlotWidth(folderData.iconSize);
            const int existingSlotHeight = GetFolderSlotHeight(folderData.iconSize);
            RECT existing = {
                folderData.posX - FOLDER_SLOT_GAP / 2,
                folderData.posY - FOLDER_SLOT_GAP / 2,
                folderData.posX + existingSlotWidth + FOLDER_SLOT_GAP / 2,
                folderData.posY + existingSlotHeight + FOLDER_SLOT_GAP / 2
            };
            RECT overlap = {};
            if (IntersectRect(&overlap, &candidate, &existing)) {
                return true;
            }
        }

        return false;
    }

    POINT ResolveFolderPosition(const FolderWidget* movingWidget, int desiredX, int desiredY, int iconSize) {
        POINT origin = ClampFolderPosition(desiredX, desiredY, iconSize);
        if (!FolderPositionOverlaps(movingWidget, origin.x, origin.y, iconSize)) {
            return origin;
        }

        bool found = false;
        POINT best = origin;
        long long bestDistance = 0;
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int maxRadius = (std::max)(screenWidth, screenHeight);

        auto considerCandidate = [&](POINT candidate) {
            candidate = ClampFolderPosition(candidate.x, candidate.y, iconSize);
            if (FolderPositionOverlaps(movingWidget, candidate.x, candidate.y, iconSize)) {
                return;
            }

            const long long dx = static_cast<long long>(candidate.x) - origin.x;
            const long long dy = static_cast<long long>(candidate.y) - origin.y;
            const long long distance = dx * dx + dy * dy;

            if (!found || distance < bestDistance) {
                found = true;
                best = candidate;
                bestDistance = distance;
            }
        };

        for (int radius = FOLDER_SEARCH_STEP; radius <= maxRadius && !found; radius += FOLDER_SEARCH_STEP) {
            for (int dx = -radius; dx <= radius; dx += FOLDER_SEARCH_STEP) {
                considerCandidate(POINT{ origin.x + dx, origin.y - radius });
                considerCandidate(POINT{ origin.x + dx, origin.y + radius });
            }

            for (int dy = -radius + FOLDER_SEARCH_STEP; dy <= radius - FOLDER_SEARCH_STEP; dy += FOLDER_SEARCH_STEP) {
                considerCandidate(POINT{ origin.x - radius, origin.y + dy });
                considerCandidate(POINT{ origin.x + radius, origin.y + dy });
            }
        }

        return found ? best : origin;
    }

    POINT FindNextFolderPosition(int iconSize) {
        constexpr int startX = 28;
        constexpr int startY = 58;
        const int stepX = GetFolderSlotWidth(iconSize) + FOLDER_SLOT_GAP + 8;
        const int stepY = GetFolderSlotHeight(iconSize) + 7;
        const int widgetWidth = GetFolderSlotWidth(iconSize);
        const int widgetHeight = GetFolderSlotHeight(iconSize);

        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int maxX = (std::max)(startX, screenWidth - widgetWidth);
        const int maxY = (std::max)(startY, screenHeight - widgetHeight);

        for (int y = startY; y <= maxY; y += stepY) {
            for (int x = startX; x <= maxX; x += stepX) {
                POINT candidate = ResolveFolderPosition(nullptr, x, y, iconSize);
                if (candidate.x == x && candidate.y == y) {
                    return POINT{ x, y };
                }
            }
        }

        return POINT{ startX, startY };
    }
}

// ============================================
// FolderWidget Implementation
// ============================================

FolderWidget::FolderWidget(const FolderData& data)
    : m_hwnd(nullptr)
    , m_hInstance(nullptr)
    , m_data(data)
    , m_isDragging(false)
    , m_dragStart{ 0, 0 }
    , m_dragStartScreen{ 0, 0 }
    , m_dragOriginPos{ data.posX, data.posY }
    , m_dragMoved(false)
    , m_isResizing(false)
    , m_resizeStartScreen{ 0, 0 }
    , m_resizeStartPaneWidth(0)
    , m_resizeStartPaneHeight(0)
    , m_resizeEdges(RESIZE_EDGE_NONE)
    , m_isDragOver(false)
    , m_hoveredItemIndex(-1)
    , m_clickedItemIndex(-1)
    , m_isLaunchAnimating(false)
    , m_scrollOffset(0)
    , m_expandLeft(false)
{
    m_data.iconSize = NormalizeFolderIconSize(m_data.iconSize);
    m_data.paneWidth = (std::max)(MIN_PANE_WIDTH, m_data.paneWidth);
    m_data.paneHeight = (std::max)(MIN_PANE_HEIGHT, m_data.paneHeight);
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

int FolderWidget::GetIconSize() const {
    return NormalizeFolderIconSize(m_data.iconSize);
}

int FolderWidget::GetCollapsedWidth() const {
    return GetFolderSlotWidth(GetIconSize());
}

int FolderWidget::GetCollapsedHeight() const {
    return GetFolderSlotHeight(GetIconSize());
}

int FolderWidget::GetExpandedPaneWidth() const {
    return (std::max)(MIN_PANE_WIDTH, m_data.paneWidth);
}

int FolderWidget::GetExpandedPaneHeight() const {
    return (std::max)(MIN_PANE_HEIGHT, m_data.paneHeight);
}

int FolderWidget::GetExpandedWidth() const {
    return GetCollapsedWidth() + PANE_GAP + GetExpandedPaneWidth();
}

int FolderWidget::GetExpandedHeight() const {
    return (std::max)(GetCollapsedHeight(), ICON_PADDING * 2 + GetExpandedPaneHeight());
}

int FolderWidget::GetItemColumns() const {
    const int contentWidth = (std::max)(1, GetExpandedPaneWidth() - PANE_PADDING * 2);
    return (std::max)(1, contentWidth / ITEM_MIN_SLOT_WIDTH);
}

int FolderWidget::GetItemSlotWidth() const {
    const int columns = GetItemColumns();
    const int contentWidth = (std::max)(1, GetExpandedPaneWidth() - PANE_PADDING * 2);
    return (std::max)(contentWidth / columns, ITEM_MIN_CARD_WIDTH + 16);
}

int FolderWidget::GetItemCardWidth() const {
    return (std::min)(ITEM_MAX_CARD_WIDTH, (std::max)(ITEM_MIN_CARD_WIDTH, GetItemSlotWidth() - 20));
}

int FolderWidget::GetItemCardHeight() const {
    return GetItemCardWidth() + 12;
}

int FolderWidget::GetItemRowHeight() const {
    return (std::max)(ITEM_MIN_ROW_HEIGHT, GetItemCardHeight() + 8);
}

RECT FolderWidget::GetPaneRect() const {
    const int paneX = m_expandLeft ? ICON_PADDING : (GetCollapsedWidth() + PANE_GAP);
    return RECT{
        paneX,
        ICON_PADDING,
        paneX + GetExpandedPaneWidth(),
        ICON_PADDING + GetExpandedPaneHeight()
    };
}

int FolderWidget::HitTestResizeHandle(int x, int y) const {
    if (!m_data.isExpanded) {
        return RESIZE_EDGE_NONE;
    }

    const RECT paneRect = GetPaneRect();
    int edges = RESIZE_EDGE_NONE;

    const int horizontalEdge = m_expandLeft ? paneRect.left : paneRect.right;
    if (y >= paneRect.top && y <= paneRect.bottom && std::abs(x - horizontalEdge) <= RESIZE_HANDLE_SIZE) {
        edges |= RESIZE_EDGE_HORIZONTAL;
    }

    if (x >= paneRect.left && x <= paneRect.right && std::abs(y - paneRect.bottom) <= RESIZE_HANDLE_SIZE) {
        edges |= RESIZE_EDGE_VERTICAL;
    }

    return edges;
}

int FolderWidget::GetItemIndexAt(int x, int y) {
    if (!m_data.isExpanded) return -1;
    
    const int padding = PANE_PADDING;
    const RECT paneRect = GetPaneRect();
    const int itemWidth = GetItemSlotWidth();
    const int itemHeight = GetItemRowHeight();
    const int cardWidth = GetItemCardWidth();
    const int cardHeight = GetItemCardHeight();
    const int visibleHeight = GetExpandedPaneHeight() - padding * 2;
    const int itemColumns = GetItemColumns();
    
    for (size_t i = 0; i < m_data.items.size(); i++) {
        int col = static_cast<int>(i) % itemColumns;
        int row = static_cast<int>(i) / itemColumns;
        
        int ix = paneRect.left + padding + col * itemWidth + (itemWidth - cardWidth) / 2;
        int iy = paneRect.top + padding + row * itemHeight - m_scrollOffset;
        
        if (iy + itemHeight < ICON_PADDING || iy > ICON_PADDING + GetExpandedPaneHeight()) continue;
        
        if (x >= ix && x < ix + cardWidth && y >= iy && y < iy + cardHeight) {
            return (int)i;
        }
    }
    return -1;
}

void FolderWidget::ShowContextMenu(int x, int y, int itemIndex) {
    HMENU hMenu = CreateStyledPopupMenu();
    
    if (itemIndex != -1) {
        g_activeFolderMenuColor = MENU_NO_COLOR;
        g_activeFolderMenuIconSize = DEFAULT_FOLDER_ICON_SIZE;
        AppendStyledMenuItem(hMenu, ID_MENU_OPEN_LOCATION);
        AppendStyledMenuItem(hMenu, ID_MENU_REMOVE_ITEM);
    } else {
        g_activeFolderMenuColor = m_data.color;
        g_activeFolderMenuIconSize = m_data.iconSize;
        AppendStyledMenuItem(hMenu, ID_MENU_RENAME_FOLDER);
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendFolderSizeMenuItems(hMenu);
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendFolderColorMenuItems(hMenu);
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendStyledMenuItem(hMenu, ID_MENU_DELETE_FOLDER);
    }
    
    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, x, y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
    g_activeFolderMenuColor = MENU_NO_COLOR;
    g_activeFolderMenuIconSize = DEFAULT_FOLDER_ICON_SIZE;
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    
    if (cmd == ID_MENU_RENAME_FOLDER) {
        std::wstring newName;
        if (ShowInputBox(m_hwnd, newName, m_data.name)) {
            SetName(newName);
            WidgetManager::Instance().SaveToConfig();
        }
    } else if (ApplyFolderSizeCommand(*this, cmd) || ApplyFolderColorCommand(*this, cmd)) {
        return;
    } else if (cmd == ID_MENU_DELETE_FOLDER) {
        WidgetManager::Instance().RemoveFolder(m_data.id);
    } else if (cmd == ID_MENU_OPEN_LOCATION) {
        if (itemIndex >= 0 && itemIndex < m_data.items.size()) {
            std::wstring path = m_data.items[itemIndex].path;
            std::wstring folder = path.substr(0, path.find_last_of(L"\\/"));
            ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    } else if (cmd == ID_MENU_REMOVE_ITEM) {
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
    wc.hIcon = GetAppIcon(false);
    wc.hIconSm = GetAppIcon(true);
    
    RegisterClassExW(&wc);
    
    // Calculate window size based on expanded state
    int width = m_data.isExpanded ? GetExpandedWidth() : GetCollapsedWidth();
    int height = m_data.isExpanded ? GetExpandedHeight() : GetCollapsedHeight();
    
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
    POINT clamped = ClampFolderPosition(x, y, GetIconSize());
    m_data.posX = clamped.x;
    m_data.posY = clamped.y;
    
    int windowX = clamped.x;
    if (m_data.isExpanded && m_expandLeft) {
        windowX = clamped.x - GetExpandedPaneWidth() - PANE_GAP;
    }
    SetWindowPos(m_hwnd, nullptr, windowX, clamped.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void FolderWidget::SetExpanded(bool expanded) {
    if (m_data.isExpanded == expanded) return;
    
    m_data.isExpanded = expanded;
    
    // Check screen position for smart expansion
    if (expanded) {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        m_expandLeft = (m_data.posX > screenW / 2);
        SetPaneSize(m_data.paneWidth, m_data.paneHeight);
        return;
    }
    
    SetWindowPos(m_hwnd, nullptr, m_data.posX, m_data.posY, GetCollapsedWidth(), GetCollapsedHeight(), SWP_NOZORDER);
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

void FolderWidget::SetIconSize(int iconSize) {
    const int normalizedSize = NormalizeFolderIconSize(iconSize);
    if (m_data.iconSize == normalizedSize) {
        return;
    }

    m_data.iconSize = normalizedSize;
    POINT resolved = ResolveFolderPosition(this, m_data.posX, m_data.posY, m_data.iconSize);
    m_data.posX = resolved.x;
    m_data.posY = resolved.y;

    SetPaneSize(m_data.paneWidth, m_data.paneHeight);
}

void FolderWidget::SetPaneSize(int paneWidth, int paneHeight) {
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int maxPaneWidth = m_expandLeft
        ? (std::max)(MIN_PANE_WIDTH, m_data.posX - PANE_GAP)
        : (std::max)(MIN_PANE_WIDTH, screenWidth - m_data.posX - GetCollapsedWidth() - PANE_GAP);
    const int maxPaneHeight = (std::max)(MIN_PANE_HEIGHT, screenHeight - m_data.posY - ICON_PADDING * 2);

    m_data.paneWidth = (std::max)(MIN_PANE_WIDTH, (std::min)(paneWidth, maxPaneWidth));
    m_data.paneHeight = (std::max)(MIN_PANE_HEIGHT, (std::min)(paneHeight, maxPaneHeight));

    const int width = m_data.isExpanded ? GetExpandedWidth() : GetCollapsedWidth();
    const int height = m_data.isExpanded ? GetExpandedHeight() : GetCollapsedHeight();
    int windowX = m_data.posX;
    if (m_data.isExpanded && m_expandLeft) {
        windowX = m_data.posX - GetExpandedPaneWidth() - PANE_GAP;
    }

    SetWindowPos(m_hwnd, nullptr, windowX, m_data.posY, width, height, SWP_NOZORDER);
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
    const int iconSize = GetIconSize();
    int folderX = ICON_PADDING;
    if (m_data.isExpanded && m_expandLeft) {
        folderX = ICON_PADDING + GetExpandedPaneWidth() + PANE_GAP;
    }
    RenderFolderIcon(g, folderX, ICON_PADDING);
    
    // Draw expanded pane if open
    if (m_data.isExpanded) {
        int paneX = m_expandLeft ? ICON_PADDING : (GetCollapsedWidth() + PANE_GAP);
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
    const int iconSize = GetIconSize();
    BYTE r = GetRValue(m_data.color);
    BYTE gb = GetGValue(m_data.color);
    BYTE b = GetBValue(m_data.color);
    
    // Folder body
    LinearGradientBrush bodyBrush(
        Point(x, y),
        Point(x + iconSize, y + iconSize),
        Color(230, r, gb, b),
        Color(200, (BYTE)(r * 0.7), (BYTE)(gb * 0.7), (BYTE)(b * 0.7))
    );
    
    // Folder shape
    GraphicsPath path;
    const int tabWidth = (std::max)(14, MulDiv(iconSize, 21, 64));
    const int tabHeight = (std::max)(6, MulDiv(iconSize, 8, 64));
    const int tabLift = (std::max)(5, MulDiv(iconSize, 8, 64));
    const int cornerRadius = (std::max)(6, MulDiv(iconSize, 8, 64));
    const int bottomInset = (std::max)(8, MulDiv(iconSize, 12, 64));
    
    // Tab
    path.AddLine(x + cornerRadius, y, x + tabWidth, y);
    path.AddLine(x + tabWidth, y, x + tabWidth + tabLift, y + tabHeight);
    path.AddLine(x + tabWidth + tabLift, y + tabHeight, x + iconSize - cornerRadius, y + tabHeight);
    
    // Right side
    path.AddArc(x + iconSize - cornerRadius * 2, y + tabHeight, cornerRadius * 2, cornerRadius * 2, 270, 90);
    path.AddLine(x + iconSize, y + tabHeight + cornerRadius, x + iconSize, y + iconSize - bottomInset - cornerRadius);
    path.AddArc(x + iconSize - cornerRadius * 2, y + iconSize - bottomInset - cornerRadius * 2, cornerRadius * 2, cornerRadius * 2, 0, 90);
    
    // Bottom
    path.AddLine(x + iconSize - cornerRadius, y + iconSize - bottomInset, x + cornerRadius, y + iconSize - bottomInset);
    path.AddArc(x, y + iconSize - bottomInset - cornerRadius * 2, cornerRadius * 2, cornerRadius * 2, 90, 90);
    
    // Left side
    path.AddLine(x, y + iconSize - bottomInset - cornerRadius, x, y + cornerRadius);
    path.AddArc(x, y, cornerRadius * 2, cornerRadius * 2, 180, 90);
    
    path.CloseFigure();
    
    g.FillPath(&bodyBrush, &path);
    
    // Folder lines (content indicator)
    Pen linePen(Color(80, 255, 255, 255), static_cast<REAL>((std::max)(1, MulDiv(iconSize, 2, 64))));
    const int lineInset1 = (std::max)(8, MulDiv(iconSize, 16, 64));
    const int lineInset2 = (std::max)(10, MulDiv(iconSize, 20, 64));
    const int lineInset3 = (std::max)(9, MulDiv(iconSize, 18, 64));
    const int lineY1 = y + (std::max)(18, MulDiv(iconSize, 28, 64));
    const int lineY2 = y + (std::max)(24, MulDiv(iconSize, 36, 64));
    const int lineY3 = y + (std::max)(30, MulDiv(iconSize, 44, 64));
    g.DrawLine(&linePen, x + lineInset1, lineY1, x + iconSize - lineInset1, lineY1);
    g.DrawLine(&linePen, x + lineInset2, lineY2, x + iconSize - lineInset2, lineY2);
    g.DrawLine(&linePen, x + lineInset3, lineY3, x + iconSize - lineInset3, lineY3);
    
    // Drop shadow effect
    if (m_isDragOver) {
        Pen glowPen(Color(150, r, gb, b), 3);
        g.DrawPath(&glowPen, &path);
    }
    
    // Item count badge
    if (!m_data.items.empty()) {
        const int badgeSize = (std::max)(14, MulDiv(iconSize, 20, 64));
        const int badgeX = x + iconSize - badgeSize + (std::max)(3, MulDiv(iconSize, 4, 64));
        const int badgeY = y - badgeSize / 5;
        SolidBrush badgeBrush(Color(255, r, gb, b));
        g.FillEllipse(&badgeBrush, badgeX, badgeY, badgeSize, badgeSize);
        
        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, static_cast<REAL>((std::max)(8, MulDiv(iconSize, 10, 64))), FontStyleBold, UnitPixel);
        SolidBrush textBrush(Color(255, 255, 255, 255));
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        
        std::wstring countStr = std::to_wstring(m_data.items.size());
        RectF badgeRect(static_cast<REAL>(badgeX), static_cast<REAL>(badgeY), static_cast<REAL>(badgeSize), static_cast<REAL>(badgeSize));
        g.DrawString(countStr.c_str(), -1, &font, badgeRect, &sf, &textBrush);
    }
    
    // Folder name label
    FontFamily fontFamily(L"Segoe UI");
    Font font(&fontFamily, static_cast<REAL>((std::max)(12, iconSize / 5)), FontStyleBold, UnitPixel);
    SolidBrush shadowBrush(Color(180, 8, 12, 20));
    SolidBrush textBrush(Color(255, 248, 250, 252));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    
    RectF textRect(static_cast<REAL>(x - 8), static_cast<REAL>(y + iconSize - 4), static_cast<REAL>(iconSize + 16), 30.0f);
    RectF shadowRect = textRect;
    shadowRect.X += 1.0f;
    shadowRect.Y += 1.0f;
    g.DrawString(m_data.name.c_str(), -1, &font, shadowRect, &sf, &shadowBrush);
    g.DrawString(m_data.name.c_str(), -1, &font, textRect, &sf, &textBrush);
}

void FolderWidget::RenderExpandedPane(Graphics& g, int x, int y) {
    const int paneWidth = GetExpandedPaneWidth();
    const int paneHeight = GetExpandedPaneHeight();
    const int itemColumns = GetItemColumns();
    BYTE r = GetRValue(m_data.color);
    BYTE gb = GetGValue(m_data.color);
    BYTE b = GetBValue(m_data.color);
    
    // Glassmorphism background
    LinearGradientBrush bgBrush(
        Point(x, y),
        Point(x + paneWidth, y + paneHeight),
        Color(200, (BYTE)(r * 0.15 + 30), (BYTE)(gb * 0.15 + 30), (BYTE)(b * 0.15 + 40)),
        Color(180, (BYTE)(r * 0.1 + 20), (BYTE)(gb * 0.1 + 20), (BYTE)(b * 0.1 + 30))
    );
    
    GraphicsPath bgPath;
    int radius = 16;
    bgPath.AddArc(x, y, radius * 2, radius * 2, 180, 90);
    bgPath.AddArc(x + paneWidth - radius * 2, y, radius * 2, radius * 2, 270, 90);
    bgPath.AddArc(x + paneWidth - radius * 2, y + paneHeight - radius * 2, radius * 2, radius * 2, 0, 90);
    bgPath.AddArc(x, y + paneHeight - radius * 2, radius * 2, radius * 2, 90, 90);
    bgPath.CloseFigure();
    
    g.FillPath(&bgBrush, &bgPath);
    
    // Border
    Pen borderPen(Color(60, r, gb, b), 1);
    g.DrawPath(&borderPen, &bgPath);
    
    // Items grid and Scrollbar
    const int padding = PANE_PADDING;
    int itemX = x + padding;
    int itemY = y + padding;
    int visibleHeight = paneHeight - padding * 2;
    const int itemWidth = GetItemSlotWidth();
    const int itemHeight = GetItemRowHeight();
    const int cardWidth = GetItemCardWidth();
    const int cardHeight = GetItemCardHeight();
    
    // Calculate total height
    int totalRows = static_cast<int>((m_data.items.size() + itemColumns - 1) / itemColumns);
    int totalHeight = totalRows * itemHeight;
    
    // Update scroll clamp
    int maxScroll = (std::max)(0, totalHeight - visibleHeight);
    if (m_scrollOffset > maxScroll) m_scrollOffset = maxScroll;
    if (m_scrollOffset < 0) m_scrollOffset = 0;
    
    // Clip to content area
    Region originalClip;
    g.GetClip(&originalClip);
    Rect clipRect(x, y + 4, paneWidth, visibleHeight);
    g.SetClip(clipRect);

    if (m_data.items.empty()) {
        // Empty state
        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, 13, FontStyleRegular, UnitPixel);
        SolidBrush textBrush(Color(200, 200, 200, 200));
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        
        RectF textRect((float)x, (float)y, (float)paneWidth, (float)paneHeight);
        g.DrawString(L"Drag and drop files", -1, &font, textRect, &sf, &textBrush); // Removed emoji
    } else {
        for (size_t i = 0; i < m_data.items.size(); i++) {
            int col = static_cast<int>(i) % itemColumns;
            int row = static_cast<int>(i) / itemColumns;
            
            int ix = itemX + col * itemWidth + (itemWidth - cardWidth) / 2;
            int iy = itemY + row * itemHeight - m_scrollOffset;
            
            // Optimization: Don't render if out of view
            if (iy + itemHeight < y || iy > y + visibleHeight) continue;

            RenderItem(g, m_data.items[i], ix, iy, cardWidth, cardHeight);
        }
    }
    
    g.SetClip(&originalClip);
    
    // Draw Scrollbar if needed
    if (maxScroll > 0) {
        int sbWidth = 6;
        int sbHeight = (int)((float)visibleHeight * ((float)visibleHeight / (float)totalHeight));
        if (sbHeight < 30) sbHeight = 30;
        
        int sbX = x + paneWidth - sbWidth - 4;
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
        int iconSize = (std::min)(40, (std::max)(28, width - 24));
        int iconX = x + (width - iconSize) / 2;
        int iconY = y + (height - 20 - iconSize) / 2;
        g.DrawImage(icon, iconX, iconY, iconSize, iconSize);
    } else {
        // Fallback emoji
        FontFamily fontFamily(L"Segoe UI Emoji");
        Font iconFont(&fontFamily, static_cast<REAL>((std::min)(34, (std::max)(24, width - 24))), FontStyleRegular, UnitPixel);
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
    HMENU hMenu = CreateStyledPopupMenu();
    g_activeFolderMenuColor = m_data.color;
    g_activeFolderMenuIconSize = m_data.iconSize;
    
    AppendStyledMenuItem(hMenu, ID_MENU_RENAME_FOLDER);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendFolderSizeMenuItems(hMenu);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendFolderColorMenuItems(hMenu);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendStyledMenuItem(hMenu, ID_MENU_DELETE_FOLDER);
    
    POINT pt = { x, y };
    ClientToScreen(m_hwnd, &pt);
    
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    
    DestroyMenu(hMenu);
    g_activeFolderMenuColor = MENU_NO_COLOR;
    g_activeFolderMenuIconSize = DEFAULT_FOLDER_ICON_SIZE;
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    
    if (cmd == ID_MENU_RENAME_FOLDER) {
        std::wstring newName;
        if (ShowInputBox(m_hwnd, newName, m_data.name)) {
            SetName(newName);
            WidgetManager::Instance().SaveToConfig();
        }
    } else if (ApplyFolderSizeCommand(*this, cmd) || ApplyFolderColorCommand(*this, cmd)) {
        return;
    } else if (cmd == ID_MENU_DELETE_FOLDER) {
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

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                POINT cursorPos = {};
                GetCursorPos(&cursorPos);
                ScreenToClient(hwnd, &cursorPos);

                const int resizeEdges = widget->m_isResizing ? widget->m_resizeEdges : widget->HitTestResizeHandle(cursorPos.x, cursorPos.y);
                if (resizeEdges != RESIZE_EDGE_NONE) {
                    LPCWSTR cursorId = IDC_ARROW;
                    if (resizeEdges == (RESIZE_EDGE_HORIZONTAL | RESIZE_EDGE_VERTICAL)) {
                        cursorId = widget->m_expandLeft ? IDC_SIZENESW : IDC_SIZENWSE;
                    } else if ((resizeEdges & RESIZE_EDGE_HORIZONTAL) != 0) {
                        cursorId = IDC_SIZEWE;
                    } else if ((resizeEdges & RESIZE_EDGE_VERTICAL) != 0) {
                        cursorId = IDC_SIZENS;
                    }

                    SetCursor(LoadCursor(nullptr, cursorId));
                    return TRUE;
                }
            }
            break;
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

        case WM_MEASUREITEM:
            if (HandleStyledMenuMeasure(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) {
                return TRUE;
            }
            break;

        case WM_DRAWITEM:
            if (HandleStyledMenuDraw(reinterpret_cast<DRAWITEMSTRUCT*>(lParam))) {
                return TRUE;
            }
            break;

        case WM_DESTROY: {
            return 0;
        }
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void FolderWidget::OnMouseDown(int x, int y, bool rightClick) {
    // Check click on folder icon
    const int iconSize = GetIconSize();
    int folderX = m_expandLeft && m_data.isExpanded ? (ICON_PADDING + GetExpandedPaneWidth() + PANE_GAP) : ICON_PADDING;
    int folderY = ICON_PADDING;

    const int resizeEdges = HitTestResizeHandle(x, y);
    if (resizeEdges != RESIZE_EDGE_NONE) {
        m_isResizing = true;
        m_resizeEdges = resizeEdges;
        GetCursorPos(&m_resizeStartScreen);
        m_resizeStartPaneWidth = GetExpandedPaneWidth();
        m_resizeStartPaneHeight = GetExpandedPaneHeight();
        SetCapture(m_hwnd);
        return;
    }
    
    if (x >= folderX && x < folderX + iconSize && y >= folderY && y < folderY + iconSize) {
        m_isDragging = true;
        m_dragStart = { x, y };
        GetCursorPos(&m_dragStartScreen);
        m_dragOriginPos = { m_data.posX, m_data.posY };
        m_dragMoved = false;
        SetCapture(m_hwnd);
    } else if (m_data.isExpanded) {
        // Check click on items
        const int padding = PANE_PADDING;
        const RECT paneRect = GetPaneRect();
        const int itemWidth = GetItemSlotWidth();
        const int itemHeight = GetItemRowHeight();
        const int cardWidth = GetItemCardWidth();
        const int cardHeight = GetItemCardHeight();
        const int itemColumns = GetItemColumns();
        
        for (size_t i = 0; i < m_data.items.size(); i++) {
            int col = static_cast<int>(i) % itemColumns;
            int row = static_cast<int>(i) / itemColumns;
            
            int ix = paneRect.left + padding + col * itemWidth + (itemWidth - cardWidth) / 2;
            int iy = paneRect.top + padding + row * itemHeight - m_scrollOffset;
            
            // Check visibility
            if (iy + itemHeight < ICON_PADDING || iy > ICON_PADDING + GetExpandedPaneHeight()) continue;

            if (x >= ix && x < ix + cardWidth && y >= iy && y < iy + cardHeight) {
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
    
    const int padding = PANE_PADDING;
    const int itemHeight = GetItemRowHeight();
    const int visibleHeight = GetExpandedPaneHeight() - padding * 2;
    const int itemColumns = GetItemColumns();
    int totalRows = (int)((m_data.items.size() + itemColumns - 1) / itemColumns);
    int totalHeight = totalRows * itemHeight;
    int maxScroll = (std::max)(0, totalHeight - visibleHeight);
    
    if (m_scrollOffset > maxScroll) m_scrollOffset = maxScroll;
    if (m_scrollOffset < 0) m_scrollOffset = 0;
    
    Render();
}

void FolderWidget::OnMouseUp(int x, int y) {
    if (m_isResizing) {
        m_isResizing = false;
        m_resizeEdges = RESIZE_EDGE_NONE;
        ReleaseCapture();
        WidgetManager::Instance().SaveToConfig();
        return;
    }

    if (m_isDragging) {
        m_isDragging = false;
        ReleaseCapture();
        
        // A short press on the folder icon toggles expansion. Real drags are evaluated on drop.
        if (!m_dragMoved) {
            UpdatePosition(m_dragOriginPos.x, m_dragOriginPos.y);
            SetExpanded(!m_data.isExpanded);
        } else if (FolderPositionOverlaps(this, m_data.posX, m_data.posY, m_data.iconSize)) {
            UpdatePosition(m_dragOriginPos.x, m_dragOriginPos.y);
        }
        
        WidgetManager::Instance().SaveToConfig();
    }
}

void FolderWidget::OnMouseMove(int x, int y) {
    if (m_isResizing) {
        POINT cursorPos = {};
        GetCursorPos(&cursorPos);

        int newPaneWidth = m_resizeStartPaneWidth;
        int newPaneHeight = m_resizeStartPaneHeight;

        if ((m_resizeEdges & RESIZE_EDGE_HORIZONTAL) != 0) {
            const int deltaX = cursorPos.x - m_resizeStartScreen.x;
            newPaneWidth += m_expandLeft ? -deltaX : deltaX;
        }

        if ((m_resizeEdges & RESIZE_EDGE_VERTICAL) != 0) {
            const int deltaY = cursorPos.y - m_resizeStartScreen.y;
            newPaneHeight += deltaY;
        }

        SetPaneSize(newPaneWidth, newPaneHeight);
        return;
    }

    if (m_isDragging) {
        POINT cursorPos;
        GetCursorPos(&cursorPos);

        const int dragDeltaX = cursorPos.x - m_dragStartScreen.x;
        const int dragDeltaY = cursorPos.y - m_dragStartScreen.y;
        if (!m_dragMoved && (std::abs(dragDeltaX) >= 6 || std::abs(dragDeltaY) >= 6)) {
            m_dragMoved = true;
        }
        
        int newX = cursorPos.x - m_dragStart.x;
        int newY = cursorPos.y - m_dragStart.y;
        
        // Fix for Smart Expansion Dragging:
        // newX is the Window Top-Left. UpdatePosition expects Anchor position.
        int anchorX = newX;
        if (m_data.isExpanded && m_expandLeft) {
            anchorX += (GetExpandedPaneWidth() + PANE_GAP);
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
    EnsureUiAssets();
    
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
    ReleaseUiAssets();
    GdiplusShutdown(g_gdiplusToken);
}

FolderWidget* WidgetManager::CreateFolder(const FolderData& data) {
    FolderData adjustedData = data;
    adjustedData.iconSize = NormalizeFolderIconSize(adjustedData.iconSize);
    adjustedData.paneWidth = (std::max)(MIN_PANE_WIDTH, adjustedData.paneWidth);
    adjustedData.paneHeight = (std::max)(MIN_PANE_HEIGHT, adjustedData.paneHeight);
    POINT resolved = ResolveFolderPosition(nullptr, adjustedData.posX, adjustedData.posY, adjustedData.iconSize);
    adjustedData.posX = resolved.x;
    adjustedData.posY = resolved.y;

    auto widget = std::make_unique<FolderWidget>(adjustedData);
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
        Bitmap* bmp = CreateBitmapFromIcon(sfi.hIcon, 64);
        DestroyIcon(sfi.hIcon);
        if (bmp) {
            m_iconCache[path] = bmp;
            return bmp;
        }
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
        data.iconSize = NormalizeFolderIconSize(cfg.iconSize);
        data.paneWidth = (std::max)(MIN_PANE_WIDTH, cfg.paneWidth);
        data.paneHeight = (std::max)(MIN_PANE_HEIGHT, cfg.paneHeight);
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

    if (!configs.empty()) {
        SaveToConfig();
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
        cfg.iconSize = data.iconSize;
        cfg.paneWidth = data.paneWidth;
        cfg.paneHeight = data.paneHeight;
        
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
        case WM_TRAYICON: {
            const UINT notifyEvent = LOWORD(lParam);

            if (notifyEvent == WM_CONTEXTMENU) {
                POINT anchor = {
                    static_cast<LONG>(static_cast<SHORT>(LOWORD(wParam))),
                    static_cast<LONG>(static_cast<SHORT>(HIWORD(wParam)))
                };
                WidgetManager::Instance().ShowTrayMenu(anchor);
            } else if (notifyEvent == WM_RBUTTONUP || notifyEvent == WM_LBUTTONUP ||
                       notifyEvent == NIN_SELECT || notifyEvent == NIN_KEYSELECT) {
                WidgetManager::Instance().ShowTrayMenu();
            }
            return 0;
        }

        case WM_MEASUREITEM:
            if (HandleStyledMenuMeasure(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) {
                return TRUE;
            }
            break;

        case WM_DRAWITEM:
            if (HandleStyledMenuDraw(reinterpret_cast<DRAWITEMSTRUCT*>(lParam))) {
                return TRUE;
            }
            break;
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
    wc.hIcon = GetAppIcon(false);
    wc.hIconSm = GetAppIcon(true);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    
    m_trayHwnd = CreateWindowExW(0, TRAY_CLASS, L"TrayWindow", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, m_hInstance, nullptr);
    
    // Create tray icon
    ZeroMemory(&m_trayIcon, sizeof(m_trayIcon));
    m_trayIcon.cbSize = sizeof(m_trayIcon);
    m_trayIcon.hWnd = m_trayHwnd;
    m_trayIcon.uID = 1;
    m_trayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_trayIcon.uCallbackMessage = WM_TRAYICON;
    m_trayIcon.hIcon = GetAppIcon(true);
    m_trayIcon.uVersion = NOTIFYICON_VERSION_4;
    wcscpy_s(m_trayIcon.szTip, APP_TITLE);
    
    Shell_NotifyIconW(NIM_ADD, &m_trayIcon);
    Shell_NotifyIconW(NIM_SETVERSION, &m_trayIcon);
}

void WidgetManager::RemoveSystemTray() {
    Shell_NotifyIconW(NIM_DELETE, &m_trayIcon);
    DestroyWindow(m_trayHwnd);
}

void WidgetManager::ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    ShowTrayMenu(pt);
}

void WidgetManager::ShowTrayMenu(POINT anchor) {
    HMENU hMenu = CreateStyledPopupMenu();

    AppendStyledMenuItem(hMenu, ID_TRAY_NEW_FOLDER);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendStyledMenuItem(hMenu, ID_TRAY_EXIT);

    SetForegroundWindow(m_trayHwnd);
    UINT command = TrackPopupMenu(
        hMenu,
        TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        anchor.x,
        anchor.y,
        0,
        m_trayHwnd,
        nullptr
    );

    DestroyMenu(hMenu);
    PostMessageW(m_trayHwnd, WM_NULL, 0, 0);

    if (command != 0) {
        ExecuteTrayCommand(command);
    }
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
