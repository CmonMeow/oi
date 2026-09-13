#pragma once

class cNetworkRuntime
{
    HWND _hWnd;
    NetTranspServer* _server;
    NetTranspClient* _client;
    vector<__int32> _players;
    vector<NetworkChatLine> _chatLines;
    unsigned __int64 _nextClientHeartbeat;
    unsigned __int64 _nextServerBroadcast;
    __int32 _localPlayerId;
    __int32 _latencyMS;
    __int32 _throughputBPS;
    vector<string> _bannedIdentities;
    vector<string> _moderatorIdentities;
    std::map<__int32, NetworkIdentity> _playerIdentities;
    std::map<__int32, string> _privateChatKeys;
    std::map<__int32, string> _privateChatNames;
    std::map<__int32, string> _pendingLeaveMessages;
    cCryptoSession _clientCrypto;
    std::map<__int32, cCryptoSession> _serverCrypto;
    unsigned char _privateChatPublicKey[crypto_box_PUBLICKEYBYTES];
    unsigned char _privateChatSecretKey[crypto_box_SECRETKEYBYTES];
    bool _privateChatKeyReady;
    std::deque<NetworkVoicePacket> _voicePackets;

    static bool looksLikeSystemLine(const string& line)
    {
        return _strnicmp(line.c_str(), "system:", 7) == 0 ||
               _strnicmp(line.c_str(), "server:", 7) == 0;
    }

    string privateChatPublicKeyBytes() const
    {
        return string(reinterpret_cast<const char*>(_privateChatPublicKey), crypto_box_PUBLICKEYBYTES);
    }

    bool ensurePrivateChatKey()
    {
        if (_privateChatKeyReady)
        {
            return true;
        }
        if (crypto_box_keypair(_privateChatPublicKey, _privateChatSecretKey) != 0)
        {
            return false;
        }
        _privateChatKeyReady = true;
        _privateChatKeys[0] = privateChatPublicKeyBytes();
        _privateChatNames[0] = "system";
        return true;
    }

    void encodeChatKey(NetworkMessageRaw& raw, __int32 player, const string& name, const string& publicKey) const
    {
        raw.putInt32(player);
        raw.putString(name, 48);
        raw.putString(publicKey, crypto_box_PUBLICKEYBYTES);
    }

    bool decodeChatKey(const char* message, __int32 messageSize, __int32& player, string& name, string& publicKey)
    {
        if (!message || messageSize < (__int32)sizeof(NetAppMessageHeader))
        {
            return false;
        }
        const NetAppMessageHeader* header = reinterpret_cast<const NetAppMessageHeader*>(message);
        if (header->type != NAMTChatKey)
        {
            return false;
        }
        NetworkMessageRaw raw(message + sizeof(NetAppMessageHeader), messageSize - (__int32)sizeof(NetAppMessageHeader));
        return raw.getInt32(player) &&
               raw.getString(name, 48) &&
               raw.getString(publicKey, crypto_box_PUBLICKEYBYTES) &&
               publicKey.size() == crypto_box_PUBLICKEYBYTES &&
               raw.fullyRead();
    }

    void sendChatKeyTo(__int32 to, __int32 player, const string& name, const string& publicKey)
    {
        if (!_server)
        {
            return;
        }
        NetworkMessageRaw raw;
        encodeChatKey(raw, player, name, publicKey);
        sendRawFromServer(to, NAMTChatKey, raw, (NetMsgFlags)(NMFGuaranteed | NMFHighPriority));
    }

    void broadcastChatKey(__int32 player, const string& name, const string& publicKey)
    {
        for (size_t i = 0; i < _players.size(); ++i)
        {
            sendChatKeyTo(_players[i], player, name, publicKey);
        }
    }

    void sendKnownChatKeysTo(__int32 to)
    {
        for (std::map<__int32, string>::const_iterator i = _privateChatKeys.begin(); i != _privateChatKeys.end(); ++i)
        {
            std::map<__int32, string>::const_iterator name = _privateChatNames.find(i->first);
            sendChatKeyTo(to, i->first, name != _privateChatNames.end() ? name->second : playerDisplayName(i->first), i->second);
        }
    }

    bool encryptPrivateText(__int32 target, const string& message, string& cipher)
    {
        std::map<__int32, string>::const_iterator key = _privateChatKeys.find(target);
        if (key == _privateChatKeys.end() || key->second.size() != crypto_box_PUBLICKEYBYTES)
        {
            return false;
        }
        cipher.resize(message.size() + crypto_box_SEALBYTES);
        if (crypto_box_seal(reinterpret_cast<unsigned char*>(&cipher[0]),
                            reinterpret_cast<const unsigned char*>(message.data()), message.size(),
                            reinterpret_cast<const unsigned char*>(key->second.data())) != 0)
        {
            cipher.clear();
            return false;
        }
        return true;
    }

    bool decryptPrivateText(const string& cipher, string& message)
    {
        message.clear();
        if (!_privateChatKeyReady || cipher.size() < crypto_box_SEALBYTES)
        {
            return false;
        }
        message.resize(cipher.size() - crypto_box_SEALBYTES);
        if (crypto_box_seal_open(reinterpret_cast<unsigned char*>(&message[0]),
                                 reinterpret_cast<const unsigned char*>(cipher.data()), cipher.size(),
                                 _privateChatPublicKey, _privateChatSecretKey) != 0)
        {
            message.clear();
            return false;
        }
        message = SanitiseChatText(message);
        return !message.empty();
    }

    bool decodePrivateChatForServer(const char* message, __int32 messageSize, __int32& target, string& cipher)
    {
        if (!message || messageSize < (__int32)sizeof(NetAppMessageHeader))
        {
            return false;
        }
        const NetAppMessageHeader* header = reinterpret_cast<const NetAppMessageHeader*>(message);
        if (header->type != NAMTPrivateChat)
        {
            return false;
        }
        NetworkMessageRaw raw(message + sizeof(NetAppMessageHeader), messageSize - (__int32)sizeof(NetAppMessageHeader));
        return raw.getInt32(target) && raw.getString(cipher, 255) && raw.fullyRead();
    }

