#pragma once

#include <intrin0.inl.h>

class AtomicRefCount
{
private:
	mutable volatile long _count;

public:
	AtomicRefCount() : _count(0) {}
	AtomicRefCount(const AtomicRefCount&) : _count(0) {}

	AtomicRefCount& operator=(const AtomicRefCount&)
	{
		_count = 0;
		return *this;
	}

	virtual ~AtomicRefCount() {}

	__int32 AddRef() const { return _InterlockedIncrement(&_count); }

	__int32 Release() const
	{
		const __int32 ret = _InterlockedDecrement(&_count);
		if (ret == 0)
			delete const_cast<AtomicRefCount*>(this);
		return ret;
	}

	__int32 referenceCount() const { return _count; }
};

template <class Type>
class IntrusivePtr
{
private:
	Type* _ref = nullptr;

public:
	IntrusivePtr() = default;

	IntrusivePtr(Type* source) : _ref(source)
	{
		if (_ref)
			_ref->AddRef();
	}

	IntrusivePtr(const IntrusivePtr& sRef) : IntrusivePtr(sRef._ref) {}

	IntrusivePtr& operator=(Type* source)
	{
		Type* old = _ref;
		if (source)
			source->AddRef();
		_ref = source;
		if (old)
			old->Release();
		return *this;
	}

	IntrusivePtr& operator=(const IntrusivePtr& sRef)
	{
		return operator=(sRef._ref);
	}

	__forceinline ~IntrusivePtr()
	{
		if (_ref)
			_ref->Release();
	}

	__forceinline Type* GetRef() const { return _ref; }
	__forceinline operator bool() const { return _ref != nullptr; }
	__forceinline Type* operator->() const { return _ref; }
};
