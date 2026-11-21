#pragma once
#include <functional>
#include <string>
// Public member declarations matched to exports of the installed exact UE4SS build.
// No UObject layout, offsets, vtable or engine memory allocator is reimplemented.
namespace RC::Unreal {
class FProperty { public: __declspec(dllimport) int& GetElementSize(); __declspec(dllimport) int GetSize(); };
class FArrayProperty:public FProperty { public: __declspec(dllimport) FProperty*& GetInner(); };
class FBoolProperty:public FProperty { public: __declspec(dllimport) bool GetPropertyValue(const void*); };
class UObject { public:
 __declspec(dllimport) void* GetValuePtrByPropertyName(const wchar_t*);
 __declspec(dllimport) FProperty* GetPropertyByName(const wchar_t*);
 __declspec(dllimport) std::wstring GetName() const;
};
class UFunction;
namespace Hook { __declspec(dllimport) void RegisterProcessEventPostCallback(std::function<void(UObject*,UFunction*,void*)>); }
}
