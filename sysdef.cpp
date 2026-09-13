
#include "sysdef.h"
Global App;
Input input; 

bool Shift()
{
	return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
}

unsigned char VK2C(unsigned char c)
{
	switch (c)
	{
	case '0': return Shift() ? ')' : '0';
	case '1': return Shift() ? '!' : '1';
	case '2': return Shift() ? '@' : '2';
	case '3': return Shift() ? '#' : '3';
	case '4': return Shift() ? '$' : '4';
	case '5': return Shift() ? '%' : '5';
	case '6': return Shift() ? '^' : '6';
	case '7': return Shift() ? '&' : '7';
	case '8': return Shift() ? '*' : '8';
	case '9': return Shift() ? '(' : '9';
	case 'A': return Shift() ? 'A' : 'a';
	case 'B': return Shift() ? 'B' : 'b';
	case 'C': return Shift() ? 'C' : 'c';
	case 'D': return Shift() ? 'D' : 'd';
	case 'E': return Shift() ? 'E' : 'e';
	case 'F': return Shift() ? 'F' : 'f';
	case 'G': return Shift() ? 'G' : 'g';
	case 'H': return Shift() ? 'H' : 'h';
	case 'I': return Shift() ? 'I' : 'i';
	case 'J': return Shift() ? 'J' : 'j';
	case 'K': return Shift() ? 'K' : 'k';
	case 'L': return Shift() ? 'L' : 'l';
	case 'M': return Shift() ? 'M' : 'm';
	case 'N': return Shift() ? 'N' : 'n';
	case 'O': return Shift() ? 'O' : 'o';
	case 'P': return Shift() ? 'P' : 'p';
	case 'Q': return Shift() ? 'Q' : 'q';
	case 'R': return Shift() ? 'R' : 'r';
	case 'S': return Shift() ? 'S' : 's';
	case 'T': return Shift() ? 'T' : 't';
	case 'U': return Shift() ? 'U' : 'u';
	case 'V': return Shift() ? 'V' : 'v';
	case 'W': return Shift() ? 'W' : 'w';
	case 'X': return Shift() ? 'X' : 'x';
	case 'Y': return Shift() ? 'Y' : 'y';
	case 'Z': return Shift() ? 'Z' : 'z';
	case VK_TAB: return '\t';
	case VK_OEM_MINUS: return Shift() ? '_' : '-';
	case VK_OEM_PLUS: return Shift() ? '+' : '=';
	case VK_OEM_1: return Shift() ? ':' : ';';
	case VK_OEM_2: return Shift() ? '?' : '/';
	case VK_OEM_3: return Shift() ? '~' : '`';
	case VK_OEM_4: return Shift() ? '{' : '[';
	case VK_OEM_5: return Shift() ? '|' : '\\';
	case VK_OEM_6: return Shift() ? '}' : ']';
	case VK_OEM_7: return Shift() ? '"' : '\'';
	case VK_OEM_COMMA: return Shift() ? '<' : ',';
	case VK_OEM_PERIOD: return Shift() ? '>' : '.';
	case VK_SPACE: return ' ';
	case VK_NUMPAD0: return '0';
	case VK_NUMPAD1: return '1';
	case VK_NUMPAD2: return '2';
	case VK_NUMPAD3: return '3';
	case VK_NUMPAD4: return '4';
	case VK_NUMPAD5: return '5';
	case VK_NUMPAD6: return '6';
	case VK_NUMPAD7: return '7';
	case VK_NUMPAD8: return '8';
	case VK_NUMPAD9: return '9';
	case VK_MULTIPLY: return '*';
	case VK_SUBTRACT: return '-';
	case VK_ADD: return '+';
	case VK_DECIMAL: return '.';
	case VK_DIVIDE: return '/';
	default: return 0;
	}
}

void Input::Clear()
{
	std::memset(keys, 0, sizeof(keys));
	std::memset(keyPresses, 0, sizeof(keyPresses));
	textCount = 0;
	mouseWheel = 0;
}
void Input::KeyDown(unsigned char c)
{
	if (!keys[c]) keyPresses[c] = true;
	keys[c] = true;
}
void Input::KeyUp(unsigned char c) { keys[c] = false; }
void Input::TextInput(unsigned char c)
{
	if (textCount < (int)sizeof(text))
		text[textCount++] = c;
}
bool Input::PopTextInput(unsigned char& c)
{
	if (textCount <= 0)
		return false;
	c = text[0];
	if (--textCount > 0)
		std::memmove(text, text + 1, textCount);
	return true;
}
void Input::AddMouseWheel(__int32 delta) { mouseWheel += delta; }
__int32 Input::ConsumeMouseWheel()
{
	__int32 result = mouseWheel;
	mouseWheel = 0;
	return result;
}
bool Input::pressed(unsigned char vk) { return keys[vk]; }
bool Input::ConsumePress(unsigned char vk)
{
	const bool result = keyPresses[vk];
	keyPresses[vk] = false;
	return result;
}
bool Input::toggle(unsigned char vk, bool& op) { if (keys[vk]) op ^= true, KeyUp(vk); return op; }
bool Input::leftClick() { return keys[VK_LBUTTON]; }
bool Input::rightClick() { return keys[VK_RBUTTON]; }
