/*
 * atlbase.h - minimal stand-in for the sliver of ATL that MultiCAD uses.
 * core/AudioHelper.h needs only CComPtr<T>; MinGW-w64 ships no ATL.
 */
#pragma once

#include <unknwn.h>

template <class T>
class CComPtr
{
public:
    T* p;

    CComPtr() noexcept : p(nullptr) {}
    CComPtr(T* lp) noexcept : p(lp) { if (p) p->AddRef(); }
    CComPtr(const CComPtr& o) noexcept : p(o.p) { if (p) p->AddRef(); }
    ~CComPtr() { if (p) p->Release(); }

    CComPtr& operator=(T* lp) noexcept
    {
        if (lp) lp->AddRef();
        if (p)  p->Release();
        p = lp;
        return *this;
    }
    CComPtr& operator=(const CComPtr& o) noexcept { return operator=(o.p); }

    void Release() noexcept
    {
        T* t = p;
        p = nullptr;
        if (t) t->Release();
    }

    operator T*()   const noexcept { return p; }
    T&  operator*()  const noexcept { return *p; }
    T** operator&()  noexcept { return &p; }   /* ATL asserts p==NULL; callers honour that */
    T*  operator->() const noexcept { return p; }
    bool operator!() const noexcept { return p == nullptr; }
    bool operator==(T* rp) const noexcept { return p == rp; }
    bool operator!=(T* rp) const noexcept { return p != rp; }
};
