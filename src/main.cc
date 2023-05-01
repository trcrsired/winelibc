int main()
{
	auto mdptr{::winelibc::GetModuleHandleW(u"ntdll.dll")};
	::winelibc::GetProcAddress(mdptr);
}