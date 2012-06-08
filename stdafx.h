#pragma once

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
/* Windows Header Files */
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <tmschema.h>
/* OLE header files */
#include <ole2.h>
#include <shobjidl.h>
#include <shlguid.h>
/* C Runtime Header Files */
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

/* shobjidl.h doesn't define these macros, so we get to! */
#ifdef UNICODE
# define IShellLink_QueryInterface	IShellLinkW_QueryInterface
# define IShellLink_AddRef			IShellLinkW_AddRef
# define IShellLink_Release			IShellLinkW_Release
# define IShellLink_SetPath			IShellLinkW_SetPath
# define IShellLink_SetIDList		IShellLinkW_SetIDList
# define IShellLink_GetIconLocation	IShellLinkW_GetIconLocation
# define IShellLink_GetPath			IShellLinkW_GetPath
# define IShellLink_GetIDList		IShellLinkW_GetIDList
#else
# define IShellLink_QueryInterface	IShellLinkA_QueryInterface
# define IShellLink_AddRef			IShellLinkA_AddRef
# define IShellLink_Release			IShellLinkA_Release
# define IShellLink_SetPath			IShellLinkA_SetPath
# define IShellLink_SetIDList		IShellLinkA_SetIDList
# define IShellLink_GetIconLocation	IShellLinkA_GetIconLocation
# define IShellLink_GetPath			IShellLinkA_GetPath
# define IShellLink_GetIDList		IShellLinkA_GetIDList
#endif