#pragma once

// Shared input handling for the normal chat and its screen-view overlay.
inline bool ChatWindowInput(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam)
{
    switch(message) {
    case WM_KILLFOCUS:
    case WM_CANCELMODE:
        input.Clear();
        if (GetCapture() == hwnd) ReleaseCapture();
        return false;
    case WM_CAPTURECHANGED:
        input.KeyUp(VK_LBUTTON);
        return true;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        input.KeyDown((unsigned char)wParam);
        return true;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        input.KeyUp((unsigned char)wParam);
        return true;
    case WM_CHAR:
        if (wParam >= 32 && wParam < 127)
        {
            input.TextInput((unsigned char)wParam);
        }
        return true;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetFocus(hwnd);
        SetCapture(hwnd);
        input.KeyDown(VK_LBUTTON);
        input.mouse.x = (short)LOWORD(lParam);
        input.mouse.y = (short)HIWORD(lParam);
        return true;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) ReleaseCapture();
        input.KeyUp(VK_LBUTTON);
        return true;
    case WM_MOUSEMOVE:
        input.mouse.x = (short)LOWORD(lParam);
        input.mouse.y = (short)HIWORD(lParam);
        return true;
    case WM_MOUSEWHEEL:
        input.AddMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return true;
    default:return false;
    }
}
