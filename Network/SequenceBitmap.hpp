#ifdef _MSC_VER
#pragma once
#endif

#ifndef BITMASK_H
#define BITMASK_H

#include <vector>

class SequenceBitmap
{
protected:
	std::vector<unsigned __int32> words;

	__int32 min, max;

public:
	
	static const __int32 END;

	SequenceBitmap();

	SequenceBitmap(const SequenceBitmap& b);
	
	SequenceBitmap& operator=(const SequenceBitmap& b);

	virtual ~SequenceBitmap();

	void set(__int32 value, bool flag);

	void on(__int32 value);

	void off(__int32 value);

	void access(__int32 value);

	void compact();

	void growOptimize(bool up, __int32 anchor = END);

	void setUnsafe(__int32 value, bool flag);

	void range(__int32 from, __int32 len, bool flag);

	inline bool get(__int32 value) const
	{
		if (value < min || value >= max)
			return false;
		__int32 offset = value - min;
		return (words[offset >> 5] & (1u << (offset & 31))) != 0;
	}

	inline bool getUnsafe(__int32 value) const
	{
		__int32 offset = value - min;
		return (words[offset >> 5] & (1u << (offset & 31))) != 0;
	}

	void empty();
	void emptyOptimize(bool up, __int32 origin = END);

	__int32 card() const;
	__int32 getFirst() const;
	__int32 getNext(__int32 i) const;
	__int32 getLast() const;

	SequenceBitmap& operator|=(const SequenceBitmap& b);
	SequenceBitmap& operator&=(const SequenceBitmap& b);
	SequenceBitmap& operator^=(const SequenceBitmap& b);
	SequenceBitmap& operator-=(const SequenceBitmap& b);

	void getStat(__int32& minimum, __int32& maximum);
};

#endif
