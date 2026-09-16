#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <string>

// The completion owns its query independently of the client, so cancelling
// DNS never waits on the UI thread or leaves a callback pointing at a client.
class AsyncResolver
{
    struct Query : OVERLAPPED
    {
        std::atomic<unsigned> references{2};
        std::atomic<bool> done{false};
        DWORD error = 0;
        PADDRINFOEXW result = nullptr;
        HANDLE cancel = nullptr;
        ADDRINFOEXW hints = {};
        timeval timeout = {8, 0};
        std::wstring name;
        Query(const std::string& host) : name(host.begin(), host.end())
        {
            ZeroMemory(static_cast<OVERLAPPED*>(this), sizeof(OVERLAPPED));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
        }
        void release()
        {
            if (references.fetch_sub(1) == 1)
            {
                if (result) FreeAddrInfoExW(result);
                delete this;
            }
        }
        static void CALLBACK complete(DWORD error, DWORD, OVERLAPPED* overlapped)
        {
            Query* query = static_cast<Query*>(overlapped);
            query->error = error;
            query->done.store(true, std::memory_order_release);
            query->release();
        }
    };
    Query* _query = nullptr;
public:
    ~AsyncResolver() { cancel(); }
    AsyncResolver() = default;
    AsyncResolver(const AsyncResolver&) = delete;
    AsyncResolver& operator=(const AsyncResolver&) = delete;
    void cancel()
    {
        if (!_query) return;
        if (!_query->done.load(std::memory_order_acquire)) GetAddrInfoExCancel(&_query->cancel);
        _query->release();
        _query = nullptr;
    }
    void start(const std::string& host)
    {
        cancel();
        _query = new Query(host);
        int error = GetAddrInfoExW(_query->name.c_str(), nullptr, NS_DNS, nullptr,
            &_query->hints, &_query->result, &_query->timeout, _query,
            Query::complete, &_query->cancel);
        if (error != WSA_IO_PENDING) Query::complete(error, 0, _query);
    }
    // 0: pending, 1: resolved, -1: failed.
    int poll(sockaddr_in& address)
    {
        if (!_query || !_query->done.load(std::memory_order_acquire)) return 0;
        int result = -1;
        if (!_query->error)
            for (auto entry = _query->result; entry; entry = entry->ai_next)
                if (entry->ai_family == AF_INET && entry->ai_addrlen >= sizeof(address))
                {
                    address.sin_addr = reinterpret_cast<sockaddr_in*>(entry->ai_addr)->sin_addr;
                    result = 1;
                    break;
                }
        cancel();
        return result;
    }
};
