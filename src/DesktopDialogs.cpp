// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <oleidl.h>
#include "DesktopDialogs.hpp"
#include "Host.hpp"
#include <algorithm>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace GakuLoader::DesktopDialogs {
namespace {
constexpr size_t MaxTickets = 16, MaxFilters = 16, MaxText = 256;
constexpr UINT_PTR CancelTimerId = 0x474D4C44; // private timer on the GML desktop HWND
struct Filter { std::wstring name, pattern; };
struct Request {
    void* owner{};
    uint64_t ticket{};
    std::wstring title;
    std::vector<Filter> filters;
    std::atomic<bool> cancel{false};
    uint32_t state = GAKU_FILE_DIALOG_PENDING;
    HRESULT error = S_OK;
    std::string path;
};
std::mutex g_mutex;
std::unordered_map<uint64_t, std::shared_ptr<Request>> g_tickets;
std::deque<std::shared_ptr<Request>> g_pending;
std::unordered_set<void*> g_retired;
std::atomic<bool> g_stopping{false};
uint64_t g_nextTicket = 1;

bool CopyText(const wchar_t* input, std::wstring& output) {
    if (!input) return false;
    size_t length = 0;
    while (length <= MaxText && input[length]) ++length;
    if (!length || length > MaxText) return false;
    output.assign(input, length);
    return true;
}

GakuStatus BeginImpl(void* owner, const GakuFileDialogRequest* input, uint64_t* ticket) {
    if (!owner || !input || !ticket) return GAKU_E_INVALID_ARGUMENT;
    if (input->abiVersion != GAKU_SERVICE_VERSION) return GAKU_E_VERSION;
    if (input->structSize < sizeof(*input)) return GAKU_E_SIZE;
    if (!input->filters || !input->filterCount || input->filterCount > MaxFilters)
        return GAKU_E_INVALID_ARGUMENT;
    auto request = std::make_shared<Request>();
    request->owner = owner;
    if (!CopyText(input->title, request->title)) return GAKU_E_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < input->filterCount; ++i) {
        Filter filter;
        if (!CopyText(input->filters[i].name, filter.name) ||
            !CopyText(input->filters[i].pattern, filter.pattern)) return GAKU_E_INVALID_ARGUMENT;
        request->filters.push_back(std::move(filter));
    }
    std::lock_guard lock(g_mutex);
    if (g_stopping || g_retired.contains(owner)) return GAKU_E_SHUTTING_DOWN;
    for (const auto& [id, entry] : g_tickets)
        if (entry->owner == owner) return GAKU_E_CONFLICT;
    if (g_tickets.size() >= MaxTickets) return GAKU_E_LIMIT;
    request->ticket = g_nextTicket++;
    // Commit both containers before exposing the token. Roll back on allocation
    // failure so the owner is never left holding an unreturnable request.
    g_tickets.emplace(request->ticket, request);
    try { g_pending.push_back(request); }
    catch (...) { g_tickets.erase(request->ticket); throw; }
    *ticket = request->ticket;
    return GAKU_OK;
}
GakuStatus Begin(void* owner, const GakuFileDialogRequest* input, uint64_t* ticket) {
    try { return BeginImpl(owner, input, ticket); }
    catch (...) { return GAKU_E_INTERNAL; }
}

GakuStatus Poll(void* owner, uint64_t ticket, uint32_t size, GakuFileDialogResult* output,
               uint32_t capacity, char* path) {
    if (!owner || !ticket || !output || (capacity && !path) || (!capacity && path))
        return GAKU_E_INVALID_ARGUMENT;
    if (size < sizeof(*output)) return GAKU_E_SIZE;
    std::lock_guard lock(g_mutex);
    const auto found = g_tickets.find(ticket);
    if (found == g_tickets.end() || found->second->owner != owner) return GAKU_E_INVALID_ARGUMENT;
    const auto& request = *found->second;
    const uint32_t bytes = request.state == GAKU_FILE_DIALOG_ACCEPTED
        ? static_cast<uint32_t>(request.path.size() + 1) : 0;
    if (capacity && bytes > capacity) return GAKU_E_SIZE;
    const GakuFileDialogResult result{GAKU_SERVICE_VERSION, sizeof(GakuFileDialogResult),
        request.state, static_cast<int32_t>(request.error), bytes};
    if (capacity && bytes) std::copy_n(request.path.c_str(), bytes, path);
    *output = result;
    return GAKU_OK;
}

GakuStatus Release(void* owner, uint64_t ticket) {
    std::lock_guard lock(g_mutex);
    const auto found = g_tickets.find(ticket);
    if (!owner || !ticket || found == g_tickets.end() || found->second->owner != owner)
        return GAKU_E_INVALID_ARGUMENT;
    found->second->cancel = true;
    std::erase_if(g_pending, [ticket](const auto& request) { return request->ticket == ticket; });
    g_tickets.erase(found);
    return GAKU_OK;
}

