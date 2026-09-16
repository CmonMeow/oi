#pragma once
#include <fstream>
#include <string>

enum ClientSettingBits
{
    CSVoiceEnabled = 1 << 0,
};

struct PackedClientSettings
{
    unsigned char bits;
    unsigned char talkKey = VK_SHIFT;
    std::string serverAddress = DEFAULT_NETWORK_ADDRESS;

    PackedClientSettings()
        : bits(CSVoiceEnabled)
    {
        load();
    }

    void load()
    {
        std::ifstream file("ClientSettings", std::ios::binary);
        if (file.good())
        {
            unsigned char savedBits = 0;
            file.read((char*)&savedBits, sizeof(savedBits));
            if (file.gcount() == sizeof(savedBits))
            {
                bits = savedBits;
                unsigned char savedKey = 0;
                if (file.read((char*)&savedKey, sizeof(savedKey)) && savedKey >= VK_BACK && savedKey != VK_ESCAPE && savedKey < 255)
                    talkKey = savedKey;
                // Optional extension after the original two settings bytes.
                // Old files keep the built-in server default.
                char address[256] = {};
                if (file.getline(address, sizeof(address)) && address[0])
                    serverAddress = address;
            }
        }
    }

    void save() const
    {
        std::ofstream file("ClientSettings", std::ios::binary | std::ios::trunc);
        file.write((const char*)&bits, sizeof(bits));
        file.write((const char*)&talkKey, sizeof(talkKey));
        file << serverAddress << '\n';
    }

    void setServerAddress(const std::string& address)
    {
        if (address.empty() || address.size() > 255 || address.find_first_of("\r\n") != std::string::npos || address == serverAddress) return;
        serverAddress = address;
        save();
    }

    bool voiceEnabled() const
    {
        return (bits & CSVoiceEnabled) != 0;
    }

    void toggleVoiceEnabled()
    {
        bits ^= CSVoiceEnabled;
        save();
    }

    void setTalkKey(unsigned char key)
    {
        talkKey = key;
        save();
    }

    string talkKeyLabel() const
    {
        char label[64] = {};
        UINT scan = MapVirtualKeyA(talkKey, MAPVK_VK_TO_VSC);
        LONG keyData = (LONG)(scan << 16);
        if (talkKey == VK_LEFT || talkKey == VK_RIGHT || talkKey == VK_UP || talkKey == VK_DOWN ||
            talkKey == VK_HOME || talkKey == VK_END || talkKey == VK_PRIOR || talkKey == VK_NEXT ||
            talkKey == VK_INSERT || talkKey == VK_DELETE || talkKey == VK_DIVIDE || talkKey == VK_NUMLOCK)
            keyData |= 1 << 24;
        if (!GetKeyNameTextA(keyData, label, sizeof(label))) strcpy_s(label, "Key");
        string result = string("PTT:") + label;
        return result.size() > 10 ? result.substr(0, 9) + "." : result;
    }

};

