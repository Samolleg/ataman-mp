#pragma once

#include <QWidget>
#include <QLibrary>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// Resolve at runtime so unsupported Windows versions keep their native frame.
inline bool applyGraphiteCaption(WId window)
{
#ifdef Q_OS_WIN
    typedef HRESULT (WINAPI *SetAttribute)(HWND, DWORD, LPCVOID, DWORD);
    static QLibrary dwm(QStringLiteral("dwmapi"));
    static const auto setAttribute = reinterpret_cast<SetAttribute>(
        dwm.resolve("DwmSetWindowAttribute"));
    if(!window || !setAttribute) return false;
    const HWND hwnd = reinterpret_cast<HWND>(window);
    const BOOL dark = TRUE;
    setAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const COLORREF caption = RGB(48, 50, 53);
    const COLORREF text = RGB(238, 238, 238);
    const HRESULT backgroundResult = setAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */,
                                                  &caption, sizeof(caption));
    const HRESULT textResult = setAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */,
                                            &text, sizeof(text));
    return SUCCEEDED(backgroundResult) && SUCCEEDED(textResult);
#else
    Q_UNUSED(window);
    return false;
#endif
}