    bool decodePrivateChatForClient(const char* message, __int32 messageSize, __int32& from, string& name, string& cipher)
    {
        if (!message || messageSize < (__int32)sizeof(NetAppMessageHeader))
        {
            return false;
        }
        const NetAppMessageHeader* header = reinterpret_cast<const NetAppMessageHeader*>(message);
        if (header->type != NAMTPrivateChat)
        {
            return false;
        }
        NetworkMessageRaw raw(message + sizeof(NetAppMessageHeader), messageSize - (__int32)sizeof(NetAppMessageHeader));
        return raw.getInt32(from) && raw.getString(name, 48) && raw.getString(cipher, 255) && raw.fullyRead();
    }

    void sendPrivateChatToClient(__int32 to, __int32 from, const string& fromName, const string& cipher)
    {
        NetworkMessageRaw raw;
        raw.putInt32(from);
        raw.putString(fromName, 48);
        raw.putString(cipher, 255);
        sendRawFromServer(to, NAMTPrivateChat, raw, (NetMsgFlags)(NMFGuaranteed | NMFHighPriority));
    }

    static void OnClientMessage(char* buffer, __int32 bufferSize, void* context)
    {
        static_cast<cNetworkRuntime*>(context)->onClientMessage(buffer, bufferSize);
    }

    static void OnServerMessage(__int32 from, char* buffer, __int32 bufferSize, void* context)
    {
        static_cast<cNetworkRuntime*>(context)->onServerMessage(from, buffer, bufferSize);
    }

    static void OnCreatePlayer(__int32 player, bool, const char* name, unsigned long, void* context)
    {
        static_cast<cNetworkRuntime*>(context)->onCreatePlayer(player, name);
    }

    static void OnDeletePlayer(__int32 player, void* context)
    {
        static_cast<cNetworkRuntime*>(context)->onDeletePlayer(player);
    }

    bool prepareClientMessage(char* buffer, __int32 bufferSize, string& decrypted, const char*& message, __int32& messageSize)
    {
        message = buffer;
        messageSize = bufferSize;
        if (!buffer ||
            bufferSize < (__int32)sizeof(NetAppMessageHeader) ||
            (size_t)bufferSize > NET_MAX_ENCRYPTED_BYTES)
        {
            return false;
        }
        const NetAppMessageHeader* header = reinterpret_cast<const NetAppMessageHeader*>(buffer);
        if (!ValidAppMessageType(header->type))
        {
            return false;
        }
        if (header->type == NAMTEncrypted)
        {
            if (!_clientCrypto.decrypt(buffer, bufferSize, decrypted))
            {
                return false;
            }
            message = decrypted.data();
            messageSize = (__int32)decrypted.size();
            return true;
        }
        return !_clientCrypto.ready() || header->type == NAMTKeyAccept;
    }

    bool prepareServerMessage(__int32 from, char* buffer, __int32 bufferSize, string& decrypted, const char*& message, __int32& messageSize)
    {
        message = buffer;
        messageSize = bufferSize;
        if (!buffer ||
            bufferSize < (__int32)sizeof(NetAppMessageHeader) ||
            (size_t)bufferSize > NET_MAX_ENCRYPTED_BYTES)
        {
            return false;
        }
        const NetAppMessageHeader* header = reinterpret_cast<const NetAppMessageHeader*>(buffer);
        if (!ValidAppMessageType(header->type))
        {
            return false;
        }
        if (header->type == NAMTEncrypted)
        {
            std::map<__int32, cCryptoSession>::iterator found = _serverCrypto.find(from);
            if (found == _serverCrypto.end() || !found->second.decrypt(buffer, bufferSize, decrypted))
            {
                return false;
            }
            message = decrypted.data();
            messageSize = (__int32)decrypted.size();
            return true;
        }
        return header->type == NAMTKeyHello;
    }

    void sendPlainRawFromClient(NetAppMessageType type, const NetworkMessageRaw& raw)
    {
        if (!_client)
        {
            return;
        }
        string payload = BuildAppRawMessage(type, raw);
        DWORD msgID = 0;
        _client->SendMsg((BYTE*)payload.data(), (__int32)payload.size(), msgID, NMFGuaranteed | NMFHighPriority, Ref<NetMessage>());
    }

    void sendPlainRawFromServer(__int32 player, NetAppMessageType type, const NetworkMessageRaw& raw)
    {
        if (!_server)
        {
            return;
        }
        string payload = BuildAppRawMessage(type, raw);
        DWORD msgID = 0;
        _server->SendMsg(player, (BYTE*)payload.data(), (__int32)payload.size(), msgID, NMFGuaranteed | NMFHighPriority, Ref<NetMessage>());
    }

    void beginClientCryptoHandshake()
    {
        string hello;
        if (_clientCrypto.buildClientHello(hello))
        {
            NetworkMessageRaw raw;
            raw.put(hello.data(), (__int32)hello.size());
            sendPlainRawFromClient(NAMTKeyHello, raw);
        }
    }

    static bool saneVoicePacket(const NetworkVoicePacket& packet)
    {
        return (packet.codec == VOICE_CODEC_OPUS || packet.codec == VOICE_CODEC_ADPCM) &&
               packet.sampleRate == VOICE_SAMPLE_RATE &&
               packet.frameSamples == VOICE_SAMPLES_PER_PACKET &&
               !packet.data.empty() &&
               packet.data.size() <= VOICE_MAX_OPUS_BYTES;
    }

