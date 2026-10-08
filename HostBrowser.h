#pragma once
#include "HostDirectoryClient.h"

class HostBrowser {
    bool visible=true,wasConnected=false;
    std::vector<HostDirectory::Entry> displayedEntries;
    unsigned displayedPage=0,displayedPages=1;
    static bool hit(int x,int y,int width,int height) {
        int mouseY=App.size.y-input.mouse.y;
        return input.mouse.x>=x&&input.mouse.x<x+width&&mouseY>=y&&mouseY<y+height;
    }
public:
    void hide(){visible=false;displayedEntries.clear();}
    void toggle(HostDirectoryClient& directory){displayedEntries.clear();visible=!visible;if(visible)directory.refresh();}
    void show(HostDirectoryClient& directory){displayedEntries.clear();visible=true;directory.refresh();}
    bool shown(const cNetworkRuntime& network) const {return visible&&!network.hasConnection();}
    void update(cNetworkRuntime& network,HostDirectoryClient& directory) {
        bool connected=network.hasConnection();if(wasConnected&&!connected)hide();wasConnected=connected;
        if(!shown(network)||!input.leftClick())return;
        const int top=App.size.y-34;
        for(size_t i=0;i<displayedEntries.size();++i)if(hit(20,top-64-(int)i*18,App.size.x-40,18)){
            std::string address=HostDirectory::addressText(displayedEntries[i]);input.KeyUp(VK_LBUTTON);
            network.connectTo(address);hide();return;
        }
        if(hit(20,48,128,22))directory.refresh(displayedPage);
        else if(hit(156,48,128,22)&&displayedPage>0)directory.refresh(displayedPage-1);
        else if(hit(292,48,128,22)&&displayedPage+1<displayedPages)directory.refresh(displayedPage+1);
        else if(hit(App.size.x-140,48,128,22))visible=false;
        if(hit(12,44,App.size.x-24,top-44))input.KeyUp(VK_LBUTTON);
    }
    void draw(const cNetworkRuntime& network,const HostDirectoryClient& directory) {
        if(!shown(network)){displayedEntries.clear();return;}
        displayedEntries=directory.entries;displayedPage=directory.page;displayedPages=directory.pages;
        const int top=App.size.y-34;
        QueueChatRect(12,44,(float)App.size.x-12,(float)top,.015f,.035f,.03f,1,false);
        QueueChatRect(12,44,(float)App.size.x-12,(float)top,.3f,.45f,.4f,1,true);
        std::string title="Hosts  "+std::to_string(directory.page+1)+"/"+std::to_string(directory.pages);
        QueueChatText(title.c_str(),24,(float)top-19,.5f,.9f,.75f);
        std::string status=FitChatText(directory.status,App.size.x-48);
        QueueChatText(status.c_str(),24,(float)top-39,.65f,.72f,.69f);
        for(size_t i=0;i<directory.entries.size();++i){
            const auto& e=directory.entries[i];int y=top-64-(int)i*18;
            std::string suffix=" | "+std::to_string(e.users)+" users | "+HostDirectory::addressText(e);
            std::string label="> "+FitChatText(e.name,App.size.x-72-ChatTextWidth(suffix))+suffix;
            if(hit(20,y,App.size.x-40,18))QueueChatRect(20,(float)y,(float)App.size.x-20,(float)y+18,.02f,.15f,.11f,1,false);
            QueueChatText(label.c_str(),24,(float)y+2,.4f,.9f,.68f);
        }
        DrawChatButton("REFRESH",vec2i(20,48),false);
        DrawChatButton(directory.page?"PREVIOUS":"--",vec2i(156,48),false);
        DrawChatButton(directory.page+1<directory.pages?"NEXT":"--",vec2i(292,48),false);
        DrawChatButton("CHAT",vec2i(App.size.x-140,48),false);
    }
};
