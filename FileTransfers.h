#pragma once
#include "NetworkProtocol.h"
#include <functional>
#include <map>
#include <memory>
#include <set>

// All transfer and file state is serviced by the application thread.
// The relay never parses this plaintext protocol or opens a file.
class FileTransfers
{
public:
    enum : unsigned char { Offer = 1, Accept, Chunk, Ack, Finish, Done, Cancel };
    static constexpr unsigned ChunkBytes = 8192;
    static constexpr unsigned __int64 MaxFileBytes = 4ULL * 1024 * 1024 * 1024;
    static constexpr unsigned MaxPacketBytes = ChunkBytes + 128;
    using Key = std::pair<int, string>;
    using Send = std::function<bool(int, const vector<unsigned char>&)>;
    using Notice = std::function<void(const string&, bool)>;
    using Announce = std::function<void(int, const string&)>;
private:
    struct File {
        HANDLE handle = INVALID_HANDLE_VALUE;
        std::wstring temporary;
        ~File() { close(); if (!temporary.empty()) DeleteFileW(temporary.c_str()); }
        void close() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); handle = INVALID_HANDLE_VALUE; }
    };
    struct Source {
        std::shared_ptr<File> file;
        string name;
        unsigned __int64 size = 0;
        ULONGLONG expires = 0;
        std::set<int> peers;
    };
    struct Download {
        string name, token, state = "save";
        unsigned __int64 size = 0, offset = 0;
        ULONGLONG touched = 0;
        std::shared_ptr<File> file;
        std::wstring destination;
        crypto_generichash_state hash;
    };
    struct Upload {
        std::shared_ptr<Source> source;
        string token;
        unsigned __int64 offset = 0, acknowledged = 0;
        bool finishing = false;
        ULONGLONG touched = 0;
        crypto_generichash_state hash;
    };
    std::map<string, std::shared_ptr<Source>> sources;
    std::map<Key, Download> downloads;
    std::map<Key, Upload> uploads;
    ULONGLONG nextChunk = 0;
    Key lastServed;
    Send send;
    Notice notice;
    Announce announce;

    static string randomId() {
        unsigned char value[16]; randombytes_buf(value, sizeof(value));
        char hex[33]; sodium_bin2hex(hex, sizeof(hex), value, sizeof(value)); return hex;
    }
    static bool validId(const string& id) {
        if (id.size() != 32) return false;
        for (char c : id) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    }
    static NetworkMessageRaw header(unsigned char type, const string& id, const string& token = "") {
        NetworkMessageRaw raw; raw.putUInt8(type); raw.putString(id, 32); raw.putString(token, 32); return raw;
    }
    bool transmit(int peer, const NetworkMessageRaw& raw) {
        return send(peer, vector<unsigned char>(raw.data(), raw.data() + raw.size()));
    }
    void control(unsigned char type, const Key& key, const string& token, unsigned __int64 offset = 0) {
        auto raw = header(type, key.second, token);
        if (type == Ack) raw.putUInt64(offset);
        transmit(key.first, raw);
    }
    void failDownload(const Key& key, Download& d, const string& reason, bool tellPeer = true) {
        if (tellPeer && !d.token.empty()) control(Cancel, key, d.token);
        d.file.reset(); d.state = reason; d.touched = GetTickCount64();
        notice(d.name + ": " + reason, true);
    }
    static bool validName(const string& name) {
        if (name.empty() || name.size() > 200 || name.back() == '.' || name.back() == ' ') return false;
        for (unsigned char c : name) if (c < 32 || c == 127 || strchr("\\/:*?\"<>|", c)) return false;
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), (int)name.size(), nullptr, 0) > 0;
    }