    void onClientMessage(char* buffer, __int32 bufferSize)
    {
        string decrypted;
        const char* message = NULL;
        __int32 messageSize = 0;
        if (!prepareClientMessage(buffer, bufferSize, decrypted, message, messageSize))
        {
            return;
        }

        NetworkPlayerAssignPacket assignPacket;
        if (ParseAppPacket(message, messageSize, NAMTPlayerAssign, assignPacket))
        {
            _localPlayerId = assignPacket.playerId;

            return;
        }

        NetworkVoicePacket voicePacket;
        if (ParseVoicePacket(message, messageSize, voicePacket))
        {
            if (saneVoicePacket(voicePacket))
            {
                _voicePackets.push_back(voicePacket);
                while (_voicePackets.size() > 32)
                {
                    _voicePackets.pop_front();
                }
            }
            return;
        }

        __int32 keyPlayer = -1;
        string keyName;
        string publicKey;
        if (decodeChatKey(message, messageSize, keyPlayer, keyName, publicKey))
        {
            if (keyPlayer >= 0)
            {
                _privateChatKeys[keyPlayer] = publicKey;
                _privateChatNames[keyPlayer] = keyName;
                _playerIdentities[keyPlayer].name = keyName;
            }
            return;
        }

        __int32 privateFrom = -1;
        string privateName;
        string privateCipher;
        if (decodePrivateChatForClient(message, messageSize, privateFrom, privateName, privateCipher))
        {
            string privateText;
            if (decryptPrivateText(privateCipher, privateText))
            {
                addChatLine(string("(private) ") + privateName + ": " + privateText, CLKPrivate);
            }
            return;
        }

        string text;
        if (ParseAppRawString(message, messageSize, NAMTConnect, text, CHAT_MAX_LINE_CHARS) ||
            ParseAppRawString(message, messageSize, NAMTDisconnect, text, CHAT_MAX_LINE_CHARS))
        {
            addChatLine(text);
            return;
        }
        if (ParseAppRawString(message, messageSize, NAMTChat, text, CHAT_MAX_LINE_CHARS))
        {
            addChatLine(SanitiseChatLine(text));
            return;
        }

        if (ParseAppRawControl(message, messageSize, NAMTHeartbeat))
        {
            return;
        }
        string keyBytes;
        if (ParseAppRawBytes(message, messageSize, NAMTKeyAccept, keyBytes, crypto_kx_PUBLICKEYBYTES))
        {
            if (_clientCrypto.acceptServerKey(keyBytes))
            {
                Error("oi: client crypto accepted server key.");
                sendRawIdentityFromClient();
                sendPrivateChatKeyFromClient();
            }
            return;
        }
        addChatLine(string("system: ") + string(message, message + messageSize), CLKSystem);
    }

    void onServerMessage(__int32 from, char* buffer, __int32 bufferSize)
    {
        string decrypted;
        const char* message = NULL;
        __int32 messageSize = 0;
        if (!prepareServerMessage(from, buffer, bufferSize, decrypted, message, messageSize))
        {
            return;
        }
        if (messageSize < (__int32)sizeof(NetAppMessageHeader))
        {
            return;
        }
        const NetAppMessageHeader* messageHeader = reinterpret_cast<const NetAppMessageHeader*>(message);
        if (messageHeader->type != NAMTKeyHello &&
            messageHeader->type != NAMTConnect &&
            _playerIdentities.find(from) == _playerIdentities.end())
        {
            return;
        }

        NetworkVoicePacket voicePacket;
        if (ParseVoicePacket(message, messageSize, voicePacket))
        {
            if (saneVoicePacket(voicePacket))
            {
                voicePacket.playerId = from;
                _voicePackets.push_back(voicePacket);
                while (_voicePackets.size() > 32)
                {
                    _voicePackets.pop_front();
                }
                broadcastVoicePacket(voicePacket, from);
            }
            return;
        }

        __int32 keyPlayer = -1;
        string keyName;
        string publicKey;
        if (decodeChatKey(message, messageSize, keyPlayer, keyName, publicKey))
        {
            _privateChatKeys[from] = publicKey;
            _privateChatNames[from] = playerDisplayName(from);
            broadcastChatKey(from, playerDisplayName(from), publicKey);
            return;
        }

        __int32 privateTarget = -1;
        string privateCipher;
        if (decodePrivateChatForServer(message, messageSize, privateTarget, privateCipher))
        {
            routePrivateChat(from, privateTarget, privateCipher);
            return;
        }

        string rawChat;
        if (ParseAppRawString(message, messageSize, NAMTChat, rawChat, CHAT_MAX_MESSAGE_CHARS))
        {
            handleChatFromClient(from, rawChat);
            return;
        }
        string keyBytes;
        if (ParseAppRawBytes(message, messageSize, NAMTKeyHello, keyBytes, crypto_kx_PUBLICKEYBYTES))
        {
            if (_playerIdentities.find(from) != _playerIdentities.end())
            {
                return;
            }
            string response;
            if (_serverCrypto[from].acceptClientHello(keyBytes, response))
            {
                Error("oi: server accepted crypto hello from %d.", from);
                NetworkMessageRaw raw;
                raw.put(response.data(), (__int32)response.size());
                sendPlainRawFromServer(from, NAMTKeyAccept, raw);
            }
            else
            {
                _server->KickOff(from, NTRKicked, "crypto failed");
            }
            return;
        }
        NetworkIdentity rawIdentity;
        if (ParseIdentityRaw(message, messageSize, rawIdentity))
        {
            if (std::find(_bannedIdentities.begin(), _bannedIdentities.end(), rawIdentity.id) != _bannedIdentities.end())
            {
                _server->KickOff(from, NTRBanned, "banned");
                return;
            }
            if (_playerIdentities.find(from) != _playerIdentities.end()) return;
            for (std::map<__int32, NetworkIdentity>::const_iterator i = _playerIdentities.begin(); i != _playerIdentities.end(); ++i)
            {
                if (i->second.id.substr(0, IDENTITY_SUFFIX_CHARS) == rawIdentity.id.substr(0, IDENTITY_SUFFIX_CHARS))
                {
                    _server->KickOff(from, NTRKicked, "Identity already connected or suffix collision.");
                    return;
                }
            }
            rawIdentity.name = IdentityDisplayName(rawIdentity.name, rawIdentity.id);
            _playerIdentities[from] = rawIdentity;
            sendInitialStateTo(from);
            return;
        }

        if (messageHeader->type == NAMTConnect)
        {
            _server->KickOff(from, NTRKicked, "Incompatible identity; update your client.");
            return;
        }

        if (ParseAppRawControl(message, messageSize, NAMTHeartbeat))
        {
            return;
        }
        if (ParseAppRawControl(message, messageSize, NAMTDisconnect))
        {
            _pendingLeaveMessages[from] = "client disconnecting";
            return;
        }

        return;
    }

