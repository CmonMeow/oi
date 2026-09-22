#pragma once
#include <functional>
#include <map>
#include <deque>

// The room carries bounded connection setup only. Video never enters this protocol.
class ScreenSignaling
{
public:
    enum Kind { Start, Stop, Watch, Signal, Close };
    struct Event { int kind = 0, peer = -1; string share, connection, payload; };
    using Wire = std::function<void(int,const NetworkMessageRaw&)>;
private:
    struct Connection { int owner, viewer; string share; };
    struct Budget { ULONGLONG start = 0, controlStart = 0; unsigned messages = 0, bytes = 0, controls = 0; };
    std::function<bool()> host;
    std::function<int()> local;
    std::function<vector<int>()> peers;
    Wire wire;
    std::function<void(int,const string&)> announce;
    std::map<int,string> offers;
    std::map<string,Connection> connections;
    std::map<int,Budget> budgets;
    std::deque<Event> events;

    void queue(const Event& e) {
        if(events.size()>=256) events.pop_front();
        events.push_back(e);
    }

    static NetworkMessageRaw encode(const Event& e) {
        NetworkMessageRaw raw; raw.putInt32(e.kind); raw.putInt32(e.peer);
        raw.putString(e.share,32); raw.putString(e.connection,32);
        raw.putBytes(vector<unsigned char>(e.payload.begin(),e.payload.end()),24000); return raw;
    }
    void deliver(int to, const Event& e) {
        if (to == local()) accept(e);
        else wire(to,encode(e));
    }
    void publish(const Event& e) {
        accept(e);
        for (int peer : peers()) if (peer != local()) wire(peer,encode(e));
    }
    void accept(const Event& e) {
        if(e.peer<0) return;
        if (e.kind == Start) {
            auto found=offers.find(e.peer);
            if(found!=offers.end() && found->second==e.share) return;
            if(found==offers.end() && offers.size()>=256) return;
            offers[e.peer] = e.share; announce(e.peer,e.share);
        } else if (e.kind == Stop) {
            auto found = offers.find(e.peer);
            if (found == offers.end() || found->second != e.share) {
                // A rejected start has no published offer, but must reset its owner's UI.
                if(e.peer==local()) queue(e);
                return;
            }
            offers.erase(found);
            for (auto it=connections.begin();it!=connections.end();)
                if (it->second.owner==e.peer) it=connections.erase(it); else ++it;
            queue(e);
        } else {
            if (e.kind == Watch) {
                if(!available(local(),e.share) || connections.size()>=256) return;
                connections[e.connection] = {local(),e.peer,e.share};
            }
            auto found=connections.find(e.connection);
            if (found==connections.end() || found->second.share!=e.share ||
                (found->second.owner!=e.peer && found->second.viewer!=e.peer)) return;
            if (e.kind==Close) connections.erase(found);
            queue(e);
        }
    }
    void closeConnection(const string& id) {
        auto found=connections.find(id); if(found==connections.end()) return;
        const auto c=found->second;
        connections.erase(found);
        // Local delivery does not need to find the just-erased record.
        for(int target : {c.owner,c.viewer}) {
            Event e{Close,target==c.owner?c.viewer:c.owner,c.share,id,{}};
            if(target==local()) queue(e); else wire(target,encode(e));
        }
    }
    void route(int from, Event e) {
        if(e.kind==Start) {
            if(!e.connection.empty() || !e.payload.empty()) return;
            auto old=offers.find(from);
            if(old==offers.end() && offers.size()>=256) return;
            if(old!=offers.end()) { if(old->second==e.share) return; stopOwner(from); }
            e.peer=from; publish(e);
        } else if(e.kind==Stop) {
            if(available(from,e.share)) stopOwner(from);
        } else if(e.kind==Watch) {
            if(e.peer==from || !available(e.peer,e.share)) {
                deliver(from,{Close,e.peer,e.share,e.connection,"This screen share has ended."}); return;
            }
            if(!e.payload.empty() || connections.count(e.connection)) return;
            vector<string> old;
            for(const auto& c:connections) if(c.second.viewer==from) old.push_back(c.first);
            for(const auto& id:old) closeConnection(id);
            if(connections.size()>=256) return;
            int owner=e.peer; connections[e.connection]={owner,from,e.share}; e.peer=from; deliver(owner,e);
        } else {
            auto c=connections.find(e.connection);
            if(c==connections.end() || c->second.share!=e.share ||
                !((c->second.owner==from && c->second.viewer==e.peer) ||
                  (c->second.viewer==from && c->second.owner==e.peer))) return;
            if(e.kind==Close) { closeConnection(e.connection); return; }
            if(e.kind!=Signal || e.payload.empty()) return;
            int target=e.peer; e.peer=from; deliver(target,e);
        }
    }
    void stopOwner(int owner) {
        auto found=offers.find(owner); if(found==offers.end()) return;
        string share=found->second;
        vector<string> closing;
        for(const auto& c:connections) if(c.second.owner==owner) closing.push_back(c.first);
        for(const auto& id:closing) closeConnection(id);
        publish({Stop,owner,share,{},{}});
    }
public:
    ScreenSignaling(std::function<bool()> h,std::function<int()> l,
        std::function<vector<int>()> p,Wire w,std::function<void(int,const string&)> a)
        :host(h),local(l),peers(p),wire(w),announce(a) {}
    static bool validId(const string& id) {
        return id.size()==32 && id.find_first_not_of("0123456789abcdef")==string::npos;
    }
    bool available(int owner,const string& id) const {
        auto found=offers.find(owner); return found!=offers.end() && found->second==id;
    }
    void send(Event e) {
        if(!validId(e.share) || e.payload.size()>24000 || e.kind<Start || e.kind>Close ||
            (e.kind>=Watch && !validId(e.connection))) return;
        if(e.kind==Watch) {
            if(e.peer==local() || !available(e.peer,e.share)) return;
            if(!host()) {
                for(auto it=connections.begin();it!=connections.end();)
                    if(it->second.viewer==local()) it=connections.erase(it); else ++it;
                connections[e.connection]={e.peer,local(),e.share};
            }
        }
        if(e.kind==Close && !host()) connections.erase(e.connection);
        if(host()) {
            route(local(),e);
        } else wire(-1,encode(e));
    }
    void receive(int from,NetworkMessageRaw& raw) {
        Event e; vector<unsigned char> bytes;
        if(!raw.getInt32(e.kind) || !raw.getInt32(e.peer) || !raw.getString(e.share,32) ||
            !raw.getString(e.connection,32) || !raw.getBytes(bytes,24000) || !raw.fullyRead() ||
            e.kind<Start || e.kind>Close || !validId(e.share) ||
            (e.kind>=Watch && !validId(e.connection))) return;
        e.payload.assign(bytes.begin(),bytes.end());
        auto& b=budgets[from]; auto now=GetTickCount64();
        if(now-b.start>=1000) { b.start=now;b.messages=b.bytes=0; }
        if(++b.messages>(host()?128u:1024u) || (b.bytes+=(unsigned)bytes.size())>(host()?131072u:1048576u)) return;
        if(host() && (e.kind==Start || e.kind==Watch)) {
            if(now-b.controlStart>=10000) { b.controlStart=now;b.controls=0; }
            if(++b.controls>4) {
                deliver(from,{e.kind==Start?Stop:Close,e.kind==Start?from:e.peer,e.share,e.connection,
                    "Too many screen requests. Wait a few seconds and try again."});
                return;
            }
        }
        if(host()) route(from,e); else accept(e);
    }
    void sync(int to) { for(const auto& offer:offers) wire(to,encode({Start,offer.first,offer.second,{},{}})); }
    void peerLeft(int peer) {
        if(host()) {
            stopOwner(peer); vector<string> closing;
            for(const auto& c:connections) if(c.second.viewer==peer) closing.push_back(c.first);
            for(const auto& id:closing) closeConnection(id);
        } else {
            auto found=offers.find(peer);
            if(found!=offers.end()) accept({Stop,peer,found->second,{},{}});
        }
        budgets.erase(peer);
    }
    bool pop(Event& event) { if(events.empty()) return false; event=std::move(events.front()); events.pop_front(); return true; }
    void clear() { offers.clear(); connections.clear(); budgets.clear(); events.clear(); }
};
