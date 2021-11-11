# GDI driver

@ cdecl wine_get_gdi_driver(long) macdrv_get_gdi_driver

# System tray
@ cdecl wine_notify_icon(long ptr)

# IME
@ stdcall ImeConfigure(long long long ptr)
@ stdcall ImeConversionList(long wstr ptr long long)
@ stdcall ImeDestroy(long)
@ stdcall ImeEnumRegisterWord(ptr wstr long wstr ptr)
@ stdcall ImeEscape(long long ptr)
@ stdcall ImeGetImeMenuItems(long long long ptr ptr long)
@ stdcall ImeGetRegisterWordStyle(long ptr)
@ stdcall ImeInquire(ptr wstr wstr)
@ stdcall ImeProcessKey(long long long ptr)
@ stdcall ImeRegisterWord(wstr long wstr)
@ stdcall ImeSelect(long long)
@ stdcall ImeSetActiveContext(long long)
@ stdcall ImeSetCompositionString(long long ptr long ptr long)
@ stdcall ImeToAsciiEx(long long ptr ptr long long)
@ stdcall ImeUnregisterWord(wstr long wstr)
@ stdcall NotifyIME(long long long long)