    void onCreatePlayer(__int32 player, const char* name)
    {
        if (std::find(_players.begin(), _players.end(), player) == _players.end())
        {
            _players.push_back(player);
        }
    }

    void onDeletePlayer(__int32 player)
    {
        std::map<__int32, string>::iterator pending = _pendingLeaveMessages.find(player);
        string leaveMessage;
        if (pending != _pendingLeaveMessages.end())
        {
            leaveMessage = pending->second;
            _pendingLeaveMessages.erase(pending);
        }
        else
        {
            leaveMessage = playerDisplayName(player) + " left";
        }

        _players.erase(std::remove(_players.begin(), _players.end(), player), _players.end());
        addChatLine(string("system: ") + leaveMessage, CLKSystem);
        sendRawStringFromServerToAll(NAMTDisconnect, string("system: ") + leaveMessage, CHAT_MAX_LINE_CHARS);

        _playerIdentities.erase(player);
        _privateChatKeys.erase(player);
        _privateChatNames.erase(player);
        _serverCrypto.erase(player);
    }

    void sendInitialStateTo(__int32 player)
    {
        if (!_server)
        {
            return;
        }
        NetworkPlayerAssignPacket assignPacket;
        assignPacket.playerId = player;
        sendPacketFromServer(player, NAMTPlayerAssign, assignPacket);

        sendKnownChatKeysTo(player);

        std::ostringstream status;
        status << "system: " << playerDisplayName(player) << " joined";
        addChatLine(status.str(), CLKSystem);
        sendRawStringFromServerToAll(NAMTConnect, status.str(), CHAT_MAX_LINE_CHARS);
    }

    void addChatLine(const string& line, ChatLineKind kind = CLKNormal)
    {
        if (line.empty())
        {
            return;
        }
        if (kind == CLKNormal && looksLikeSystemLine(line))
        {
            kind = CLKSystem;
        }
        if (!_hWnd) printf("%s\n", SanitiseChatLine(line).c_str());
        string text = SanitiseChatLine(line);
        size_t offset = 0;
        while (offset < text.size())
        {
            size_t remaining = text.size() - offset;
            size_t count = CHAT_WRAP_CHARS < remaining ? CHAT_WRAP_CHARS : remaining;
            if (offset + count < text.size())
            {
                size_t split = text.rfind(' ', offset + count);
                if (split != string::npos && split > offset)
                {
                    count = split - offset;
                }
            }
            NetworkChatLine chatLine;
            chatLine.text = text.substr(offset, count);
            chatLine.kind = kind;
            _chatLines.push_back(chatLine);
            offset += count;
            while (offset < text.size() && text[offset] == ' ')
            {
                ++offset;
            }
        }
        while (_chatLines.size() > CHAT_MAX_HISTORY_LINES)
        {
            _chatLines.erase(_chatLines.begin());
        }
    }

    template<class T>
    void sendPacketFromClient(NetAppMessageType type, const T& packet)
    {
        if (!_client)
        {
            return;
        }
        string payload = BuildAppPacket(type, packet);
        string encrypted;
        if (!_clientCrypto.encrypt(payload, encrypted))
        {
            return;
        }
        payload = encrypted;
        DWORD msgID = 0;
        _client->SendMsg((BYTE*)payload.data(), (__int32)payload.size(), msgID, NMFGuaranteed | NMFHighPriority, Ref<NetMessage>());
    }

    template<class T>
    void sendPacketFromServer(__int32 player, NetAppMessageType type, const T& packet)
    {
        if (!_server)
        {
            return;
        }
        string payload = BuildAppPacket(type, packet);
        std::map<__int32, cCryptoSession>::iterator found = _serverCrypto.find(player);
        string encrypted;
        if (found == _serverCrypto.end() || !found->second.encrypt(payload, encrypted))
        {
            return;
        }
        payload = encrypted;
        DWORD msgID = 0;
        _server->SendMsg(player, (BYTE*)payload.data(), (__int32)payload.size(), msgID, NMFGuaranteed | NMFHighPriority, Ref<NetMessage>());
    }

    template<class T>
    void sendRealtimePacketFromClient(NetAppMessageType type, const T& packet)
    {
        if (!_client)
        {
            return;
        }
        string payload = BuildAppPacket(type, packet);
        string encrypted;
        if (!_clientCrypto.encrypt(payload, encrypted))
        {
            return;
        }
        payload = encrypted;
        DWORD msgID = 0;
        _client->SendMsg((BYTE*)payload.data(), (__int32)payload.size(), msgID, NMFNone, Ref<NetMessage>());
    }

    template<class T>
    void sendRealtimePacketFromServer(__int32 player, NetAppMessageType type, const T& packet)
    {
        if (!_server)
        {
            return;
        }
        string payload = BuildAppPacket(type, packet);
        std::map<__int32, cCryptoSession>::iterator found = _serverCrypto.find(player);
        string encrypted;
        if (found == _serverCrypto.end() || !found->second.encrypt(payload, encrypted))
        {
            return;
        }
        payload = encrypted;
        DWORD msgID = 0;
        _server->SendMsg(player, (BYTE*)payload.data(), (__int32)payload.size(), msgID, NMFNone, Ref<NetMessage>());
    }

    void sendRawFromClient(NetAppMessageType type, const NetworkMessageRaw& raw, NetMsgFlags flags)
    {
        if (!_client)
        {
            return;
        }
        string payload = BuildAppRawMessage(type, raw);
        string encrypted;
        if (!_clientCrypto.encrypt(payload, encrypted))
        {
            return;
        }
        DWORD msgID = 0;
        _client->SendMsg((BYTE*)encrypted.data(), (__int32)encrypted.size(), msgID, flags, Ref<NetMessage>());
    }

