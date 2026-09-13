#pragma once

#include <shellapi.h>

class cChatBox
{
    bool _active;
    string _draft;
    string _notice;
    size_t _cursor;
    size_t _inputViewStart;
    size_t _scrollOffset;
    enum
    {
        BOX_X = 12,
        BOX_Y = 14,
        BOX_WIDTH = 680,
        LINE_HEIGHT = 18,
        INPUT_HEIGHT = 24,
        INPUT_TEXT_X = BOX_X + 10,
        INPUT_TEXT_Y = BOX_Y + 12,
        INPUT_CHAR_WIDTH = 12,
        HISTORY_WHEEL_LINES = 3
    };

    static void DrainTextInput()
    {
        unsigned char ignored;
        while (input.PopTextInput(ignored))
        {
        }
    }

    static string Trim(const string& text)
    {
        size_t first = 0;
        while (first < text.size() && isspace((unsigned char)text[first]))
        {
            ++first;
        }
        size_t last = text.size();
        while (last > first && isspace((unsigned char)text[last - 1]))
        {
            --last;
        }
        return text.substr(first, last - first);
    }

    static bool ParseCommandArgument(const string& command, const char* prefix, string& argument)
    {
        size_t prefixLength = strlen(prefix);
        if (_strnicmp(command.c_str(), prefix, prefixLength) != 0)
        {
            return false;
        }
        argument = Trim(command.size() > prefixLength ? command.substr(prefixLength) : string());
        return !argument.empty();
    }

    static bool ControlDown()
    {
        return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    }

    static string ClipboardText()
    {
        string result;
        if (!IsClipboardFormatAvailable(CF_TEXT) || !OpenClipboard(NULL))
        {
            return result;
        }

        HANDLE handle = GetClipboardData(CF_TEXT);
        if (handle)
        {
            const char* text = static_cast<const char*>(GlobalLock(handle));
            if (text)
            {
                result = text;
                GlobalUnlock(handle);
            }
        }
        CloseClipboard();
        return result;
    }

    void pasteClipboard()
    {
        string text = SanitiseChatText(ClipboardText());
        if (text.empty() || _draft.size() >= CHAT_MAX_MESSAGE_CHARS)
        {
            return;
        }

        size_t available = CHAT_MAX_MESSAGE_CHARS - _draft.size();
        if (text.size() > available)
        {
            text.resize(available);
        }
        _draft.insert(_cursor, text);
        _cursor += text.size();
    }

    size_t visibleRows() const
    {
        return CHAT_VISIBLE_ROWS;
    }

    float boxWidth() const
    {
        const float margin = (float)(BOX_X * 2);
        if (App.size.x > margin + 120.f && App.size.x - margin < BOX_WIDTH)
        {
            return App.size.x - margin;
        }
        return BOX_WIDTH;
    }

    void clampCursor()
    {
        if (_cursor > _draft.size())
        {
            _cursor = _draft.size();
        }
    }

    void clampScroll(const cNetworkRuntime& network)
    {
        const vector<NetworkChatLine>& lines = network.chatLines();
        const size_t rows = visibleRows();
        const size_t maxOffset = lines.size() > rows ? lines.size() - rows : 0;
        if (_scrollOffset > maxOffset)
        {
            _scrollOffset = maxOffset;
        }
    }

    size_t inputViewStart()
    {
        clampCursor();
        if (_cursor < _inputViewStart)
        {
            _inputViewStart = _cursor;
        }
        const size_t usableChars = CHAT_WRAP_CHARS > 3 ? CHAT_WRAP_CHARS - 3 : CHAT_WRAP_CHARS;
        if (_cursor > _inputViewStart + usableChars)
        {
            _inputViewStart = _cursor - usableChars;
        }
        if (_inputViewStart > _draft.size())
        {
            _inputViewStart = _draft.size();
        }
        return _inputViewStart;
    }

