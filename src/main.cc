#include"imports/impl.h"

void* __stdcall wine_unix_call_model( ::std::uint_least64_t handle, ::std::uint_least32_t code, void *args ) noexcept;

int main()
{
	auto mdptr{::winelibc::GetModuleHandleW(u"ntdll.dll")};
	if(mdptr==nullptr)
	{
		__builtin_trap();
	}
	auto wineunixcall = ::winelibc::GetProcAddress(mdptr,reinterpret_cast<char const*>(u8"__wine_unix_call"));

	if(wineunixcall==nullptr)
	{
		__builtin_trap();
	}

	auto __wine_unix_call{reinterpret_cast<decltype(wine_unix_call_model)*>(wineunixcall)};

	__wine_unix_call();
//	__builtin_printf("unxc:%p\n",unxc);
}