    void sendRawFromServer(__int32 player, NetAppMessageType type, const NetworkMessageRaw& raw, NetMsgFlags flags)
    {
        if (!_server)
        {
            return;
        }
        string payload = BuildAppRawMessage(type, raw);
        std::map<__int32, cCryptoSession>::iterator found = _serverCrypto.find(player);
        string encrypted;
        if (found == _serverCrypto.end() || !found->second.encrypt(payload, encrypted))
        {
            return;
        }
        DWORD msgID = 0;
        _server->SendMsg(player, (BYTE*)encrypted.data(), (__int32)encrypted.size(), msgID, flags, Ref<NetMessage>());
    }

    void sendRawStringFromClient(NetAppMessageType type, const string& message, size_t maxLength)
    {
        NetworkMessageRaw raw;
        raw.putString(message, maxLength);
        sendRawFromClient(type, raw, (NetMsgFlags)(NMFGuaranteed | NMFHighPriority));
    }

    void sendRawStringFromServer(__int32 player, NetAppMessageType type, const string& message, size_t maxLength)
    {
        NetworkMessageRaw raw;
        raw.putString(message, maxLength);
        sendRawFromServer(player, type, raw, (NetMsgFlags)(NMFGuaranteed | NMFHighPriority));
    }

    void sendRawStringFromServerToAll(NetAppMessageType type, const string& message, size_t maxLength)
    {
        for (size_t i = 0; i < _players.size(); ++i)
        {
            sendRawStringFromServer(_players[i], type, message, maxLength);
        }
    }

    void sendRawControlFromClient(NetAppMessageType type)
    {
        NetworkMessageRaw raw;
        sendRawFromClient(type, raw, (NetMsgFlags)(NMFGuaranteed | NMFHighPriority));
    }

    void sendRawControlFromServer(__int32 player, NetAppMessageType type)
    {
        NetworkMessageRaw raw;
        sendRawFromServer(player, type, raw, (NetMsgFlags)(NMFGuaranteed | NMFHighPriority));
    }

    void sendRawIdentityFromClient()
    {
        NetworkMessageRaw raw;
        EncodeLocalIdentityRaw(raw);
        sendRawFromClient(NAMTConnect, raw, (NetMsgFlags)(NMFGuaranteed | NMFHighPriority));
    }

    void sendPrivateChatKeyFromClient()
    {
        if (!_client || !ensurePrivateChatKey())
        {
            return;
        }
        NetworkMessageRaw raw;
        encodeChatKey(raw, 0, LocalUserName(), privateChatPublicKeyBytes());
        sendRawFromClient(NAMTChatKey, raw, (NetMsgFlags)(NMFGuaranteed | NMFHighPriority));
    }

    void sendVoicePayloadFromClient(const NetworkVoicePacket& packet)
    {
        if (!_client)
        {
            return;
        }
        string payload = BuildVoicePayload(packet);
        string encrypted;
        if (!_clientCrypto.encrypt(payload, encrypted))
        {
            return;
        }
        payload = encrypted;
        DWORD msgID = 0;
        _client->SendMsg((BYTE*)payload.data(), (__int32)payload.size(), msgID, NMFNone, Ref<NetMessage>());
    }

    void sendVoicePayloadFromServer(__int32 player, const NetworkVoicePacket& packet)
    {
        if (!_server)
        {
            return;
        }
        string payload = BuildVoicePayload(packet);
        std::map<__int32, cCryptoSession>::iterator found = _serverCrypto.find(player);
        string encrypted;
        if (found == _serverCrypto.end() || !found->second.encrypt(payload, encrypted))
        {
            return;
        }
        payload = encrypted;
        DWORD msgID = 0;
        _server->SendMsg(player, (BYTE*)payload.data(), (__int32)payload.size(), msgID, NMFNone, Ref<NetMessage>());
    }

    void broadcastVoicePacket(const NetworkVoicePacket& packet, __int32 exceptPlayer)
    {
        for (size_t i = 0; i < _players.size(); ++i)
        {
            if (_players[i] != exceptPlayer)
            {
                sendVoicePayloadFromServer(_players[i], packet);
            }
        }
    }

    string playerDisplayName(__int32 player) const
    {
        if (player == 0)
        {
            return "system";
        }
        std::map<__int32, NetworkIdentity>::const_iterator found = _playerIdentities.find(player);
        if (found != _playerIdentities.end() && !found->second.name.empty())
        {
            return found->second.name;
        }
        std::ostringstream fallback;
        fallback << "player " << player;
        return fallback.str();
    }

    bool isModerator(__int32 player) const
    {
        std::map<__int32, NetworkIdentity>::const_iterator found = _playerIdentities.find(player);
        return found != _playerIdentities.end() &&
               std::find(_moderatorIdentities.begin(), _moderatorIdentities.end(), found->second.id) != _moderatorIdentities.end();
    }

    bool resolvePlayerReference(const string& reference, __int32& player, bool allowBaseName = false) const
    {
        string value = TrimWhitespace(reference);
        if (value.empty())
        {
            return false;
        }

        char* end = NULL;
        long numeric = strtol(value.c_str(), &end, 10);
        if (end && *end == 0 &&
            (std::find(_players.begin(), _players.end(), (__int32)numeric) != _players.end() ||
             _privateChatKeys.find((__int32)numeric) != _privateChatKeys.end()))
        {
            player = (__int32)numeric;
            return true;
        }

        for (std::map<__int32, NetworkIdentity>::const_iterator i = _playerIdentities.begin(); i != _playerIdentities.end(); ++i)
        {
            if (!i->second.id.empty() && _stricmp(i->second.id.substr(0, IDENTITY_SUFFIX_CHARS).c_str(), value.c_str()) == 0)
            {
                player = i->first;
                return true;
            }
        }

        for (std::map<__int32, NetworkIdentity>::const_iterator i = _playerIdentities.begin(); i != _playerIdentities.end(); ++i)
        {
            if (_stricmp(i->second.name.c_str(), value.c_str()) == 0)
            {
                player = i->first;
                return true;
            }
        }
        if (allowBaseName)
        {
            __int32 match = -1;
            bool found = false;
            for (std::map<__int32, NetworkIdentity>::const_iterator i = _playerIdentities.begin(); i != _playerIdentities.end(); ++i)
            {
                const string& name = i->second.name;
                size_t suffix = name.rfind('_');
                if (suffix == string::npos || _stricmp(name.substr(0, suffix).c_str(), value.c_str()) != 0) continue;
                if (found) return false;
                match = i->first;
                found = true;
            }
            if (found) { player = match; return true; }
        }
        return false;
    }

