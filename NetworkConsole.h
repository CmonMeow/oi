#pragma once

static bool PollConsoleLine(string& line)
{
    static string draft;
    while (_kbhit())
    {
        int c = _getch();
        if (c == '\r' || c == '\n')
        {
            putchar('\n');
            line = draft;
            draft.clear();
            return true;
        }
        if (c == 8)
        {
            if (!draft.empty())
            {
                draft.resize(draft.size() - 1);
                fputs("\b \b", stdout);
            }
            continue;
        }
        if (c >= 32 && c < 127 && draft.size() < CHAT_MAX_MESSAGE_CHARS)
        {
            draft.push_back((char)c);
            putchar(c);
        }
    }
    return false;
}

static void RunConsoleCommand(const string& inputLine, cNetworkRuntime& network, bool& quit, bool& voiceTogglePulse)
{
    string line = TrimWhitespace(inputLine);
    if (line.empty())
    {
        return;
    }
    if (_stricmp(line.c_str(), "/host") == 0) { network.hostOnPort(); return; }
    if (_stricmp(line.c_str(), "/help") == 0) { network.showHelp(); return; }
    if (_stricmp(line.c_str(), "/clear") == 0) { network.clearChat(); return; }
    if (MatchesCommand(line, "/name")) { network.changeName(line.substr(5)); return; }
    if (_stricmp(line.c_str(), "/quit") == 0 || _stricmp(line.c_str(), "/exit") == 0)
    {
        quit = true;
        return;
    }
    if (_stricmp(line.c_str(), "/voice") == 0)
    {
        voiceTogglePulse = true;
        printf("voice toggled\n");
        return;
    }
    if (_stricmp(line.c_str(), "/disconnect") == 0)
    {
        network.disconnect();
        return;
    }
    if (MatchesCommand(line, "/connect"))
    {
        string address = TrimWhitespace(line.size() > 8 ? line.substr(8) : string());
        network.connectTo(address.empty() ? DEFAULT_NETWORK_ADDRESS : address);
        return;
    }
    if (MatchesCommand(line, "/kick"))
    {
        if (network.isHost())
        {
            network.kickPlayer(TrimWhitespace(line.size() > 5 ? line.substr(5) : string()), false);
        }
        else
        {
            network.sendChat(line);
        }
        return;
    }
    if (MatchesCommand(line, "/ban"))
    {
        if (network.isHost())
        {
            network.kickPlayer(TrimWhitespace(line.size() > 4 ? line.substr(4) : string()), true);
        }
        else
        {
            network.sendChat(line);
        }
        return;
    }

    network.sendChat(line);
}