#pragma once
#include <cstring>
#include <cstdarg>

#include "resource.h"

#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN   
#include <windows.h>
#include <WinInet.h>
#include <io.h>
#include <iostream>
#include <string>

#include <time.h>

#define CLAMP(x, minimum, maximum) ((x) = ((x) < (minimum) ? (minimum) : ((x) > (maximum) ? (maximum) : (x))))
#define MOD(x,y) ((x) % (y))

#ifndef ERR
#define ERR
inline void ClearLog()
{
	FILE* logfile = fopen("Log", "w");
	if(logfile)
	{
		fclose(logfile);
	}
}

// Log failures, exceptions, and unexpected states only; routine activity stays silent.
inline void _cdecl Error(const char* format, ...)
{
	FILE* logfile = fopen("Log", "a");
	if(logfile)
	{
	va_list arglist;
	va_start(arglist, format);
	vfprintf(logfile, format, arglist);
	fprintf(logfile, "\n");
	fclose(logfile);
	va_end(arglist);
	}
}
#endif

#define OP2(type,sign) type& operator sign(type op){x sign op.x;y sign op.y;return *this; }
#define OP3(type,sign) type& operator sign(type op){x sign op.x;y sign op.y;z sign op.z;return *this; }

#ifndef Input_Interface
#define Input_Interface
#include "maff.h"
struct Global
{
	vec2i size = { 800,600 };
	bool quit = false;
	bool OGL = false;
};
extern Global App;

struct Input
{
private:
	bool keys[256];
	bool keyPresses[256];
	char text[64];
	__int32 textCount;
public:
	vec2i mouse;
	void Clear();
	void KeyDown(unsigned char c);
	void KeyUp(unsigned char c);
	void TextInput(unsigned char c);
	bool PopTextInput(unsigned char& c);
	void AddMouseWheel(__int32 delta);
	__int32 ConsumeMouseWheel();
	bool pressed(unsigned char vk);
	bool ConsumePress(unsigned char vk);
	bool toggle(unsigned char vk, bool& op);
	bool leftClick();
	bool rightClick();
	__int32 mouseWheel;
};

extern Input input; 

bool Shift();
unsigned char VK2C(unsigned char c);

#endif

#define YEAR ((((__DATE__ [7] - '0') * 10 + (__DATE__ [8] - '0')) * 10 + (__DATE__ [9] - '0')) * 10 + (__DATE__ [10] - '0'))

#define MONTH (__DATE__ [2] == 'n' ? 1 \
: __DATE__ [2] == 'b' ? 2 \
: __DATE__ [2] == 'r' ? (__DATE__ [0] == 'M' ? 3 : 4) \
: __DATE__ [2] == 'y' ? 5 \
: __DATE__ [2] == 'n' ? 6 \
: __DATE__ [2] == 'l' ? 7 \
: __DATE__ [2] == 'g' ? 8 \
: __DATE__ [2] == 'p' ? 9 \
: __DATE__ [2] == 't' ? 10 \
: __DATE__ [2] == 'v' ? 11 : 12)

#define DAY ((__DATE__ [4] == ' ' ? 0 : __DATE__ [4] - '0') * 10 + (__DATE__ [5] - '0'))

#define VERSION  YEAR*10000+MONTH*100+DAY