public:
    FileTransfers(Send s, Notice n, Announce a) : send(std::move(s)), notice(std::move(n)), announce(std::move(a)) {}
    static bool localPath(const std::wstring& path) {
        return path.size() > 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
            path[1] == L':' && path[2] == L'\\' && path.find(L':', 2) == std::wstring::npos &&
            GetDriveTypeW(path.substr(0,3).c_str()) != DRIVE_REMOTE;
    }
    static string utf8(const std::wstring& value) {
        int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
        string result(n, '\0'); if (n) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), (int)value.size(), &result[0], n, nullptr, nullptr); return result;
    }
    static std::wstring wide(const string& value) {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
        std::wstring result(n, L'\0'); if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), &result[0], n); return result;
    }
    static string displayName(const string& name) {
        string result; for (unsigned char c : name) result += c >= 32 && c < 127 ? (char)c : '?'; return result;
    }
    bool offer(const std::wstring& path, const vector<int>& peers) {
        if (peers.empty()) { notice("No recipients are ready for file transfers.", true); return false; }
        if (sources.size() >= 8 || !localPath(path)) { notice("File offer limit reached or path is not a local file.", true); return false; }
        auto source = std::make_shared<Source>(); source->file = std::make_shared<File>();
        source->name = utf8(path.substr(path.find_last_of(L"\\/") + 1));
        if (!validName(source->name)) { notice("Unsupported filename (maximum 200 UTF-8 bytes).", true); return false; }
        // Deny concurrent writers/deletion so the offered bytes cannot change during a transfer.
        source->file->handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION info = {}; LARGE_INTEGER size = {};
        if (source->file->handle == INVALID_HANDLE_VALUE || GetFileType(source->file->handle) != FILE_TYPE_DISK ||
            !GetFileInformationByHandle(source->file->handle, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            !GetFileSizeEx(source->file->handle, &size) || size.QuadPart < 0 || size.QuadPart > MaxFileBytes) {
            notice("Cannot offer file: inaccessible, in use, or larger than 4 GiB.", true); return false;
        }
        source->size = (unsigned __int64)size.QuadPart; source->expires = GetTickCount64() + 600000;
        const string id = randomId();
        auto raw = header(Offer, id); raw.putUInt64(source->size); raw.putString(source->name, 200);
        for (int peer : peers) if (transmit(peer, raw)) source->peers.insert(peer);
        if (source->peers.empty()) { notice("File recipients are unavailable.", true); return false; }
        sources[id] = source;
        announce(-1,id);
        return true;
    }
    std::wstring suggestedName(const Key& key) const {
        auto it = downloads.find(key); if (it == downloads.end() || it->second.file || it->second.state != "save") return L"";
        const auto name = wide(it->second.name);
        auto stem = name.substr(0,name.find(L'.'));
        while (!stem.empty() && stem.back()==L' ') stem.pop_back();
        const bool device = !_wcsicmp(stem.c_str(),L"CON") || !_wcsicmp(stem.c_str(),L"PRN") ||
            !_wcsicmp(stem.c_str(),L"AUX") || !_wcsicmp(stem.c_str(),L"NUL") ||
            (stem.size()==4 && (!_wcsnicmp(stem.c_str(),L"COM",3) || !_wcsnicmp(stem.c_str(),L"LPT",3)) &&
                ((stem[3]>=L'1' && stem[3]<=L'9') || stem[3]==0xb9 || stem[3]==0xb2 || stem[3]==0xb3));
        return device ? L"download-"+name : name;
    }
    string label(const Key& key) const {
        string name, state; unsigned __int64 bytes;
        if (key.first == -1) {
            auto it = sources.find(key.second); if (it == sources.end()) return "File offer withdrawn or expired";
            name = it->second->name; bytes = it->second->size; state = "offered/cancel";
        } else {
            auto it = downloads.find(key); if (it == downloads.end()) return "File offer expired";
            const auto& d = it->second; name = d.name; bytes = d.size;
            state = d.file ? std::to_string(d.size ? (unsigned long long)d.offset * 100 / d.size : 0) + "% cancel" : d.state;
        }
        name = displayName(name); if (name.size() > 25) name = name.substr(0,12) + "..." + name.substr(name.size()-10);
        char size[32];
        if (bytes >= 1073741824ULL) snprintf(size,sizeof(size),"%.1f GiB",bytes/1073741824.0);
        else if (bytes >= 1048576) snprintf(size,sizeof(size),"%.1f MiB",bytes/1048576.0);
        else if (bytes >= 1024) snprintf(size,sizeof(size),"%.1f KiB",bytes/1024.0);
        else snprintf(size,sizeof(size),"%llu B",bytes);
        return name + " (" + size + ") [" + state + "]";
    }
    void withdraw(const string& id) {
        auto source = sources.find(id); if (source == sources.end()) return;
        for (int peer : source->second->peers) control(Cancel,{peer,id},"");
        for (auto it=uploads.begin(); it!=uploads.end();) if (it->first.second==id) it=uploads.erase(it); else ++it;
        sources.erase(source);
    }
    bool active(const Key& key) const { auto it = downloads.find(key); return it != downloads.end() && !!it->second.file; }
    void cancel(const Key& key) { auto it = downloads.find(key); if (it != downloads.end() && it->second.file) failDownload(key, it->second, "cancelled"); }
    bool accept(const Key& key, const std::wstring& destination) {
        auto it = downloads.find(key);
        if (it == downloads.end() || it->second.file || it->second.state != "save") return false;
        size_t active = 0; for (const auto& entry : downloads) if (entry.second.file) ++active;
        if (active >= 4 || !localPath(destination) || GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES) {
            notice("Choose a new local filename; at most four downloads can run.", true); return false;
        }
        auto& d = it->second; d.file = std::make_shared<File>(); d.destination = destination; d.token = randomId();
        const auto temp = destination.substr(0, destination.find_last_of(L'\\') + 1) + L".oi-" + wide(d.token) + L".part";
        d.file->handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (d.file->handle == INVALID_HANDLE_VALUE) { d.file.reset(); notice("Cannot create download in selected folder.", true); return false; }
        d.file->temporary = temp; d.offset = 0; d.touched = GetTickCount64();
        crypto_generichash_init(&d.hash, nullptr, 0, 32);
        control(Accept, key, d.token); return true;
    }
    void receive(int peer, const vector<unsigned char>& bytes) {
        if (bytes.empty() || bytes.size() > MaxPacketBytes) return;
        NetworkMessageRaw raw((const char*)bytes.data(), (int)bytes.size());
        unsigned char type; string id, token;
        if (!raw.getUInt8(type) || !raw.getString(id,32) || !validId(id) || !raw.getString(token,32)) return;
        const Key key(peer,id); const auto now = GetTickCount64();
        if (type == Offer) {
            unsigned __int64 size; string name;
            if (!token.empty() || !raw.getUInt64(size) || size > MaxFileBytes || !raw.getString(name,200) || !validName(name) || !raw.fullyRead() || downloads.count(key)) return;
            size_t fromPeer = 0; for (const auto& entry : downloads) if (entry.first.first == peer) ++fromPeer;
            if (downloads.size() >= 32 || fromPeer >= 8) return;
            Download d; d.name = name; d.size = size; d.touched = now; downloads.emplace(key, std::move(d)); announce(peer,id); return;
        }
        if (type == Cancel && token.empty() && raw.fullyRead()) {
            auto it = downloads.find(key);
            if (it != downloads.end() && (it->second.file || it->second.state == "save")) {
                it->second.file.reset(); it->second.state = "withdrawn";
            }
            return;
        }
        if (!validId(token)) return;
        if (type == Accept) {
            if (!raw.fullyRead() || uploads.count(key)) return;
            auto source = sources.find(id);
            if (source == sources.end() || !source->second->peers.count(peer) || now >= source->second->expires || uploads.size() >= 4) { control(Cancel,key,token); return; }
            Upload u; u.source = source->second; u.token = token; u.touched = now; crypto_generichash_init(&u.hash,nullptr,0,32);
            uploads.emplace(key,std::move(u)); return;
        }
        if (type == Ack || type == Done) {
            auto it = uploads.find(key); if (it == uploads.end() || token != it->second.token) return;
            auto& u = it->second; unsigned __int64 offset = 0;
            if (type == Ack) {
                if (!raw.getUInt64(offset) || !raw.fullyRead() || u.finishing || offset != u.offset || offset <= u.acknowledged) return;
                u.acknowledged = offset; u.touched = now;
            } else if (raw.fullyRead() && u.finishing) {
                notice("Sent " + displayName(u.source->name) + ".",false); u.source->peers.erase(peer); uploads.erase(it);
            }
            return;
        }
        if (type == Cancel) {
            if (!raw.fullyRead()) return;
            auto up = uploads.find(key); if (up != uploads.end() && up->second.token == token) { notice("File transfer cancelled by recipient.",false); uploads.erase(up); }
            auto down = downloads.find(key); if (down != downloads.end() && down->second.file && down->second.token == token) failDownload(key,down->second,"sender unavailable",false);
            return;
        }
        auto it = downloads.find(key); if (it == downloads.end() || !it->second.file || it->second.token != token) return;
        auto& d = it->second;
        if (type == Chunk) {
            unsigned __int64 offset; vector<unsigned char> data;
            if (!raw.getUInt64(offset) || !raw.getBytes(data,ChunkBytes) || data.empty() || !raw.fullyRead() || offset != d.offset || data.size() > d.size - d.offset) { failDownload(key,d,"invalid chunk"); return; }
            DWORD written = 0;
            if (!WriteFile(d.file->handle,data.data(),(DWORD)data.size(),&written,nullptr) || written != data.size()) { failDownload(key,d,"write failed"); return; }
            crypto_generichash_update(&d.hash,data.data(),data.size()); d.offset += written; d.touched = now; control(Ack,key,token,d.offset);
        } else if (type == Finish) {
            unsigned char digest[32], actual[32];
            if (!raw.get(digest,32) || !raw.fullyRead() || d.offset != d.size) { failDownload(key,d,"incomplete transfer"); return; }
            crypto_generichash_final(&d.hash,actual,32);
            if (sodium_memcmp(digest,actual,32) || !FlushFileBuffers(d.file->handle)) { failDownload(key,d,"integrity or disk error"); return; }
            d.file->close();
            // Preserve Windows' downloaded-file provenance without executing or inspecting the content.
            HANDLE zone = CreateFileW((d.file->temporary + L":Zone.Identifier").c_str(), GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (zone != INVALID_HANDLE_VALUE) { const char mark[] = "[ZoneTransfer]\r\nZoneId=3\r\n"; DWORD written; WriteFile(zone,mark,sizeof(mark)-1,&written,nullptr); CloseHandle(zone); }
            // No overwrite flag: never replace an existing user file, even if created during transfer.
            if (!MoveFileExW(d.file->temporary.c_str(),d.destination.c_str(),0)) { failDownload(key,d,"cannot save destination"); return; }
            d.file->temporary.clear(); d.file.reset(); d.state = "saved"; d.touched = now; control(Done,key,token);
            notice("Saved " + displayName(d.name) + ".",false);
        }
    }
    void update() {
        const auto now = GetTickCount64();
        for (auto it = downloads.begin(); it != downloads.end();) {
            if (it->second.file && now - it->second.touched > 30000) failDownload(it->first,it->second,"timed out");
            if (!it->second.file && now - it->second.touched > 600000) it = downloads.erase(it); else ++it;
        }
        for (auto it = uploads.begin(); it != uploads.end();) {
            if (now - it->second.touched > 30000) { control(Cancel,it->first,it->second.token); notice("File upload timed out.",true); it = uploads.erase(it); } else ++it;
        }
        // Expiry stops new acceptances only; active uploads retain their shared Source.
        for (auto it = sources.begin(); it != sources.end();) {
            if (now >= it->second->expires || it->second->peers.empty()) it = sources.erase(it); else ++it;
        }
        if (uploads.empty() || now < nextChunk) return;
        auto it = uploads.upper_bound(lastServed); if (it == uploads.end()) it = uploads.begin();
        for (size_t n = 0; n < uploads.size(); ++n) {
            if (!it->second.finishing && it->second.offset == it->second.acknowledged) break;
            if (++it == uploads.end()) it = uploads.begin();
        }
        auto& u = it->second; if (u.finishing || u.offset != u.acknowledged) return;
        lastServed = it->first;
        if (u.offset == u.source->size) {
            unsigned char digest[32]; auto hash = u.hash; crypto_generichash_final(&hash,digest,32);
            auto raw = header(Finish,it->first.second,u.token); raw.put(digest,32);
            u.finishing = transmit(it->first.first,raw); return;
        }
        const unsigned count = (unsigned)(std::min)((unsigned __int64)ChunkBytes,u.source->size-u.offset);
        vector<unsigned char> bytes(count); LARGE_INTEGER pos; pos.QuadPart = u.offset; DWORD read = 0;
        if (!SetFilePointerEx(u.source->file->handle,pos,nullptr,FILE_BEGIN) || !ReadFile(u.source->file->handle,bytes.data(),count,&read,nullptr) || read != count) {
            control(Cancel,it->first,u.token); notice("File read failed.",true); uploads.erase(it); return;
        }
        auto raw = header(Chunk,it->first.second,u.token); raw.putUInt64(u.offset); raw.putBytes(bytes,ChunkBytes);
        if (transmit(it->first.first,raw)) { crypto_generichash_update(&u.hash,bytes.data(),bytes.size()); u.offset += count; }
        nextChunk = now + 64; // 128 KiB/s aggregate, no catch-up bursts.
    }
    void peerLeft(int peer) {
        for (auto it = uploads.begin(); it != uploads.end();) if (it->first.first == peer) it = uploads.erase(it); else ++it;
        for (auto& entry : downloads) if (entry.first.first == peer) { entry.second.file.reset(); entry.second.state = "unavailable"; }
        for (auto& entry : sources) entry.second->peers.erase(peer);
    }
    void clear() { uploads.clear(); downloads.clear(); sources.clear(); nextChunk = 0; }
};
