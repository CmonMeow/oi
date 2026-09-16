#include <winsock2.h>
#include "sysdef.h"
#include "NetworkProtocol.h"
#include <map>
#include <deque>
#include <algorithm>
#include <sstream>
#include <conio.h>
#include "NetworkRuntime.h"
#include "font.h"
static void QueueChatRect(float minX,float minY,float maxX,float maxY,float r,float g,float b,float a,bool outline)
{
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r,g,b,a); glPolygonMode(GL_FRONT_AND_BACK,outline ? GL_LINE : GL_FILL);
    glRectf(minX,minY,maxX,maxY); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
}
static void QueueChatText(const char* text,float x,float y,float r=.86f,float g=.9f,float b=.88f)
{ drawstring(text,vec3f(x,y,0),vec3f(r,g,b)); }
static bool ChatButtonHovered(vec2i pos)
{
    const int y = App.size.y - input.mouse.y;
    return input.mouse.x >= pos.x && input.mouse.x < pos.x + 128 &&
           y >= pos.y && y < pos.y + 22;
}

static void DrawChatButton(const char* text, vec2i pos, bool on)
{
    QueueChatRect((float)pos.x, (float)pos.y, (float)pos.x + 128.f, (float)pos.y + 22.f,
                  on ? 0.f : .02f, on ? .55f : .02f, on ? .52f : .02f, .92f, false);
    const float border = ChatButtonHovered(pos) ? .8f : .5f;
    QueueChatRect((float)pos.x, (float)pos.y, (float)pos.x + 128.f, (float)pos.y + 22.f,
                  border, border, border, .95f, true);
    const float textX = (float)pos.x + (128.f - (float)strlen(text) * 12.f) * .5f;
    QueueChatText(text, textX, (float)pos.y + 5.f, .92f, .98f, .96f);
}

#include "ClientSettings.h"
#include "VoiceChat.h"
#include "ChatBox.h"
void RunChatClient(HWND hWnd)
{
    HDC dc = GetDC(hWnd);
    PackedClientSettings settings;
    cNetworkRuntime network(hWnd, &settings);
    cChatBox chatBox;
    cVoiceChat voiceChat;
    unsigned __int64 lastTitleUpdate = 0;
    unsigned __int64 previousIncoming = 0, previousOutgoing = 0;
    bool hadTrafficSample = false;
    string previousTitle;
    bool selectingHotkey = false;
    unsigned char releaseHotkey = 0;

    while (!App.quit)
    {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (selectingHotkey && (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN))
            {
                if (msg.wParam == VK_ESCAPE)
                    selectingHotkey = false;
                else if (msg.wParam >= VK_BACK && msg.wParam < 255)
                {
                    settings.setTalkKey((unsigned char)msg.wParam);
                    selectingHotkey = false;
                }
                releaseHotkey = (unsigned char)msg.wParam;
                input.Clear();
                continue;
            }
            if (releaseHotkey && msg.wParam == releaseHotkey &&
                (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN ||
                 msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP))
            {
                if (msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) releaseHotkey = 0;
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            // Touch taps can arrive as a complete mouse press/release pair.
            // Let the controls observe each edge before draining the next one.
            if (msg.message == WM_LBUTTONDOWN || msg.message == WM_LBUTTONDBLCLK ||
                msg.message == WM_LBUTTONUP)
                break;
        }

        network.update();
        const unsigned __int64 now = GetTickCount64();
        if (now - lastTitleUpdate >= 1000 || previousTitle.empty())
        {
            unsigned __int64 incoming = 0, outgoing = 0;
            const bool connected = network.trafficTotals(incoming, outgoing);
            char title[192];
            if (connected)
            {
                const double seconds = (now - lastTitleUpdate) / 1000.0;
                const bool validSample = hadTrafficSample && seconds > 0 &&
                    incoming >= previousIncoming && outgoing >= previousOutgoing;
                const double inputRate = validSample ? (incoming - previousIncoming) / seconds / 1024.0 : 0;
                const double outputRate = validSample ? (outgoing - previousOutgoing) / seconds / 1024.0 : 0;
                if (network.isHost())
                    snprintf(title, sizeof(title), "Hosting | Avg ping: %d ms | In: %.1f KiB/s | Out: %.1f KiB/s",
                             network.latencyMS(), inputRate, outputRate);
                else if (network.clientReady())
                    snprintf(title, sizeof(title), "Ping: %d ms | In: %.1f KiB/s | Out: %.1f KiB/s",
                             network.latencyMS(), inputRate, outputRate);
                else
                    snprintf(title, sizeof(title), "Connecting | In: %.1f KiB/s | Out: %.1f KiB/s", inputRate, outputRate);
            }
            else
                snprintf(title, sizeof(title), "Disconnected");
            if (previousTitle != title)
            {
                SetWindowTextA(hWnd, title);
                previousTitle = title;
            }
            hadTrafficSample = connected;
            previousIncoming = incoming;
            previousOutgoing = outgoing;
            lastTitleUpdate = now;
        }
        const vec2i micButton(App.size.x - 140, App.size.y - 28);
        const vec2i hotkeyButton(App.size.x - 276, App.size.y - 28);
        const bool micClicked = ChatButtonHovered(micButton) && input.leftClick();
        const bool hotkeyClicked = ChatButtonHovered(hotkeyButton) && input.leftClick();
        if (micClicked || hotkeyClicked) input.KeyUp(VK_LBUTTON);
        if (micClicked && !settings.voiceEnabled()) settings.toggleVoiceEnabled();
        if (hotkeyClicked)
        {
            selectingHotkey = !selectingHotkey;
            input.Clear();
            voiceChat.resetTransmitMode();
        }
        if (!selectingHotkey && !releaseHotkey) chatBox.update(network);
        input.ConsumeMouseWheel();
        const bool talkDown = !selectingHotkey && !releaseHotkey &&
            input.pressed(settings.talkKey) && !chatBox.active();
        if (talkDown && !settings.voiceEnabled()) settings.toggleVoiceEnabled();
        voiceChat.update(network, talkDown, settings, micClicked);

        glViewport(0, 0, App.size.x, App.size.y);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, App.size.x, 0, App.size.y, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        chatBox.draw(network);
        DrawChatButton(voiceChat.micEnabled() ? "MIC ON" : "MIC OFF",
                                    micButton, voiceChat.micEnabled());
        DrawChatButton(selectingHotkey ? "PRESS KEY" : settings.talkKeyLabel().c_str(), hotkeyButton, selectingHotkey);
        SwapBuffers(dc);

        Sleep(10);
    }

    ReleaseDC(hWnd, dc);
}

