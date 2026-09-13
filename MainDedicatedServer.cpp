
#include <sysdef.h>
#include "RunClient.h"

static LONG WINAPI DedicatedServerExceptionFilter(EXCEPTION_POINTERS* exceptionPointers)
{
    if(exceptionPointers && exceptionPointers->ExceptionRecord)
    {
        Error("Dedicated server exception 0x%08x at %p",
            exceptionPointers->ExceptionRecord->ExceptionCode,
            exceptionPointers->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int main()
{
    ClearLog();
    SetUnhandledExceptionFilter(DedicatedServerExceptionFilter);
    __try
    {
        return RunDedicatedServer();
    }
    __except(DedicatedServerExceptionFilter(GetExceptionInformation()))
    {
        return 1;
    }
}
