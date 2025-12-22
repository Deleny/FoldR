// Desktop Folder Widget - C++ Win32 Application
// Entry Point

#include "FolderWidget.h"
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // Initialize Common Controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);
    
    // Initialize COM (for drag & drop and shell operations)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    
    // Initialize Widget Manager
    WidgetManager::Instance().Initialize(hInstance);
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    WidgetManager::Instance().Shutdown();
    CoUninitialize();
    
    return static_cast<int>(msg.wParam);
}
