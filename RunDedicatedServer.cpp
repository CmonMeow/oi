#include <winsock2.h>
#include "sysdef.h"
#include "NetworkProtocol.h"
#include <map>
#include <deque>
#include <algorithm>
#include <sstream>
#include <conio.h>
#include "NetworkRuntime.h"
#include "NetworkConsole.h"
int RunDedicatedServer()
{
    char path[MAX_PATH];
    if (GetModuleFileNameA(NULL, path, MAX_PATH)) { char* slash = strrchr(path, '\\'); if (slash) { *slash = 0; SetCurrentDirectoryA(path); } }
    cNetworkRuntime network(NULL);
    if (!network.hostOnPort()) return EXIT_FAILURE;
    printf("oi server listening on UDP port %u. /quit to exit.\n", DEFAULT_NETWORK_PORT);
    bool quit = false, voice = false;
    while (!quit) { network.update(); string line; if (PollConsoleLine(line)) RunConsoleCommand(line, network, quit, voice); Sleep(10); }
    return EXIT_SUCCESS;
}
