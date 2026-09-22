#pragma once
#include <commdlg.h>
#include <objbase.h>
#include <mutex>
#include <thread>
#include <memory>

// Only the picker runs on this worker; networking, file I/O and voice stay on the app thread.
class FileSaveDialog {
    struct Result { std::mutex mutex; bool done = false; std::wstring path; int peer; string id; };
    std::shared_ptr<Result> pending;
public:
    bool start(int peer, const string& id, const std::wstring& suggested) {
        if (pending) return false;
        auto result = std::make_shared<Result>(); result->peer = peer; result->id = id; pending = result;
        std::thread([result, suggested] {
            const HRESULT com = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            std::vector<wchar_t> name(32768,0);
            wcsncpy_s(name.data(),name.size(),suggested.c_str(),_TRUNCATE);
            OPENFILENAMEW dialog = {}; dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFile = name.data(); dialog.nMaxFile = (DWORD)name.size();
            dialog.lpstrFilter = L"All files\0*.*\0\0";
            dialog.lpstrTitle = L"Save received file (choose a new filename)";
            dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
            const bool selected = GetSaveFileNameW(&dialog) != FALSE;
            if (SUCCEEDED(com)) CoUninitialize();
            std::lock_guard<std::mutex> lock(result->mutex);
            if (selected) result->path = name.data();
            result->done = true;
        }).detach();
        return true;
    }
    bool poll(int& peer, string& id, std::wstring& path) {
        auto result = pending; if (!result) return false;
        std::lock_guard<std::mutex> lock(result->mutex);
        if (!result->done) return false;
        peer = result->peer; id = result->id; path = result->path; pending.reset(); return true;
    }
};