    void sendCommandResult(__int32 player, const string& message)
    {
        if (player == 0 && std::find(_players.begin(), _players.end(), player) == _players.end())
        {
            addChatLine(string("system: ") + message, CLKSystem);
            return;
        }
        sendRawStringFromServer(player, NAMTChat, string("system: ") + message, CHAT_MAX_LINE_CHARS);
    }

    void handleChatFromClient(__int32 from, string text)
    {
        text = SanitiseChatText(text);
        if (text.empty())
        {
            return;
        }
        if (text[0] == '/')
        {
            runPlayerCommand(from, text);
            return;
        }
        std::ostringstream line;
        line << playerDisplayName(from) << ": " << text;
        addChatLine(line.str());
        sendRawStringFromServerToAll(NAMTChat, line.str(), CHAT_MAX_LINE_CHARS);
    }

    void routePrivateChat(__int32 from, __int32 target, const string& cipher)
    {
        if (!_server || cipher.empty())
        {
            return;
        }
        const string fromName = playerDisplayName(from);
        if (target == 0)
        {
            string privateText;
            if (decryptPrivateText(cipher, privateText))
            {
                addChatLine(fromName + ": " + privateText, CLKPrivate);
            }
            return;
        }
        if (std::find(_players.begin(), _players.end(), target) == _players.end())
        {
            sendCommandResult(from, "private target not found");
            return;
        }
        sendPrivateChatToClient(target, from, fromName, cipher);
    }

    bool sendPrivateChatByReferenceInternal(const string& reference, const string& message)
    {
        __int32 target = -1;
        if (!resolvePlayerReference(reference, target))
        {
            addChatLine("system: private target not found", CLKSystem);
            return false;
        }
        string text = SanitiseChatText(message);
        string cipher;
        if (text.empty() || !encryptPrivateText(target, text, cipher))
        {
            addChatLine("system: private key unavailable", CLKSystem);
            return false;
        }
        if (_client)
        {
            NetworkMessageRaw raw;
            raw.putInt32(target);
            raw.putString(cipher, 255);
            sendRawFromClient(NAMTPrivateChat, raw, (NetMsgFlags)(NMFGuaranteed | NMFHighPriority));
            addChatLine(string(">") + playerDisplayName(target) + ": " + text, CLKPrivate);
            return true;
        }
        if (_server)
        {
            sendPrivateChatToClient(target, 0, "system", cipher);
            addChatLine(string(">") + playerDisplayName(target) + ": " + text, CLKPrivate);
            return true;
        }
        return false;
    }

    bool grantModerator(const string& reference, string& result)
    {
        if (!_server)
        {
            result = "host only command";
            return false;
        }

        __int32 player = -1;
        if (!resolvePlayerReference(reference, player))
        {
            result = "player not found";
            return false;
        }

        std::map<__int32, NetworkIdentity>::const_iterator identity = _playerIdentities.find(player);
        if (identity == _playerIdentities.end() || identity->second.id.empty())
        {
            result = "player has no identity yet";
            return false;
        }

        if (AddUniqueString(_moderatorIdentities, identity->second.id))
        {
            SaveModeratorList(_moderatorIdentities);
        }
        result = playerDisplayName(player) + " is now a moderator";
        addChatLine(string("system: ") + result, CLKSystem);
        sendRawStringFromServerToAll(NAMTChat, string("system: ") + result, CHAT_MAX_LINE_CHARS);
        return true;
    }

    void sendUsersList(__int32 to)
    {
        std::ostringstream summary;
        summary << "users online: " << _players.size();
        sendCommandResult(to, summary.str());
        for (size_t i = 0; i < _players.size(); ++i)
        {
            __int32 player = _players[i];
            __int32 latency = 0;
            __int32 throughput = 0;
            if (!_server || !_server->GetConnectionInfo(player, latency, throughput))
            {
                latency = 0;
            }
            std::ostringstream line;
            line << "#" << player << " " << playerDisplayName(player);
            line << " " << latency << "ms";
            sendCommandResult(to, line.str());
        }
    }