    void setCursorFromMouse()
    {
        const __int32 mouseX = input.mouse.x;
        size_t viewStart = inputViewStart();
        int relativeX = mouseX - INPUT_TEXT_X - INPUT_CHAR_WIDTH * 2;
        size_t clicked = relativeX > 0 ? (size_t)(relativeX / INPUT_CHAR_WIDTH) : 0;
        _cursor = viewStart + clicked;
        if (_cursor > _draft.size())
        {
            _cursor = _draft.size();
        }
    }

    static bool IsUrlChar(char c)
    {
        return c > 32 && c != '"' && c != '\'' && c != '<' && c != '>' && c != '[' && c != ']';
    }

    static bool UrlAtCharacter(const string& line, size_t character, string& url)
    {
        url.clear();
        const char* prefixes[] = { "http://", "https://", "www." };
        for (size_t p = 0; p < 3; ++p)
        {
            size_t found = line.find(prefixes[p]);
            while (found != string::npos)
            {
                size_t end = found;
                while (end < line.size() && IsUrlChar(line[end]))
                {
                    ++end;
                }
                if (character >= found && character <= end)
                {
                    url = line.substr(found, end - found);
                    while (!url.empty() && (url[url.size() - 1] == '.' || url[url.size() - 1] == ',' || url[url.size() - 1] == ')' || url[url.size() - 1] == ';'))
                    {
                        url.resize(url.size() - 1);
                    }
                    if (p == 2)
                    {
                        url = string("http://") + url;
                    }
                    return !url.empty();
                }
                found = line.find(prefixes[p], end);
            }
        }
        return false;
    }

    static bool FindNextUrl(const string& line, size_t searchFrom, size_t& urlStart, size_t& urlEnd)
    {
        const char* prefixes[] = { "http://", "https://", "www." };
        size_t best = string::npos;
        for (size_t p = 0; p < 3; ++p)
        {
            size_t found = line.find(prefixes[p], searchFrom);
            if (found != string::npos && (best == string::npos || found < best))
            {
                best = found;
            }
        }
        if (best == string::npos)
        {
            return false;
        }

        size_t end = best;
        while (end < line.size() && IsUrlChar(line[end]))
        {
            ++end;
        }
        while (end > best && (line[end - 1] == '.' || line[end - 1] == ',' || line[end - 1] == ')' || line[end - 1] == ';'))
        {
            --end;
        }
        if (end <= best)
        {
            return false;
        }
        urlStart = best;
        urlEnd = end;
        return true;
    }

    static void DrawChatLineWithLinks(const string& line, float x, float y, float r, float g, float b)
    {
        size_t offset = 0;
        while (offset < line.size())
        {
            size_t urlStart = 0;
            size_t urlEnd = 0;
            if (!FindNextUrl(line, offset, urlStart, urlEnd))
            {
                QueueChatText(line.substr(offset).c_str(), x + (float)offset * INPUT_CHAR_WIDTH, y, r, g, b);
                break;
            }
            if (urlStart > offset)
            {
                QueueChatText(line.substr(offset, urlStart - offset).c_str(), x + (float)offset * INPUT_CHAR_WIDTH, y, r, g, b);
            }
            QueueChatText(line.substr(urlStart, urlEnd - urlStart).c_str(), x + (float)urlStart * INPUT_CHAR_WIDTH, y, 1.0f, .92f, .22f);
            offset = urlEnd;
        }
    }

    bool openLinkUnderMouse(const cNetworkRuntime& network)
    {
        const vector<NetworkChatLine>& lines = network.chatLines();
        const size_t rows = visibleRows();
        const size_t newestExclusive = lines.size() > _scrollOffset ? lines.size() - _scrollOffset : 0;
        const size_t first = newestExclusive > rows ? newestExclusive - rows : 0;
        const float height = (float)(rows * (size_t)LINE_HEIGHT) + INPUT_HEIGHT + 18.f;
        const float textTop = BOX_Y + height - 22.f;
        const float mouseX = (float)input.mouse.x;
        const float mouseY = (float)(-input.mouse.y + App.size.y);
        for (size_t i = first; i < newestExclusive; ++i)
        {
            const float lineY = textTop - (float)(i - first) * LINE_HEIGHT;
            if (mouseY >= lineY - 2.f && mouseY <= lineY + LINE_HEIGHT - 2.f && mouseX >= BOX_X + 8.f)
            {
                size_t character = (size_t)((mouseX - (BOX_X + 8.f)) / INPUT_CHAR_WIDTH);
                string url;
                if (UrlAtCharacter(lines[i].text, character, url))
                {
                    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    return true;
                }
            }
        }
        return false;
    }

