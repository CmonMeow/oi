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

	__int64 min, max;

public:
	
	static const __int64 END;

	SequenceBitmap();

	SequenceBitmap(const SequenceBitmap& b);
	
	SequenceBitmap& operator=(const SequenceBitmap& b);

	virtual ~SequenceBitmap();

	void set(__int64 value, bool flag);

	void on(__int64 value);

	void off(__int64 value);

	void access(__int64 value);

	void compact();

	void growOptimize(bool up, __int64 anchor = END);

	void setUnsafe(__int64 value, bool flag);

	void range(__int64 from, __int64 len, bool flag);

	inline bool get(__int64 value) const
	{
		if (value < min || value >= max)
			return false;
		__int64 offset = value - min;
		return (words[offset >> 5] & (1u << (offset & 31))) != 0;
	}

	inline bool getUnsafe(__int64 value) const
	{
		__int64 offset = value - min;
		return (words[offset >> 5] & (1u << (offset & 31))) != 0;
	}

	void empty();
	void emptyOptimize(bool up, __int64 origin = END);

	__int64 card() const;
	__int64 getFirst() const;
	__int64 getNext(__int64 i) const;
	__int64 getLast() const;

	SequenceBitmap& operator|=(const SequenceBitmap& b);
	SequenceBitmap& operator&=(const SequenceBitmap& b);
	SequenceBitmap& operator^=(const SequenceBitmap& b);
	SequenceBitmap& operator-=(const SequenceBitmap& b);

	void getStat(__int64& minimum, __int64& maximum);
};

#endif