    void runPlayerCommand(__int32 from, const string& command)
    {
        if (_stricmp(command.c_str(), "/help") == 0)
        {
            if (from == 0 || isModerator(from))
            {
                sendCommandResult(from, "/kick name|name_hash|netId - kick player");
                sendCommandResult(from, "/ban name|name_hash|netId - ban player");
            }
            if (from == 0)
                sendCommandResult(from, "/gm name_hash|netId - grant moderator");
            return;
        }
        if (MatchesCommand(command, "/name"))
        {
            std::map<__int32, NetworkIdentity>::iterator identity = _playerIdentities.find(from);
            if (identity == _playerIdentities.end())
            {
                sendCommandResult(from, "The server uses the name system.");
                return;
            }
            string name = TrimWhitespace(command.substr(5));
            if (name.empty())
            {
                sendCommandResult(from, "usage: /name username");
                return;
            }
            if (!ValidUserName(name))
            {
                sendCommandResult(from, "usage: /name username (1-32 letters: a-z, A-Z only)");
                return;
            }
            string oldName = identity->second.name;
            identity->second.name = IdentityDisplayName(name, identity->second.id);
            _privateChatNames[from] = identity->second.name;
            std::map<__int32, string>::const_iterator key = _privateChatKeys.find(from);
            if (key != _privateChatKeys.end()) broadcastChatKey(from, identity->second.name, key->second);
            string notice = "system: " + oldName + " is now " + identity->second.name;
            addChatLine(notice, CLKSystem);
            sendRawStringFromServerToAll(NAMTChat, notice, CHAT_MAX_LINE_CHARS);
            return;
        }
        if (_stricmp(command.c_str(), "/users") == 0)
        {
            sendUsersList(from);
            return;
        }
        if (from != 0 && !isModerator(from))
        {
            sendCommandResult(from, "moderator only command");
            return;
        }

        if (MatchesCommand(command, "/kick"))
        {
            string argument = TrimWhitespace(command.size() > 5 ? command.substr(5) : string());
            if (argument.empty())
            {
                sendCommandResult(from, "usage: /kick name|name_hash|netId");
                return;
            }
            sendCommandResult(from, kickPlayer(argument, false) ? "kick sent" : "Player not found or name ambiguous; use name_hash or netId.");
            return;
        }
        if (MatchesCommand(command, "/ban"))
        {
            string argument = TrimWhitespace(command.size() > 4 ? command.substr(4) : string());
            if (argument.empty())
            {
                sendCommandResult(from, "usage: /ban name|name_hash|netId");
                return;
            }
            sendCommandResult(from, kickPlayer(argument, true) ? "ban sent" : "Player not found or name ambiguous; use name_hash or netId.");
            return;
        }
        if (MatchesCommand(command, "/gm"))
        {
            if (from != 0) { sendCommandResult(from, "host only command"); return; }
            string argument = TrimWhitespace(command.size() > 3 ? command.substr(3) : string());
            string result;
            if (argument.empty())
            {
                sendCommandResult(from, "usage: /gm name_hash|netId");
                return;
            }
            sendCommandResult(from, grantModerator(argument, result) ? result : result);
            return;
        }
        sendCommandResult(from, string("unknown command: ") + command);
    }

public:
    cNetworkRuntime(HWND hWnd)
        : _hWnd(hWnd),
          _server(NULL),
          _client(NULL),
          _nextClientHeartbeat(0),
          _nextServerBroadcast(0),
          _localPlayerId(-1),
          _latencyMS(0),
          _throughputBPS(0),
          _privateChatKeyReady(false)
    {
        sodium_init();
        ensurePrivateChatKey();
        addChatLine("Type /help for commands.", CLKSystem);
    }

    ~cNetworkRuntime()
    {
        delete _client;
        delete _server;
    }

    bool hostOnPort(unsigned short port = DEFAULT_NETWORK_PORT)
    {
        if (_server || _client)
        {
            addChatLine("network already active");
            return false;
        }

        _players.clear();
        _playerIdentities.clear();
        _privateChatKeys.clear();
        _privateChatNames.clear();
        _pendingLeaveMessages.clear();
        _serverCrypto.clear();
        _clientCrypto.clear();
        _voicePackets.clear();
        _privateChatKeyReady = false;
        ensurePrivateChatKey();
        _nextServerBroadcast = 0;
        _server = CreateNetServer();
        if (!_server || !_server->Init("oi", "", port))
        {
            addChatLine("host failed");
            Error("Network host failed on port %u", port);
            delete _server;
            _server = NULL;
            return false;
        }

        LoadBanList(_bannedIdentities);
        LoadModeratorList(_moderatorIdentities);
        return true;
    }

    bool connectTo(const string& address)
    {
        if (_client || _server)
        {
            addChatLine("network already active");
            return false;
        }

        if (LocalIdentityId().empty())
        {
            addChatLine("Cannot read the C: volume identity.", CLKSystem);
            return false;
        }
        unsigned short port = DEFAULT_NETWORK_PORT;
        _client = CreateNetClient();
        ConnectResult result = _client ? _client->Init(address, "", false, port, "oi", NULL) : CRError;
        if (result != CROK)
        {
            addChatLine(string("Failed to join. Error: ") + ConnectResultName(result));
            delete _client;
            _client = NULL;
            return false;
        }

        addChatLine(string("joined ") + address);
        beginClientCryptoHandshake();
        return true;
    }

    void update()
    {
        if (_server)
        {
            _server->ProcessPlayers(OnCreatePlayer, OnDeletePlayer, this);
            _server->ProcessUserMessages(OnServerMessage, this);

            unsigned __int64 now = GetTickCount64();
            if (!_players.empty() && now >= _nextServerBroadcast)
            {
                for (size_t i = 0; i < _players.size(); ++i)
                {
                    sendRawControlFromServer(_players[i], NAMTHeartbeat);
                }
                _nextServerBroadcast = now + 5000;
            }
        }

        if (_client)
        {
            _client->ProcessUserMessages(OnClientMessage, this);
            _client->GetConnectionInfo(_latencyMS, _throughputBPS);
            if (_client->IsSessionTerminated())
            {
                string reason = _client->GetWhySessionTerminatedStr();
                addChatLine(reason.empty() ? "disconnected" : reason);
                delete _client;
                _client = NULL;
                _localPlayerId = -1;

            }
            else
            {
                unsigned __int64 now = GetTickCount64();
                if (now >= _nextClientHeartbeat)
                {
                    sendRawControlFromClient(NAMTHeartbeat);
                    _nextClientHeartbeat = now + 3000;
                }
            }
        }

        if (_server)
        {
            __int32 totalLatency = 0;
            __int32 totalThroughput = 0;
            __int32 counted = 0;
            for (size_t i = 0; i < _players.size(); ++i)
            {
                __int32 latency = 0;
                __int32 throughput = 0;
                if (_server->GetConnectionInfo(_players[i], latency, throughput))
                {
                    totalLatency += latency;
                    totalThroughput += throughput;
                    ++counted;
                }
            }
            _latencyMS = counted ? totalLatency / counted : 0;
            _throughputBPS = counted ? totalThroughput / counted : 0;
        }
    }