    static bool ParsePrivateCommand(const string& command, const char* prefix, string& target, string& message)
    {
        string argument;
        if (!ParseCommandArgument(command, prefix, argument))
        {
            return false;
        }
        size_t split = argument.find(' ');
        if (split == string::npos)
        {
            return false;
        }
        target = Trim(argument.substr(0, split));
        message = Trim(argument.substr(split + 1));
        return !target.empty() && !message.empty();
    }

    enum class ChatCommand
    {
        Help,
        Name,
        Connect,
        Host,
        Clear,
        Disconnect,
        Kick,
        Ban,
        Gm,
        Users,
        Pm,
        Whisper,
        Tell,
        Direct,
        Unknown
    };

    struct CommandEntry
    {
        const char* name;
        ChatCommand id;
    };

    static constexpr CommandEntry COMMANDS[] =
    {
        { "/name",       ChatCommand::Name       },
        { "/help",       ChatCommand::Help       },
        { "/host",       ChatCommand::Host       },
        { "/connect",    ChatCommand::Connect    },
        { "/clear",      ChatCommand::Clear      },
        { "/disconnect", ChatCommand::Disconnect },
        { "/kick",       ChatCommand::Kick       },
        { "/ban",        ChatCommand::Ban        },
        { "/gm",         ChatCommand::Gm         },
        { "/users",      ChatCommand::Users      },
        { "/pm",         ChatCommand::Pm         },
        { "/w",          ChatCommand::Whisper    },
        { "/tell",       ChatCommand::Tell       },
        { "/direct",     ChatCommand::Direct     },
    };

    static ChatCommand ResolveCommand(const string& command)
    {
        for (const auto& entry : COMMANDS)
        {
            if (MatchesCommand(command, entry.name))
                return entry.id;
        }

        return ChatCommand::Unknown;
    }

    void runCommand(const string& command, cNetworkRuntime& network)
    {
        const ChatCommand cmd = ResolveCommand(command);

        switch (cmd)
        {
        case ChatCommand::Help:
            network.showHelp();
            _notice = "Scroll chat to read help.";
            break;

        case ChatCommand::Name:
            network.changeName(command.substr(5));
            break;

        case ChatCommand::Host:
            if (!Trim(command.substr(5)).empty())
                _notice = "usage: /host";
            else if (network.hasConnection())
                _notice = "Use /disconnect before hosting.";
            else
                _notice = network.hostOnPort() ? "Hosting on UDP port 777." : "Host failed; port may be in use.";
            break;

        case ChatCommand::Connect:
        {
            string address = Trim(command.size() > 8 ? command.substr(8) : string());

            if (address.empty()) address = DEFAULT_NETWORK_ADDRESS;

            _notice = network.connectTo(address) ? string("connecting to ") + address : string("connection failed");

            break;
        }

        case ChatCommand::Clear:
            network.clearChat();
            break;

        case ChatCommand::Disconnect:
            if(network.disconnect()) _notice = "disconnected";
            break;

        case ChatCommand::Kick:
        case ChatCommand::Ban:
        case ChatCommand::Gm:
        case ChatCommand::Users:
            network.sendChat(command);
            break;

        case ChatCommand::Pm:
        case ChatCommand::Whisper:
        case ChatCommand::Tell:
        case ChatCommand::Direct:
        {
            const char* prefix = cmd == ChatCommand::Tell ? "/tell" : cmd == ChatCommand::Pm ? "/pm" : cmd == ChatCommand::Direct ? "/direct" : "/w";

            string target;
            string message;

            if (!ParsePrivateCommand(command, prefix, target, message))
            {
                _notice = "usage: /pm name|netId message";
            }
            else
            {
                network.sendPrivateChatByReference(target, message);
            }

            break;
        }

        default:
            _notice = string("unknown command: ") + command;
            break;
        }
    }

