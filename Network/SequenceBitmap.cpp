#include "SequenceBitmap.hpp"

#include <algorithm>

const __int64 SequenceBitmap::END = 0x7FFFFFFFFFFFFFFFLL;

static __int64 alignDown32(__int64 value)
{
	return value & -32;
}

static __int64 alignUp32(__int64 value)
{
	return (value + 31) & -32;
}

SequenceBitmap::SequenceBitmap()
{
	min = max = 0;
}

SequenceBitmap::SequenceBitmap(const SequenceBitmap& b)
{
	this->operator=(b);
}

SequenceBitmap& SequenceBitmap::operator=(const SequenceBitmap& b)
{
	if (this == &b)
		return *this;
	words = b.words;
	min = b.min;
	max = b.max;
	return *this;
}

void SequenceBitmap::empty()
{
	words.clear();
	min = max = 0;
}

SequenceBitmap::~SequenceBitmap()
{
	empty();
}

void SequenceBitmap::access(__int64 value)
{
	if (words.empty())
	{
		words.assign(1, 0);
		min = alignDown32(value);
		max = min + 32;
		return;
	}
	if (value < min)
	{
		const __int64 add = (min - value + 31) >> 5;
		words.insert(words.begin(), add, 0);
		min -= add << 5;
		return;
	}
	if (value >= max)
	{
		const __int64 newWords = (value - min + 32) >> 5;
		if (newWords <= static_cast<__int64>(words.size()))
			return;
		words.resize(newWords, 0);
		max = min + (newWords << 5);
	}
}

void SequenceBitmap::compact()
{
	__int64 mMin = getFirst();
	if (mMin == END)
	{
		empty();
		return;
	}
	mMin = alignDown32(mMin);
	const __int64 mMax = alignUp32(getLast() + 1);
	const __int64 newWords = (mMax - mMin) >> 5;
	if (newWords == static_cast<__int64>(words.size()))
		return;

	const __int64 firstWord = (mMin - min) >> 5;
	std::vector<unsigned __int32> newMask(newWords);
	std::copy(words.begin() + firstWord, words.begin() + firstWord + newWords, newMask.begin());
	words.swap(newMask);
	min = mMin;
	max = mMax;
}

void SequenceBitmap::growOptimize(bool up, __int64 anchor)
{
	if (words.empty())
		return;

	if (up)
	{
		__int64 m = getFirst();
		if (m == END)
		{
			if (anchor != END)
			{
				const __int64 shift = alignDown32(anchor) - min;
				min += shift;
				max += shift;
			}
			return;
		}

		__int64 shift = m - min;
		if (shift >= 32)
		{
			shift = alignDown32(shift);
			const __int64 wordShift = shift >> 5;
			if (wordShift >= static_cast<__int64>(words.size()))
				std::fill(words.begin(), words.end(), 0);
			else
			{
				std::move(words.begin() + wordShift, words.end(), words.begin());
				std::fill(words.end() - wordShift, words.end(), 0);
			}
			min += shift;
			max += shift;
		}
	}
	else
	{
		__int64 m = getLast();
		if (m == END)
		{
			if (anchor != END)
			{
				const __int64 shift = alignUp32(anchor + 1) - max;
				min += shift;
				max += shift;
			}
			return;
		}

		__int64 shift = max - m + 1;
		if (shift >= 32)
		{
			shift = alignDown32(shift);
			const __int64 wordShift = shift >> 5;
			if (wordShift >= static_cast<__int64>(words.size()))
				std::fill(words.begin(), words.end(), 0);
			else
			{
				std::move_backward(words.begin(), words.end() - wordShift, words.end());
				std::fill(words.begin(), words.begin() + wordShift, 0);
			}
			min -= shift;
			max -= shift;
		}
	}
}

void SequenceBitmap::emptyOptimize(bool up, __int64 origin)
{
	if (words.empty())
		return;

	if (max - min > 32)
	{
		if (up)
		{
			if (origin == END)
				min = max - 32;
			else
				min = alignDown32(origin);
			max = min + (static_cast<__int64>(words.size()) << 5);
		}
		else
		{
			if (origin == END)
				max = min + 32;
			else
				max = alignDown32(origin) + 32;
			min = max - (static_cast<__int64>(words.size()) << 5);
		}
	}
	std::fill(words.begin(), words.end(), 0);
}

void SequenceBitmap::setUnsafe(__int64 value, bool flag)
{
	const __int64 offset = value - min;
	const unsigned __int32 bit = 1u << (offset & 31);
	if (flag)
		words[offset >> 5] |= bit;
	else
		words[offset >> 5] &= ~bit;
}

void SequenceBitmap::set(__int64 value, bool flag)
{
	if (flag)
	{
		access(value);
		setUnsafe(value, true);
		return;
	}
	if (value < min || value >= max)
		return;
	setUnsafe(value, false);
}

void SequenceBitmap::on(__int64 value)
{
	access(value);
	setUnsafe(value, true);
}

void SequenceBitmap::off(__int64 value)
{
	if (value < min || value >= max)
		return;
	setUnsafe(value, false);
}