    bool clientTrafficTotals(unsigned __int64& incoming, unsigned __int64& outgoing) const
    {
        incoming = outgoing = 0;
        if (!_client) return false;
        _client->GetTrafficTotals(incoming, outgoing);
        return true;
    }

    bool clientReady() const { return _client && _localPlayerId >= 0; }

    __int32 latencyMS() const
    {
        return _latencyMS;
    }

    __int32 throughputBPS() const
    {
        return _throughputBPS;
    }

    const vector<NetworkChatLine>& chatLines() const
    {
        return _chatLines;
    }

    bool hasConnection() const
    {
        return _server != NULL || _client != NULL;
    }

    bool isConnectedClient() const
    {
        return _client != NULL;
    }

    void showHelp()
    {
        addChatLine("/help - show commands", CLKSystem);
        addChatLine("/name username - change your name", CLKSystem);
        addChatLine("/host - host a server on UDP port 777", CLKSystem);
        addChatLine("/connect [address] - connect", CLKSystem);
        addChatLine("/disconnect - leave or stop hosting", CLKSystem);
        addChatLine("/clear - clear chat", CLKSystem);
        addChatLine("/users - list connected users", CLKSystem);
        addChatLine("/pm name_hash|netId message - private message", CLKSystem);
        addChatLine("/w name_hash|netId message - private message", CLKSystem);
        addChatLine("/tell name_hash|netId message - private message", CLKSystem);
        addChatLine("/direct name_hash|netId message - private message", CLKSystem);
        if (_server)
        {
            runPlayerCommand(0, "/help");
            if (!_hWnd)
            {
                addChatLine("/quit - exit", CLKSystem);
                addChatLine("/exit - exit", CLKSystem);
                addChatLine("/voice - toggle voice", CLKSystem);
            }
        }
        else if (_client) sendChat("/help");
    }

    void changeName(const string& argument)
    {
        string name = TrimWhitespace(argument);
        if (_server) { addChatLine("The server uses the name system.", CLKSystem); return; }
        if (name.empty())
        {
            addChatLine("usage: /name username", CLKSystem);
            return;
        }
        if (!ValidUserName(name))
        {
            addChatLine("usage: /name username (1-32 letters: a-z, A-Z only)", CLKSystem);
            return;
        }
        std::ofstream file("ClientName.txt", std::ios::trunc);
        file << name << "\n";
        file.close();
        if (!file) { addChatLine("Could not save your name.", CLKSystem); return; }
        if (_client) sendChat("/name " + name);
        else addChatLine("Name saved: " + IdentityDisplayName(name, LocalIdentityId()), CLKSystem);
    }

    bool sendPrivateChatByReference(const string& reference, const string& message)
    {
        return sendPrivateChatByReferenceInternal(reference, message);
    }

    void sendChat(const string& text)
    {
        string message = SanitiseChatText(text);
        if (message.empty())
        {
            return;
        }

        if (_client)
        {
            sendRawStringFromClient(NAMTChat, message, CHAT_MAX_MESSAGE_CHARS);
        }
        else if (_server)
        {
            if (message[0] == '/')
            {
                runPlayerCommand(0, message);
                return;
            }
            string line = string("system: ") + message;
            addChatLine(line, CLKSystem);
            sendRawStringFromServerToAll(NAMTChat, line, CHAT_MAX_LINE_CHARS);
        }
    }

    bool kickPlayer(const string& reference, bool ban)
    {
        if (!_server)
        {
            addChatLine("host only command");
            return false;
        }
        __int32 player = -1;
        if (!resolvePlayerReference(reference, player, true))
        {
            addChatLine("Player not found or name ambiguous; use name_hash or netId.", CLKSystem);
            return false;
        }
        const string displayName = playerDisplayName(player);
        if (ban)
        {
            std::map<__int32, NetworkIdentity>::const_iterator identity = _playerIdentities.find(player);
            if (identity != _playerIdentities.end() &&
                AddUniqueString(_bannedIdentities, identity->second.id))
            {
                SaveBanList(_bannedIdentities);
            }
        }
        std::ostringstream line;
        line << displayName << (ban ? " was banned" : " was kicked");
        _pendingLeaveMessages[player] = line.str();
        _server->KickOff(player, ban ? NTRBanned : NTRKicked, line.str().c_str());
        return true;
    }

    bool makeModerator(const string& reference)
    {
        string result;
        return grantModerator(reference, result);
    }

    bool isHost() const
    {
        return _server != NULL;
    }

    void sendVoice(NetworkVoicePacket packet)
    {
        if (_client)
        {
            packet.playerId = _localPlayerId;
            sendVoicePayloadFromClient(packet);
        }
        else if (_server)
        {
            packet.playerId = 0;
            broadcastVoicePacket(packet, -1);
        }
    }

    bool consumeVoicePacket(NetworkVoicePacket& packet)
    {
        if (_voicePackets.empty())
        {
            return false;
        }
        packet = _voicePackets.front();
        _voicePackets.pop_front();
        return true;
    }

    void clearChat()
    {
        _chatLines.clear();
    }

    bool disconnect()
    {
        if (!_client && !_server) return false;
        const bool hosting = _server != NULL;
        if (_client) sendRawControlFromClient(NAMTDisconnect);
        if (_server)
            sendRawStringFromServerToAll(NAMTDisconnect, "system: Host stopped the server.", CHAT_MAX_LINE_CHARS);
        delete _client;
        delete _server;
        _client = NULL;
        _server = NULL;
        _localPlayerId = -1;
        _players.clear();
        _playerIdentities.clear();
        _privateChatKeys.clear();
        _privateChatNames.clear();
        _pendingLeaveMessages.clear();
        _serverCrypto.clear();
        _clientCrypto.clear();
        _voicePackets.clear();
        _privateChatKeyReady = false;
        ensurePrivateChatKey();
        _nextClientHeartbeat = _nextServerBroadcast = 0;
        _latencyMS = _throughputBPS = 0;
        addChatLine(hosting ? "Hosting stopped." : "disconnected", CLKSystem);
        return true;
    }
};