    void submit(cNetworkRuntime& network)
    {
        string text = Trim(_draft);
        _draft.clear();
        _cursor = 0;
        _inputViewStart = 0;
        _active = false;
        if (text.empty())
        {
            return;
        }

        if (text[0] == '/')
        {
            runCommand(text, network);
        }
        else
        {
            network.sendChat(text);
        }
    }

public:
    cChatBox() : _active(false), _cursor(0), _inputViewStart(0), _scrollOffset(0)
    {
    }

    bool active() const
    {
        return _active;
    }

    bool mouseOver() const
    {
        const float height = (float)(CHAT_VISIBLE_ROWS * LINE_HEIGHT) + INPUT_HEIGHT + 18.f;
        const float width = boxWidth();
        const float mouseX = (float)input.mouse.x;
        const float mouseY = (float)(-input.mouse.y + App.size.y);
        return mouseX >= BOX_X && mouseX <= BOX_X + width && mouseY >= BOX_Y && mouseY <= BOX_Y + height;
    }

    void update(cNetworkRuntime& network)
    {
        clampScroll(network);
        const bool capturesMouse = mouseOver();
        __int32 mouseWheel = capturesMouse ? input.ConsumeMouseWheel() : 0;
        if (mouseWheel != 0)
        {
            const size_t rows = visibleRows();
            const vector<NetworkChatLine>& lines = network.chatLines();
            const size_t maxOffset = lines.size() > rows ? lines.size() - rows : 0;
            size_t wheelSteps = (size_t)(abs(mouseWheel) / WHEEL_DELTA);
            if (wheelSteps == 0)
            {
                wheelSteps = 1;
            }
            const size_t scrollLines = HISTORY_WHEEL_LINES * wheelSteps;
            if (mouseWheel > 0)
            {
                _scrollOffset += scrollLines;
                if (_scrollOffset > maxOffset)
                {
                    _scrollOffset = maxOffset;
                }
            }
            else
            {
                _scrollOffset = _scrollOffset > scrollLines ? _scrollOffset - scrollLines : 0;
            }
        }

        if (!_active)
        {
            if (input.leftClick() && capturesMouse)
            {
                if(openLinkUnderMouse(network)) input.KeyUp(VK_LBUTTON);
            }
            if (input.pressed(VK_RETURN) || input.leftClick() && capturesMouse)
            {
                _active = true;
                _cursor = _draft.size();
                DrainTextInput();
                input.KeyUp(VK_RETURN);
                input.KeyUp(VK_LBUTTON);
            }
            else if (input.pressed(VK_OEM_2))
            {
                _active = true;
                _draft = "/";
                _cursor = _draft.size();
                DrainTextInput();
                input.KeyUp(VK_OEM_2);
            }
            return;
        }

        if (input.leftClick() && capturesMouse)
        {
            setCursorFromMouse();
            input.KeyUp(VK_LBUTTON);
        }

        if (ControlDown() && input.pressed('V'))
        {
            pasteClipboard();
            DrainTextInput();
            input.KeyUp('V');
        }

        unsigned char c;
        while (input.PopTextInput(c))
        {
            if (_draft.size() < CHAT_MAX_MESSAGE_CHARS)
            {
                _draft.insert(_cursor, 1, (char)c);
                ++_cursor;
            }
        }

        if (input.pressed(VK_BACK))
        {
            if (_cursor > 0 && !_draft.empty())
            {
                _draft.erase(_cursor - 1, 1);
                --_cursor;
            }
            input.KeyUp(VK_BACK);
        }
        if (input.pressed(VK_DELETE))
        {
            if (_cursor < _draft.size())
            {
                _draft.erase(_cursor, 1);
            }
            input.KeyUp(VK_DELETE);
        }
        if (input.pressed(VK_LEFT))
        {
            if (_cursor > 0)
            {
                --_cursor;
            }
            input.KeyUp(VK_LEFT);
        }
        if (input.pressed(VK_RIGHT))
        {
            if (_cursor < _draft.size())
            {
                ++_cursor;
            }
            input.KeyUp(VK_RIGHT);
        }
        if (input.pressed(VK_HOME))
        {
            _cursor = 0;
            input.KeyUp(VK_HOME);
        }
        if (input.pressed(VK_END))
        {
            _cursor = _draft.size();
            input.KeyUp(VK_END);
        }
        if (input.pressed(VK_ESCAPE) || input.leftClick() && !capturesMouse)
        {
            _draft.clear();
            _cursor = 0;
            _inputViewStart = 0;
            _active = false;
            input.KeyUp(VK_ESCAPE);
        }
        if (input.pressed(VK_RETURN))
        {
            submit(network);
            input.KeyUp(VK_RETURN);
        }
    }