struct ActiveDialog { IFileOpenDialog* dialog; const Request* request; HWND owner; bool closePosted = false; };
thread_local ActiveDialog* g_active = nullptr;
void CALLBACK CancelTimer(HWND, UINT, UINT_PTR, DWORD) {
    if (!g_active || g_active->closePosted || (!g_stopping && !g_active->request->cancel)) return;
    // Close() is not reliable from an unrelated timer callback (outside the
    // dialog's IFileDialogEvents dispatch). Post the standard close message to
    // this COM object's native dialog, never to its owner/game window.
    IOleWindow* oleWindow = nullptr;
    if (SUCCEEDED(g_active->dialog->QueryInterface(IID_IOleWindow, reinterpret_cast<void**>(&oleWindow)))) {
        HWND window = nullptr;
        const HRESULT status = oleWindow->GetWindow(&window);
        oleWindow->Release();
        DWORD process = 0;
        if (SUCCEEDED(status) && window && window != g_active->owner &&
            GetWindowThreadProcessId(window, &process) == GetCurrentThreadId() &&
            process == GetCurrentProcessId() && GetWindow(window, GW_OWNER) == g_active->owner)
            g_active->closePosted = PostMessageW(window, WM_CLOSE, 0, 0) != FALSE;
    }
}
struct Apartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~Apartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};
struct ComRelease {
    template<class T> void operator()(T* pointer) const { if (pointer) pointer->Release(); }
};
struct TaskFree { void operator()(wchar_t* pointer) const { CoTaskMemFree(pointer); } };

HRESULT Show(const Request& request, HWND owner, std::string& path) {
    if (!IsWindow(owner)) return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    const Apartment apartment;
    if (FAILED(apartment.status)) return apartment.status;
    IFileOpenDialog* rawDialog = nullptr;
    HRESULT status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IFileOpenDialog, reinterpret_cast<void**>(&rawDialog));
    const std::unique_ptr<IFileOpenDialog, ComRelease> dialog(rawDialog);
    if (SUCCEEDED(status)) {
        std::vector<COMDLG_FILTERSPEC> filters;
        for (const auto& filter : request.filters)
            filters.push_back({filter.name.c_str(), filter.pattern.c_str()});
        FILEOPENDIALOGOPTIONS options{};
        status = dialog->GetOptions(&options);
        if (SUCCEEDED(status)) status = dialog->SetOptions(options | FOS_FORCEFILESYSTEM |
            FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | FOS_DONTADDTORECENT);
        if (SUCCEEDED(status)) status = dialog->SetTitle(request.title.c_str());
        if (SUCCEEDED(status)) status = dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
        ActiveDialog active{dialog.get(), &request, owner};
        g_active = &active;
        // The owner remains alive until Show unwinds and UI teardown completes.
        const UINT_PTR timer = SetTimer(owner, CancelTimerId, 50, CancelTimer);
        if (SUCCEEDED(status) && !timer) status = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY);
        if (SUCCEEDED(status)) {
            if (g_stopping || request.cancel) status = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            else status = dialog->Show(owner);
        }
        if (timer) KillTimer(owner, timer);
        g_active = nullptr;
        if (SUCCEEDED(status)) {
            IShellItem* rawItem = nullptr;
            status = dialog->GetResult(&rawItem);
            const std::unique_ptr<IShellItem, ComRelease> item(rawItem);
            if (SUCCEEDED(status)) {
                PWSTR wide = nullptr;
                status = item->GetDisplayName(SIGDN_FILESYSPATH, &wide);
                const std::unique_ptr<wchar_t, TaskFree> allocatedPath(wide);
                if (SUCCEEDED(status)) {
                    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, nullptr, 0, nullptr, nullptr);
                    if (!bytes) status = HRESULT_FROM_WIN32(GetLastError());
                    else {
                        std::vector<char> buffer(static_cast<size_t>(bytes));
                        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, buffer.data(), bytes, nullptr, nullptr))
                            status = HRESULT_FROM_WIN32(GetLastError());
                        else path.assign(buffer.data(), buffer.size() - 1);
                    }
                }
            }
        }
    }
    return status;
}
}

GakuFileDialogService ServiceForOwner(void* owner) {
    return {GAKU_SERVICE_VERSION, sizeof(GakuFileDialogService), owner, Begin, Poll, Release};
}

void Pump(void* nativeWindow) {
    std::shared_ptr<Request> request;
    {
        std::lock_guard lock(g_mutex);
        while (!g_pending.empty() && g_pending.front()->cancel) g_pending.pop_front();
        if (g_stopping || g_pending.empty()) return;
        request = g_pending.front();
        g_pending.pop_front();
    }
    std::string path;
    HRESULT status = E_UNEXPECTED;
    try { status = Show(*request, static_cast<HWND>(nativeWindow), path); }
    catch (...) { status = E_OUTOFMEMORY; }
    {
        std::lock_guard lock(g_mutex);
        if (request->cancel || g_stopping || status == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            request->state = GAKU_FILE_DIALOG_CANCELLED;
            request->error = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        } else if (FAILED(status)) {
            request->state = GAKU_FILE_DIALOG_FAILED;
            request->error = status;
        } else {
            request->path = std::move(path);
            request->state = GAKU_FILE_DIALOG_ACCEPTED;
            request->error = S_OK;
        }
    }
    if (FAILED(status) && status != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        LogError("[FileDialog] ticket=%llu owner_hwnd=%p failed HRESULT=0x%08lx", request->ticket, nativeWindow, static_cast<unsigned long>(status));
}

void CancelOwner(void* owner) {
    if (!owner) return;
    std::lock_guard lock(g_mutex);
    g_retired.insert(owner);
    for (auto it = g_tickets.begin(); it != g_tickets.end();) {
        if (it->second->owner == owner) { it->second->cancel = true; it = g_tickets.erase(it); }
        else ++it;
    }
    std::erase_if(g_pending, [owner](const auto& request) { return request->owner == owner; });
}

void Shutdown() {
    std::lock_guard lock(g_mutex);
    g_stopping = true;
    for (auto& [id, request] : g_tickets) {
        request->cancel = true;
        if (request->state == GAKU_FILE_DIALOG_PENDING) {
            request->state = GAKU_FILE_DIALOG_CANCELLED;
            request->error = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
    }
    g_pending.clear();
}
}
