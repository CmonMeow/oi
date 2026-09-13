#pragma once

#include <intrin0.inl.h>

class RefCountSafe
{
private:
	mutable volatile long _count;

public:
	RefCountSafe() : _count(0) {}
	RefCountSafe(const RefCountSafe&) : _count(0) {}

	RefCountSafe& operator=(const RefCountSafe&)
	{
		_count = 0;
		return *this;
	}

	virtual ~RefCountSafe() {}

	__int32 AddRef() const { return _InterlockedIncrement(&_count); }

	__int32 Release() const
	{
		const __int32 ret = _InterlockedDecrement(&_count);
		if (ret == 0)
			delete const_cast<RefCountSafe*>(this);
		return ret;
	}

	__int32 RefCounter() const { return _count; }
};

template <class Type>
class Ref
{
private:
	Type* _ref = nullptr;

public:
	Ref() = default;

	Ref(Type* source) : _ref(source)
	{
		if (_ref)
			_ref->AddRef();
	}

	Ref(const Ref& sRef) : Ref(sRef._ref) {}

	Ref& operator=(Type* source)
	{
		Type* old = _ref;
		if (source)
			source->AddRef();
		_ref = source;
		if (old)
			old->Release();
		return *this;
	}

	Ref& operator=(const Ref& sRef)
	{
		return operator=(sRef._ref);
	}

	__forceinline ~Ref()
	{
		if (_ref)
			_ref->Release();
	}

	__forceinline Type* GetRef() const { return _ref; }
	__forceinline operator bool() const { return _ref != nullptr; }
	__forceinline Type* operator->() const { return _ref; }
};