    void draw(const cNetworkRuntime& network)
    {
        clampScroll(network);
        const float x = BOX_X;
        const float y = BOX_Y;
        const float width = boxWidth();
        const float lineHeight = LINE_HEIGHT;
        const float inputHeight = INPUT_HEIGHT;
        const vector<NetworkChatLine>& lines = network.chatLines();
        const size_t rows = visibleRows();
        const float historyHeight = (float)(rows * (size_t)lineHeight);
        const float height = historyHeight + inputHeight + 18.f;

        QueueChatRect(x, y, x + width, y + height, .02f, .025f, .025f, .70f, false);
        QueueChatRect(x, y, x + width, y + height, .32f, .38f, .36f, .95f, true);

        const size_t newestExclusive = lines.size() > _scrollOffset ? lines.size() - _scrollOffset : 0;
        const size_t first = newestExclusive > rows ? newestExclusive - rows : 0;
        float textY = y + height - 22.f;
        for (size_t i = first; i < newestExclusive; ++i)
        {
            float r = .96f;
            float g = .96f;
            float b = .96f;
            if (lines[i].kind == CLKSystem)
            {
                r = 1.0f;
                g = .30f;
                b = .25f;
            }
            else if (lines[i].kind == CLKPrivate)
            {
                r = .82f;
                g = .70f;
                b = 1.0f;
            }
            DrawChatLineWithLinks(lines[i].text, x + 8.f, textY - (float)(i - first) * lineHeight, r, g, b);
        }

        size_t viewStart = inputViewStart();
        string draft = _draft.substr(viewStart);
        size_t cursorInView = _cursor - viewStart;
        const size_t maxDraftChars = CHAT_WRAP_CHARS > 3 ? CHAT_WRAP_CHARS - 3 : CHAT_WRAP_CHARS;
        if (draft.size() > maxDraftChars)
        {
            draft.resize(maxDraftChars);
        }
        if (cursorInView > draft.size())
        {
            cursorInView = draft.size();
        }
        string inputLine = _active ? string("> ") + draft.substr(0, cursorInView) + "|" + draft.substr(cursorInView) : string("> ");
        QueueChatRect(x + 6.f, y + 6.f, x + width - 6.f, y + inputHeight + 4.f, .07f, .085f, .08f, .92f, false);
        QueueChatText(inputLine.c_str(), x + 10.f, y + 12.f, _active ? .95f : .60f, _active ? .96f : .68f, _active ? .92f : .66f);

        if (_scrollOffset > 0)
        {
            char scrollText[48];
            sprintf(scrollText, "history +%u", (unsigned)_scrollOffset);
            const float scrollTextX = x + width - 8.f - (float)strlen(scrollText) * INPUT_CHAR_WIDTH - (float)(INPUT_CHAR_WIDTH * 5);
            QueueChatText(scrollText, scrollTextX, y + height - 22.f, .95f, .82f, .48f);
        }

        if (!_notice.empty())
        {
            // Reserve the right side of the header for the two voice buttons.
            const int available = App.size.x - 284 - (int)(x + 8.f);
            const size_t chars = available > 0 ? (size_t)available / INPUT_CHAR_WIDTH : 0;
            string notice = _notice;
            if (notice.size() > chars)
                notice = chars > 3 ? notice.substr(0, chars - 3) + "..." : string();
            QueueChatText(notice.c_str(), x + 8.f, y + height + 4.f, .95f, .82f, .48f);
        }
    }
};