void SequenceBitmap::range(__int64 from, __int64 len, bool flag)
{
	if (len <= 0)
		return;

	if (flag)
	{
		access(from);
		access(from + len - 1);
	}
	else
	{
		if (words.empty())
			return;
		if (from < min)
		{
			len -= min - from;
			from = min;
		}
		if (from >= max || len <= 0)
			return;
		if (from + len > max)
			len = max - from;
		if (len <= 0)
			return;
	}

	while (len > 0 && (from & 31))
	{
		setUnsafe(from++, flag);
		len--;
	}

	const unsigned __int32 wordValue = flag ? 0xffffffffu : 0u;
	while (len >= 32)
	{
		words[(from - min) >> 5] = wordValue;
		from += 32;
		len -= 32;
	}

	while (len-- > 0)
		setUnsafe(from++, flag);
}

__int64 SequenceBitmap::card() const
{
	__int64 count = 0;
	for (unsigned __int32 word : words)
	{
		while (word)
		{
			count += word & 1u;
			word >>= 1;
		}
	}
	return count;
}

__int64 SequenceBitmap::getFirst() const
{
	return getNext(min - 1);
}

__int64 SequenceBitmap::getNext(__int64 i) const
{
	if (words.empty() || i >= END - 1)
		return END;

	__int64 value = i + 1;
	if (value < min)
		value = min;
	if (value >= max)
		return END;

	__int64 offset = value - min;
	__int64 wordIndex = offset >> 5;
	unsigned __int32 word = words[wordIndex] & (0xffffffffu << (offset & 31));

	while (wordIndex < static_cast<__int64>(words.size()))
	{
		if (word)
		{
			for (__int64 bit = 0; bit < 32; bit++)
			{
				if (word & (1u << bit))
				{
					const __int64 found = min + (wordIndex << 5) + bit;
					return found < max ? found : END;
				}
			}
		}
		wordIndex++;
		if (wordIndex >= static_cast<__int64>(words.size()))
			break;
		word = words[wordIndex];
	}
	return END;
}

__int64 SequenceBitmap::getLast() const
{
	if (words.empty())
		return END;

	for (__int64 wordIndex = static_cast<__int64>(words.size()) - 1; wordIndex >= 0; --wordIndex)
	{
		unsigned __int32 word = words[wordIndex];
		if (!word)
			continue;
		for (__int64 bit = 31; bit >= 0; --bit)
		{
			if (word & (1u << bit))
			{
				const __int64 found = min + (wordIndex << 5) + bit;
				return found < max ? found : END;
			}
		}
	}
	return END;
}

SequenceBitmap& SequenceBitmap::operator|=(const SequenceBitmap& b)
{
	const __int64 bFirst = b.getFirst();
	if (bFirst == END)
		return *this;
	const __int64 bLast = b.getLast();
	access(bFirst);
	access(bLast);

	const __int64 firstWordValue = alignDown32(bFirst);
	const __int64 wordCount = (bLast - firstWordValue + 32) >> 5;
	const __int64 src = (firstWordValue - b.min) >> 5;
	const __int64 dst = (firstWordValue - min) >> 5;
	for (__int64 i = 0; i < wordCount; ++i)
		words[dst + i] |= b.words[src + i];
	return *this;
}

SequenceBitmap& SequenceBitmap::operator&=(const SequenceBitmap& b)
{
	if (words.empty())
		return *this;

	const __int64 bFirst = b.getFirst();
	if (bFirst == END)
	{
		empty();
		return *this;
	}
	const __int64 bLast = b.getLast();
	access(bFirst);
	access(bLast);

	if (bFirst > min)
		range(min, bFirst - min, false);
	if (bLast < max - 1)
		range(bLast + 1, max - bLast - 1, false);

	const __int64 firstWordValue = alignDown32(bFirst);
	const __int64 wordCount = (bLast - firstWordValue + 32) >> 5;
	const __int64 src = (firstWordValue - b.min) >> 5;
	const __int64 dst = (firstWordValue - min) >> 5;
	for (__int64 i = 0; i < wordCount; ++i)
		words[dst + i] &= b.words[src + i];
	return *this;
}

SequenceBitmap& SequenceBitmap::operator^=(const SequenceBitmap& b)
{
	const __int64 bFirst = b.getFirst();
	if (bFirst == END)
		return *this;
	const __int64 bLast = b.getLast();
	access(bFirst);
	access(bLast);

	const __int64 firstWordValue = alignDown32(bFirst);
	const __int64 wordCount = (bLast - firstWordValue + 32) >> 5;
	const __int64 src = (firstWordValue - b.min) >> 5;
	const __int64 dst = (firstWordValue - min) >> 5;
	for (__int64 i = 0; i < wordCount; ++i)
		words[dst + i] ^= b.words[src + i];
	return *this;
}

SequenceBitmap& SequenceBitmap::operator-=(const SequenceBitmap& b)
{
	const __int64 bFirst = b.getFirst();
	if (bFirst == END)
		return *this;
	const __int64 bLast = b.getLast();
	access(bFirst);
	access(bLast);

	const __int64 firstWordValue = alignDown32(bFirst);
	const __int64 wordCount = (bLast - firstWordValue + 32) >> 5;
	const __int64 src = (firstWordValue - b.min) >> 5;
	const __int64 dst = (firstWordValue - min) >> 5;
	for (__int64 i = 0; i < wordCount; ++i)
		words[dst + i] &= ~b.words[src + i];
	return *this;
}

void SequenceBitmap::getStat(__int64& minimum, __int64& maximum)
{
	minimum = min;
	maximum = max;
}
