#include "stdafx.h"
#include "dock.h"
#include "dockbutton.h"
#include "resource.h"

#pragma warning (disable: 4311)

#define CIDA_GetPIDLFolder(pida) (LPCITEMIDLIST)(((LPBYTE)pida)+(pida)->aoffset[0])
#define CIDA_GetPIDLItem(pida, i) (LPCITEMIDLIST)(((LPBYTE)pida)+(pida)->aoffset[i+1])

DockButton g_dockButtons[DOCKBUTTON_MAX];
UINT g_dockButtonsActive = 0;
ATOM g_dockButtonClass = 0;
UINT g_dockButtonDrag = 0;

ATOM
RegisterDockButtonClass(void)
{
	WNDCLASSEX wc;

	wc.cbSize		 = sizeof(WNDCLASSEX);
	wc.style		 = 0;
	wc.lpfnWndProc	 = DockButtonWindowProc;
	wc.cbClsExtra	 = 0;
	wc.cbWndExtra	 = 0;
	wc.hInstance	 = g_hInstance;
	wc.hIcon		 = NULL;
	wc.hCursor		 = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
	wc.lpszMenuName	 = NULL;
	wc.lpszClassName = TEXT("DockButton");
	wc.hIconSm		 = NULL;

	return RegisterClassEx(&wc);
}

BOOL
CreateDockButton(HWND hWndParent, UINT uPosition, OUT DockButton *newBtn)
{
	/* Take the border into account if we're on the right */
	int btnX = (g_dockEdge == ABE_LEFT)? 0 : 1, btnY = g_dockButtonSize*uPosition;
	TOOLINFO ti;
	RECT clientRect;

	/* Put a one-pixel border between the head button and the others */
	if(uPosition > 0) ++btnY;

	if(!g_dockButtonClass)
	{
		g_dockButtonClass = RegisterDockButtonClass();
		if(!g_dockButtonClass) return FALSE;
	}
	
	/* Initialise the control's settings */
	newBtn->iDropTarget.lpVtbl = &g_dockButtonDropTargetImpl;
	newBtn->uPosition = uPosition;
	GetDockButtonLinkFile(newBtn);
	newBtn->state.hot = newBtn->state.pressed = newBtn->state.exists = FALSE;
	if(uPosition == 0)
		/* Give the header button the application icon */
		memcpy(newBtn->hIcons, g_dockIcons, sizeof(g_dockIcons));

	/* Create the control */
	newBtn->hWnd = CreateWindowEx(0, (LPCTSTR)g_dockButtonClass, TEXT("DockButton"), WS_CHILD, btnX, btnY, g_dockButtonSize, g_dockButtonSize, hWndParent, NULL, g_hInstance, NULL);
	if(!newBtn->hWnd)
		return FALSE;
	/* Create the tooltip */
	if(g_dockHWndTooltip)
	{
		GetWindowRect(newBtn->hWnd, &clientRect);
		ti.cbSize	= sizeof(TOOLINFO);
		ti.uFlags	= TTF_SUBCLASS | TTF_IDISHWND;
		ti.hwnd		= newBtn->hWnd;
		ti.hinst	= NULL;
		ti.uId		= (UINT)newBtn->hWnd;
		ti.lpszText	= LPSTR_TEXTCALLBACK;
		ti.rect		= clientRect;

		SendMessage(g_dockHWndTooltip, TTM_ADDTOOL, 0, (LPARAM) &ti);
	}
	/* Get theme data */
	if(g_hasThemes)
		newBtn->hTheme = pOpenThemeData(newBtn->hWnd, L"BUTTON");
	else
		newBtn->hTheme = NULL;
	/* Register the control as a drop target */
	if(uPosition != 0) RegisterDragDrop(newBtn->hWnd, &newBtn->iDropTarget);
	/* Attach a reference to this struct to the window handle */
	SetProp(newBtn->hWnd, TEXT("DockButton"), (HANDLE)newBtn);
	return TRUE;
}

void
DestroyDockButton(DockButton *btn)
{
	if(btn->hTheme)
		pCloseThemeData(btn->hTheme);
	DestroyWindow(btn->hWnd);
}

BOOL
CreateDockButtons(HWND hWndParent)
{
	UINT i;
	for(i = 0; i < DOCKBUTTON_MAX; ++i)
	{
		if(!CreateDockButton(hWndParent, i, &g_dockButtons[i]))
			return FALSE;
	}
	UpdateDockButtons();
	return TRUE;
}

void
DestroyDockButtons(void)
{
	UINT i;
	for(i = 0; i < DOCKBUTTON_MAX; ++i)
	{
		DestroyDockButton(&g_dockButtons[i]);
	}
}

/* Static low-level methods to change a button's link file */
static void
_SetDockButtonFile(DockButton *btn, LPCTSTR file, BOOL refresh)
{
	HKEY hLinkFileKey;
	TCHAR szLinkFileValue[10];

	_tcsncpy(btn->szLinkFile, file, MAX_PATH);
	btn->szLinkFile[MAX_PATH] = TEXT('\0');
	if(refresh) GetDockButtonLinkFile(btn);
	InvalidateRect(btn->hWnd, NULL, TRUE);

	if(!SUCCEEDED(RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\WinDock\\Buttons"), 0, KEY_WRITE, &hLinkFileKey)))
		return;
	_sntprintf(szLinkFileValue, 9, TEXT("%u"), btn->uPosition);
	RegSetValueEx(hLinkFileKey, szLinkFileValue, 0, REG_SZ, (LPBYTE)file, (DWORD)(_tcslen(file)+1)*sizeof(TCHAR));
	RegCloseKey(hLinkFileKey);
}

static void
_ClearDockButtonFile(DockButton *btn)
{
	HKEY hLinkFileKey;
	TCHAR szLinkFileValue[10];
	UINT i;

	if(!SUCCEEDED(RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\WinDock\\Buttons"), 0, KEY_WRITE, &hLinkFileKey)))
		return;
	_sntprintf(szLinkFileValue, 9, TEXT("%u"), btn->uPosition);
	RegDeleteValue(hLinkFileKey, szLinkFileValue);
	RegCloseKey(hLinkFileKey);

	btn->szLinkFile[0] = TEXT('\0');
	for(i = 0; i < DOCK_ICONS; ++i)
		if(btn->hIcons[i])
		{
			DestroyIcon(btn->hIcons[i]);
			btn->hIcons[i] = NULL;
		}
	btn->lastWriteTime.dwHighDateTime = btn->lastWriteTime.dwLowDateTime = 0;
	btn->creationTime.dwHighDateTime = btn->creationTime.dwLowDateTime = 0;
	InvalidateRect(btn->hWnd, NULL, TRUE);
}

static void
_MoveDockButtonFile(DockButton *destBtn, DockButton *srcBtn)
{
	HKEY hLinkFileKey;
	TCHAR szLinkFileDestValue[10], szLinkFileSrcValue[10];
	UINT i;
	TCHAR deleteFile[MAX_PATH+1] = "";

	if(!SUCCEEDED(RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\WinDock\\Buttons"), 0, KEY_WRITE, &hLinkFileKey)))
		return;
	_sntprintf(szLinkFileDestValue, 9, TEXT("%u"), destBtn->uPosition);
	_sntprintf(szLinkFileSrcValue,  9, TEXT("%u"), srcBtn->uPosition);
	RegDeleteValue(hLinkFileKey, szLinkFileSrcValue);
	if(!DOCKBUTTON_EMPTY(*srcBtn))
		RegSetValueEx(hLinkFileKey, szLinkFileDestValue, 0, REG_SZ, (LPBYTE)srcBtn->szLinkFile, (DWORD)(_tcslen(srcBtn->szLinkFile)+1)*sizeof(TCHAR));
	RegCloseKey(hLinkFileKey);

	for(i = 0; i < DOCK_ICONS; ++i)
		if(destBtn->hIconsBak[i])
			DestroyIcon(destBtn->hIconsBak[i]);
	if(!DOCKBUTTON_EMPTY(*destBtn))
	{
		_tcsncpy(deleteFile, destBtn->szLinkFile, MAX_PATH);
		deleteFile[MAX_PATH] = TEXT('\0');
	}

	destBtn->lastWriteTime = srcBtn->lastWriteTime;
	destBtn->creationTime = srcBtn->creationTime;
	CopyMemory(destBtn->hIcons, srcBtn->hIconsBak, sizeof(srcBtn->hIcons));
	_tcsncpy(destBtn->szLinkFile, srcBtn->szLinkFile, MAX_PATH);
	destBtn->szLinkFile[MAX_PATH] = TEXT('\0');
	ZeroMemory(srcBtn->hIcons, sizeof(srcBtn->hIcons));
	srcBtn->szLinkFile[0] = TEXT('\0');
	srcBtn->creationTime.dwHighDateTime = srcBtn->creationTime.dwLowDateTime = 0;
	srcBtn->lastWriteTime.dwHighDateTime = srcBtn->lastWriteTime.dwLowDateTime = 0;

	srcBtn->state.hot = FALSE;
	destBtn->state.hot = FALSE;
	InvalidateRect(srcBtn->hWnd, NULL, TRUE);
	InvalidateRect(destBtn->hWnd, NULL, TRUE);
	if(deleteFile[0] != TEXT('\0'))
		DeleteFile(deleteFile);
}

LRESULT CALLBACK
DockButtonWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	DockButton *btn = GetProp(hWnd, TEXT("DockButton"));
	static BOOL dragged = FALSE;

	switch(Msg)
	{
	case WM_THEMECHANGED:
		/* Reopen the theme data */
		if(btn->hTheme)
			pCloseThemeData(btn->hTheme);
		btn->hTheme = pOpenThemeData(btn->hWnd, L"BUTTON");
		InvalidateRect(hWnd, NULL, TRUE);
		break;

	case WM_MOUSEMOVE:
		if(btn->state.pressed)
		{
			/* Since we've captured the mouse, do our own hot tracking */
			int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
			if(x < 0 || y < 0 || x >= (signed)g_dockButtonSize || y >= (signed)g_dockButtonSize)
			{
				if(btn->state.hot)
				{
					btn->state.hot = FALSE;
					InvalidateRect(hWnd, NULL, TRUE);
				}
				/* See if we're dragging to another button */
				if(btn->uPosition != 0 && x >= 0 && x < (signed)g_dockButtonSize)
				{

					UINT dragButton = btn->uPosition;
					if(y >= 0)
						dragButton += (y/g_dockButtonSize);
					else
						dragButton -= (-y/g_dockButtonSize + 1);

					if(dragButton >= 1 && dragButton < g_dockButtonsActive && dragButton != g_dockButtonDrag)
					{
						dragged = TRUE;
						/* Restore the button we're moving past and change the one we're moving to */
						if(g_dockButtonDrag == 0)
						{
							CopyMemory(btn->hIconsBak, btn->hIcons, sizeof(btn->hIcons));
							ZeroMemory(btn->hIcons, sizeof(btn->hIcons));
							InvalidateRect(hWnd, NULL, TRUE);
						}
						else
						{
							CopyMemory(g_dockButtons[g_dockButtonDrag].hIcons, g_dockButtons[g_dockButtonDrag].hIconsBak, sizeof(btn->hIcons));
							g_dockButtons[g_dockButtonDrag].state.hot = FALSE;
							InvalidateRect(g_dockButtons[g_dockButtonDrag].hWnd, NULL, TRUE);
						}
						CopyMemory(g_dockButtons[dragButton].hIconsBak, g_dockButtons[dragButton].hIcons, sizeof(btn->hIcons));
						CopyMemory(g_dockButtons[dragButton].hIcons, btn->hIconsBak, sizeof(btn->hIcons));
						g_dockButtons[dragButton].state.hot = TRUE;
						InvalidateRect(g_dockButtons[dragButton].hWnd, NULL, TRUE);
						g_dockButtonDrag = dragButton;
					}
				}
				else if(g_dockButtonDrag)
				{
					/* Restore if we drag off the dock */
					CopyMemory(g_dockButtons[g_dockButtonDrag].hIcons, g_dockButtons[g_dockButtonDrag].hIconsBak, sizeof(btn->hIcons));
					CopyMemory(btn->hIcons, btn->hIconsBak, sizeof(btn->hIcons));
					g_dockButtons[g_dockButtonDrag].state.hot = FALSE;
					InvalidateRect(hWnd, NULL, TRUE);
					InvalidateRect(g_dockButtons[g_dockButtonDrag].hWnd, NULL, TRUE);
					g_dockButtonDrag = 0;
				}
			}
			else if(!btn->state.hot)
			{
				btn->state.hot = TRUE;
				if(g_dockButtonDrag)
				{
					CopyMemory(g_dockButtons[g_dockButtonDrag].hIcons, g_dockButtons[g_dockButtonDrag].hIconsBak, sizeof(btn->hIcons));
					CopyMemory(btn->hIcons, btn->hIconsBak, sizeof(btn->hIcons));
					g_dockButtons[g_dockButtonDrag].state.hot = FALSE;
					InvalidateRect(g_dockButtons[g_dockButtonDrag].hWnd, NULL, TRUE);
					g_dockButtonDrag = 0;
				}
				InvalidateRect(hWnd, NULL, FALSE);
			}
		}
		else if(!btn->state.hot)
		{
			/* Get leave notification */
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hWnd;
			tme.dwHoverTime = HOVER_DEFAULT;

			_TrackMouseEvent(&tme);
			btn->state.hot = TRUE;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;

	case WM_MOUSELEAVE:
		btn->state.hot = FALSE;
		InvalidateRect(hWnd, NULL, TRUE);
		break;

	case WM_LBUTTONDOWN:
		btn->state.pressed = TRUE;
		SetCapture(hWnd);
		InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_LBUTTONUP:
		btn->state.pressed = FALSE;
		ReleaseCapture();
		if(g_dockButtonDrag)
		{
			g_dockButtons[g_dockButtonDrag].state.hot = FALSE;
#if 1
			if(DOCKBUTTON_EMPTY(g_dockButtons[g_dockButtonDrag])
				|| !g_dockConfirmations.confirmReplace
				|| MessageBox(g_dockHWnd, TEXT("Replace the contents of this dock button?"), TEXT("Drag Button"), MB_YESNO | MB_ICONQUESTION) == IDYES)
#endif
				_MoveDockButtonFile(&g_dockButtons[g_dockButtonDrag], btn);
#if 1
			else
			{
				CopyMemory(g_dockButtons[g_dockButtonDrag].hIcons, g_dockButtons[g_dockButtonDrag].hIconsBak, sizeof(g_dockButtons[g_dockButtonDrag].hIcons));
				CopyMemory(btn->hIcons, btn->hIconsBak, sizeof(btn->hIcons));
				InvalidateRect(g_dockButtons[g_dockButtonDrag].hWnd, NULL, TRUE);
				InvalidateRect(btn->hWnd, NULL, TRUE);
			}
#endif
			g_dockButtonDrag = 0;
		}
		else if(btn->state.hot && !dragged)
		{
			/* Clicking the head button gives us options */
			if(btn->uPosition == 0)
				OpenOptionsDialog(g_dockHWnd);
			/* Clicking other buttons launches the linked program */
			else if(!DOCKBUTTON_EMPTY(*btn))
				OpenDockButton(btn, NULL);
			/* Clicking an empty button brings up the Select File dialog */
			else
				SelectDockButtonFile(btn);

			btn->state.hot = FALSE;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		dragged = FALSE;
		break;

	case WM_CONTEXTMENU:
		{
			HMENU contexts = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDR_CONTEXTS)),
				  hMenu;
			int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
			
			if(x == -1 || y == -1)
			{
				/* Pop the menu up in the corner if opened by the keyboard */
				RECT wndRect;
				GetWindowRect(hWnd, &wndRect);
				x = (g_dockEdge == ABE_LEFT)? wndRect.left : wndRect.right;
				y = wndRect.top;
			}

			if(btn->uPosition == 0)
			{
				MENUITEMINFO mii;

				/* Pop up the header menu */
				hMenu = GetSubMenu(contexts, SUBMENU_CONTEXTS_HEADERBUTTON);
				/* Set Options as the default item */
				mii.cbSize = sizeof(MENUITEMINFO);
				mii.fMask  = MIIM_STATE;
				mii.fState = MFS_ENABLED | MFS_DEFAULT;
				SetMenuItemInfo(hMenu, ID_HEADERBUTTON_OPTIONS, FALSE, &mii);

				/* Pop up the menu in the parent window's context */
				TrackPopupMenu(
					hMenu,
					((g_dockEdge == ABE_LEFT)? TPM_LEFTALIGN : TPM_RIGHTALIGN) | TPM_RIGHTBUTTON,
					x, y, 0, g_dockHWnd, NULL
				);
			}
			else
			{
				MENUITEMINFO miiDefault, miiDisabled;
				/* Pop up the button menu */
				hMenu = GetSubMenu(contexts, SUBMENU_CONTEXTS_DOCKBUTTONS);
				/* Set default item as appropriate */
				miiDefault.cbSize = miiDisabled.cbSize = sizeof(MENUITEMINFO);
				miiDefault.fMask  = miiDisabled.fMask  = MIIM_STATE;
				miiDefault.fState = MFS_ENABLED | MFS_DEFAULT;
				miiDisabled.fState = MFS_DISABLED;
				if(DOCKBUTTON_EMPTY(*btn))
				{
					/* If empty, disable the Open and Clear items and set Select File
					   as default. */
					SetMenuItemInfo(hMenu, ID_DOCKBUTTONS_OPEN,  FALSE, &miiDisabled);
					SetMenuItemInfo(hMenu, ID_DOCKBUTTONS_CLEAR, FALSE, &miiDisabled);
					SetMenuItemInfo(hMenu, ID_DOCKBUTTONS_SELECTFILE, FALSE, &miiDefault);
					SetMenuItemInfo(hMenu, ID_DOCKBUTTONS_PROPERTIES, FALSE, &miiDisabled);
				}
				else
				{
					/* Set Open as default */
					SetMenuItemInfo(hMenu, ID_DOCKBUTTONS_OPEN, FALSE, &miiDefault);
				}

				TrackPopupMenu(
					hMenu,
					((g_dockEdge == ABE_LEFT)? TPM_LEFTALIGN : TPM_RIGHTALIGN) | TPM_RIGHTBUTTON,
					x, y, 0, hWnd, NULL
				);
			}
		}
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case ID_DOCKBUTTONS_OPEN:
			OpenDockButton(btn, NULL);
			break;
		case ID_DOCKBUTTONS_SELECTFILE:
			SelectDockButtonFile(btn);
			break;
		case ID_DOCKBUTTONS_CLEAR:
            if(!g_dockConfirmations.confirmDelete || MessageBox(g_dockHWnd, TEXT("Are you sure you want to delete this dock button?"), TEXT("Delete"), MB_YESNO | MB_ICONQUESTION) == IDYES)
				ClearDockButton(btn);
			break;
		case ID_DOCKBUTTONS_PROPERTIES:
			OpenDockButton(btn, "properties");
		}
		break;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hWnd, &ps);
			HICON hIcon;
			UINT iconSize, iconCorner;

			if(!btn->hTheme)
			{
				HPEN shadow;

				if(btn->state.hot)
				{
					shadow = CreatePen(PS_SOLID, 0, GetSysColor(btn->state.pressed? COLOR_BTNSHADOW : COLOR_BTNHIGHLIGHT));
					SelectObject(dc, shadow);
					Rectangle(dc, 0, 0, g_dockButtonSize, g_dockButtonSize);
				}
			}
			else
			{
				int state = PBS_NORMAL;
				RECT clientRect;

				if(btn->state.hot)
				{
					if(!dragged && btn->state.pressed)
						state = PBS_PRESSED;
					else
						state = PBS_HOT;
				}

				if(state != PBS_NORMAL)
				{
					GetClientRect(btn->hWnd, &clientRect);
					pDrawThemeBackground(btn->hTheme, dc, BP_PUSHBUTTON, state, &clientRect, &ps.rcPaint);
				}
			}

			if(g_dockButtonSize < 40)
				iconSize = 16;
			else if(g_dockButtonSize < 56)
				iconSize = 32;
			else
				iconSize = 48;

			iconCorner = (g_dockButtonSize - iconSize)/2;
			if(!dragged && btn->state.pressed && btn->state.hot)
				iconCorner += 1;
			
			hIcon = btn->hIcons[DOCK_ICON_SIZE(iconSize)];
			if(hIcon)
				DrawIconEx(dc, iconCorner, iconCorner, hIcon, iconSize, iconSize, 0, NULL, DI_NORMAL);
			
			EndPaint(hWnd, &ps);
		}
		break;

	case TTN_GETDISPINFO:
		{
			SHFILEINFO sfi;
			NMTTDISPINFO *pDispInfo = (NMTTDISPINFO*)lParam;
			pDispInfo->hinst = NULL;
			SHGetFileInfo(btn->szLinkFile, 0, &sfi, sizeof(SHFILEINFO), SHGFI_DISPLAYNAME);
			_tcsncpy(pDispInfo->szText, sfi.szDisplayName, sizeof(pDispInfo->szText)-1);
			pDispInfo->szText[sizeof(pDispInfo->szText)-1] = TEXT('\0');
			pDispInfo->lpszText = pDispInfo->szText;
		}
		break;

	default:
		return DefWindowProc(hWnd, Msg, wParam, lParam);
	}
	return 0;
}

void
ResizeDockButton(const DockButton *btn)
{
	int btnX = (g_dockEdge == ABE_LEFT)? 0 : 1, btnY = g_dockButtonSize*btn->uPosition;
	TOOLINFO ti;
	RECT clientRect;

	if(btn->uPosition > 0) ++btnY;
	MoveWindow(btn->hWnd, btnX, btnY, g_dockButtonSize, g_dockButtonSize, TRUE);
	if(g_dockHWndTooltip)
	{
		/* Resize the tooltip area */
		GetWindowRect(btn->hWnd, &clientRect);
		ti.cbSize	= sizeof(TOOLINFO);
		ti.uFlags	= TTF_SUBCLASS | TTF_IDISHWND;
		ti.hwnd		= btn->hWnd;
		ti.hinst	= NULL;
		ti.uId		= (UINT)btn->hWnd;
		ti.lpszText	= LPSTR_TEXTCALLBACK;
		ti.rect		= clientRect;

		SendMessage(g_dockHWndTooltip, TTM_DELTOOL, 0, (LPARAM) &ti);
		SendMessage(g_dockHWndTooltip, TTM_ADDTOOL, 0, (LPARAM) &ti);
	}
}

void
ResizeDockButtons(void)
{
	UINT i;
	for(i = 0; i < DOCKBUTTON_MAX; ++i)
	{
		ResizeDockButton(&g_dockButtons[i]);
	}
}

void
ShowDockButtons(UINT count)
{
	UINT i;

	if(count == g_dockButtonsActive) return;

	if(count < g_dockButtonsActive)
		/* Hide the remainder */
		for(i = count; i < g_dockButtonsActive; ++i)
			ShowWindow(g_dockButtons[i].hWnd, SW_HIDE);
	else
		for(i = g_dockButtonsActive; i < count; ++i)
			ShowWindow(g_dockButtons[i].hWnd, SW_SHOW);
	g_dockButtonsActive = count;
}

void
ClearDockButton(DockButton *btn)
{
	if(!DOCKBUTTON_EMPTY(*btn))
		DeleteFile(btn->szLinkFile);
}

void
SelectDockButtonFile(DockButton *btn)
{
	TCHAR selectedFile[MAX_PATH];
	OPENFILENAME ofn;
	BOOL bOfnResult;

	selectedFile[0] = TEXT('\0');
	/* Pop up the open file dialog */
	ofn.lStructSize		= sizeof(OPENFILENAME);
	ofn.hwndOwner		= btn->hWnd;
	ofn.hInstance		= NULL;
	ofn.lpstrFilter		= "All Files\0*.*\0";
	ofn.lpstrCustomFilter = NULL;
	ofn.nMaxCustFilter	= 0;
	ofn.nFilterIndex	= 0;
	ofn.lpstrFile		= selectedFile;
	ofn.nMaxFile		= MAX_PATH;
	ofn.lpstrFileTitle	= NULL;
	ofn.nMaxFileTitle	= 0;
	ofn.lpstrInitialDir	= NULL;
	ofn.lpstrTitle		= TEXT("Select docked file");
	ofn.Flags			= OFN_DONTADDTORECENT | OFN_ENABLESIZING | OFN_LONGNAMES | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	ofn.nFileOffset		= 0;
	ofn.nFileExtension	= 0;
	ofn.lpstrDefExt		= NULL;
	ofn.lCustData		= 0;
	ofn.lpfnHook		= NULL;
	ofn.lpTemplateName	= NULL;
	ofn.pvReserved		= NULL;
	ofn.dwReserved		= 0;
	ofn.FlagsEx			= 0;

	bOfnResult = GetOpenFileName(&ofn);
	/* GetOpenFileName may change the current directory; set it back */
	SetCurrentDirectory(g_dockAppDataDir);
	if(bOfnResult)
		if(DOCKBUTTON_EMPTY(*btn) || !g_dockConfirmations.confirmReplace || (MessageBox(g_dockHWnd, TEXT("Replace the current content of this dock button?"), TEXT("Select File"), MB_YESNO | MB_ICONQUESTION) == IDYES))
			SetDockButtonFile(btn, ofn.lpstrFile);
}

BOOL
SetDockButtonFile(DockButton *btn, LPCTSTR lpszFilename)
{
	TCHAR szLinkFileName[MAX_PATH+1];
	BOOL discard;

	if(!DOCKBUTTON_EMPTY(*btn))
		ClearDockButton(btn);
	SHGetNewLinkInfo(lpszFilename, g_dockAppDataDir, szLinkFileName, &discard, 0);
	_SetDockButtonFile(btn, BaseName(szLinkFileName), FALSE);
	btn->szLinkFile[MAX_PATH] = TEXT('\0');
	btn->lastWriteTime.dwHighDateTime = btn->lastWriteTime.dwLowDateTime = 0;
	btn->creationTime.dwHighDateTime = btn->creationTime.dwLowDateTime = 0;

	return CreateShellLinkToFile(szLinkFileName, lpszFilename);
}

BOOL
SetDockButtonObject(DockButton *btn, LPCITEMIDLIST lpidlObject)
{
	TCHAR szLinkFileName[MAX_PATH+1];
	BOOL discard;

	if(!DOCKBUTTON_EMPTY(*btn))
		ClearDockButton(btn);
	SHGetNewLinkInfo((LPCTSTR)lpidlObject, g_dockAppDataDir, szLinkFileName, &discard, SHGNLI_PIDL);
	_SetDockButtonFile(btn, BaseName(szLinkFileName), FALSE);
	btn->szLinkFile[MAX_PATH] = TEXT('\0');
	btn->lastWriteTime.dwHighDateTime = btn->lastWriteTime.dwLowDateTime = 0;
	btn->creationTime.dwHighDateTime = btn->creationTime.dwLowDateTime = 0;

	return CreateShellLinkToObject(szLinkFileName, lpidlObject);
}

BOOL
CreateShellLinkToFile(LPCTSTR lpszLinkFilename, LPCTSTR lpszTargetFilename)
{
	IShellLink *link;
	IPersistFile *linkFile;
#ifndef UNICODE
	wchar_t wLinkFilename[MAX_PATH+1];
#else
# define wLinkFilename lpszLinkFilename
#endif

	if(!SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLink, (LPVOID*)&link)))
		return FALSE;

	IShellLink_SetPath(link, lpszTargetFilename);

	/* Write the link to disk */
	if(!SUCCEEDED(IShellLink_QueryInterface(link, &IID_IPersistFile, (LPVOID*)&linkFile)))
		return FALSE;
#ifndef UNICODE
	MultiByteToWideChar(CP_ACP, 0, lpszLinkFilename, -1, wLinkFilename, MAX_PATH);
#endif
	if(!SUCCEEDED(IPersistFile_Save(linkFile, wLinkFilename, TRUE)))
		return FALSE;
	IPersistFile_Release(linkFile);
#ifdef UNICODE
# undef wLinkFilename
#endif
	return TRUE;
}

BOOL
CreateShellLinkToObject(LPCTSTR lpszLinkFilename, LPCITEMIDLIST lpidlTargetObject)
{
	IShellLink *link;
	IPersistFile *linkFile;
#ifndef UNICODE
	wchar_t wLinkFilename[MAX_PATH+1];
#else
# define wLinkFilename lpszLinkFilename
#endif

	if(!SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLink, (LPVOID*)&link)))
		return FALSE;

	IShellLink_SetIDList(link, lpidlTargetObject);

	/* Write the link to disk */
	if(!SUCCEEDED(IShellLink_QueryInterface(link, &IID_IPersistFile, (LPVOID*)&linkFile)))
		return FALSE;
#ifndef UNICODE
	MultiByteToWideChar(CP_ACP, 0, lpszLinkFilename, -1, wLinkFilename, MAX_PATH);
#endif
	if(!SUCCEEDED(IPersistFile_Save(linkFile, wLinkFilename, TRUE)))
		return FALSE;
	IPersistFile_Release(linkFile);
#ifdef UNICODE
# undef wLinkFilename
#endif
	return TRUE;
}

void
OpenDockButton(const DockButton *btn, LPCTSTR verb)
{
	TCHAR curDir[MAX_PATH+1], filename[MAX_PATH+1];
	filename[MAX_PATH] = TEXT('\0');
	GetCurrentDirectory(MAX_PATH, curDir);
	_tcscpy(filename, curDir);
	_tcsncat(filename, TEXT("\\"), MAX_PATH);
	_tcsncat(filename, btn->szLinkFile, MAX_PATH);
	ShellExecute(g_dockHWnd, verb, filename, NULL, curDir, SW_SHOW);
}

void
GetDockButtonLinkFile(DockButton *btn)
{
	IShellLink *link;
	IPersistFile *linkFile;
#ifndef UNICODE
	wchar_t wLinkFilename[MAX_PATH+1];
#else
# define wLinkFilename btn->szLinkFile
#endif
	TCHAR szLinkFileValue[10], iconFile[MAX_PATH+1];
	DWORD valueType, valueSize;
	HANDLE hLinkFile = NULL;
	HKEY hLinkFileKey = NULL;
	UINT iconIndex;
	SHFILEINFO sfi;
	LPTSTR pszPath = NULL;
	UINT uPidlFlag = 0;
	TCHAR path[MAX_PATH+1];

	/* Delete any icons that this button is currently holding */
	for(iconIndex = 0; iconIndex < DOCK_ICONS; ++iconIndex)
		if(btn->hIcons[iconIndex])
		{
			DestroyIcon(btn->hIcons[iconIndex]);
			btn->hIcons[iconIndex] = NULL;
		}

	/* Get the filename attached to this button */
	if(btn->szLinkFile[0] == TEXT('\0'))
	{
		if(!SUCCEEDED(RegCreateKeyEx(HKEY_CURRENT_USER, TEXT("Software\\WinDock\\Buttons"), 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &hLinkFileKey, NULL)))
			goto error;
		_sntprintf(szLinkFileValue, 9, TEXT("%u"), btn->uPosition);
		valueSize = MAX_PATH;
		if(!SUCCEEDED(RegQueryValueEx(hLinkFileKey, szLinkFileValue, NULL, &valueType, btn->szLinkFile, &valueSize)))
			goto error;
		if(valueType != REG_SZ)
			goto error;
	}

	/* Check if the file exists, and if so get the creation and last access time */
	hLinkFile = CreateFile(btn->szLinkFile, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if(!hLinkFile)
		goto error;
	GetFileTime(hLinkFile, &btn->creationTime, NULL, &btn->lastWriteTime);
	CloseHandle(hLinkFile);
	hLinkFile = NULL;
	RegCloseKey(hLinkFileKey);/*TODO: if(hLinkFileKey) */
	hLinkFileKey = NULL;

	/* Load the link file */
	if(!SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLink, (LPVOID*)&link)))
		goto error;
	if(!SUCCEEDED(IShellLink_QueryInterface(link, &IID_IPersistFile, (LPVOID*)&linkFile)))
		goto error;
#ifndef UNICODE
	MultiByteToWideChar(CP_ACP, 0, btn->szLinkFile, -1, wLinkFilename, MAX_PATH);
#endif
	if(!SUCCEEDED(IPersistFile_Load(linkFile, wLinkFilename, STGM_READ)))
		goto not_link;
	IPersistFile_Release(linkFile);

    /* Get the icon location for the link */
	IShellLink_GetIconLocation(link, iconFile, MAX_PATH, &iconIndex);
	if(iconFile[0] == TEXT('\0'))
	{
		/* If the link doesn't have an icon set, use the object's default icon */
		if(SUCCEEDED(IShellLink_GetIDList(link, &(LPITEMIDLIST)pszPath)))
			uPidlFlag |= SHGFI_PIDL;
		else
		{
			IShellLink_GetPath(link, path, MAX_PATH, NULL, 0);
			pszPath = path;
		}
		goto not_link;
	}
	else
	{
		TCHAR expandString[MAX_PATH+1];
		ExpandEnvironmentStrings(iconFile, expandString, MAX_PATH+1);

		/* Load the icon */
		ExtractIconEx(expandString, iconIndex, &btn->hIcons[1], &btn->hIcons[0], 1);
		/* TODO: Get 48x48 icon */
		btn->hIcons[2] = btn->hIcons[1];
		if(g_dockReady)
			InvalidateRect(btn->hWnd, NULL, TRUE);
	}

	IShellLink_Release(link);

	return;

not_link:
	/* Get the object icon from the shell */
	if(!pszPath) pszPath = btn->szLinkFile;
	SHGetFileInfo(pszPath, 0, &sfi, sizeof(SHFILEINFO), uPidlFlag | SHGFI_ICON | SHGFI_SMALLICON);
	btn->hIcons[0] = sfi.hIcon;
	SHGetFileInfo(pszPath, 0, &sfi, sizeof(SHFILEINFO), uPidlFlag | SHGFI_ICON | SHGFI_LARGEICON);
	btn->hIcons[1] = sfi.hIcon;
	/* TODO: Get 48x48 icon */
	btn->hIcons[2] = sfi.hIcon;
    if(g_dockReady)
		InvalidateRect(btn->hWnd, NULL, TRUE);

	return;

error:
	if(hLinkFile) CloseHandle(hLinkFile);
	/* Clear the registry value, if it exists */
	if(hLinkFileKey)
	{
		RegDeleteValue(hLinkFileKey, szLinkFileValue);
		RegCloseKey(hLinkFileKey);
	}
	btn->szLinkFile[0] = TEXT('\0');
	ZeroMemory(btn->hIcons, sizeof(btn->hIcons));
	btn->lastWriteTime.dwHighDateTime = btn->lastWriteTime.dwLowDateTime = 0;
	btn->creationTime.dwHighDateTime = btn->creationTime.dwLowDateTime = 0;
	return;
#ifdef UNICODE
# undef wLinkFilename
#endif
}

void
UpdateDockButtons(void)
{
	TCHAR findPath[MAX_PATH+1];
	HANDLE hFind;
	WIN32_FIND_DATA findData;
	UINT openPos = 0, i;

	/* Clear the exist flag on every button */
	for(i = 1; i < DOCKBUTTON_MAX; ++i)
		g_dockButtons[i].state.exists = FALSE;

	/* Look at all the files in the directory */
	_sntprintf(findPath, MAX_PATH, TEXT("%s\\*"), g_dockAppDataDir);
	hFind = FindFirstFile(findPath, &findData);
	if(hFind == INVALID_HANDLE_VALUE) return;
	do
	{
		/* Ignore . and .. entries */
		if(_tcscmp(findData.cFileName, TEXT(".")) == 0 || _tcscmp(findData.cFileName, TEXT("..")) == 0)
			continue;
		/* See if this file is in a button already */
		for(i = 1; i < DOCKBUTTON_MAX; ++i)
		{
			/* Check if this button slot is open */
			if(!openPos && DOCKBUTTON_EMPTY(g_dockButtons[i]))
			{
				openPos = i;
				continue;
			}
			/* See if this is the same file as the one in this button */
			if((findData.ftCreationTime.dwHighDateTime != g_dockButtons[i].creationTime.dwHighDateTime
				|| findData.ftCreationTime.dwLowDateTime != g_dockButtons[i].creationTime.dwLowDateTime)
				&& _tcsicmp(findData.cFileName, g_dockButtons[i].szLinkFile) != 0)
				continue;
			/* If it is, check if it's changed */
			g_dockButtons[i].state.exists = TRUE;
			if(_tcsicmp(findData.cFileName, g_dockButtons[i].szLinkFile) != 0)
				_SetDockButtonFile(&g_dockButtons[i], findData.cFileName, FALSE);
			if(findData.ftLastWriteTime.dwLowDateTime != g_dockButtons[i].lastWriteTime.dwLowDateTime
				|| findData.ftLastWriteTime.dwHighDateTime != g_dockButtons[i].lastWriteTime.dwHighDateTime)
				GetDockButtonLinkFile(&g_dockButtons[i]);
			goto next_file;
		}
		/* This file hasn't been assigned a button yet. If there's an empty slot, take it */
		if(openPos)
		{
			_SetDockButtonFile(&g_dockButtons[openPos], findData.cFileName, TRUE);
			g_dockButtons[openPos].state.exists = TRUE;
			openPos = 0;
		}
next_file:
		(void)0;
	} while(FindNextFile(hFind, &findData));
	FindClose(hFind);

	/* Clear out any buttons whose file no longer exists */
	for(i = 1; i < DOCKBUTTON_MAX; ++i)
		if(!DOCKBUTTON_EMPTY(g_dockButtons[i]) && !g_dockButtons[i].state.exists)
			_ClearDockButtonFile(&g_dockButtons[i]);
}

/*** COM IDropTarget implementation ***/

static HRESULT CALLBACK
DockButton_QueryInterface(IDropTarget *idtThis, REFIID riid, OUT void **ppvObject)
{
	DockButton *This = (DockButton*)idtThis;

	if(IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget))
		*ppvObject = idtThis;
	else
	{
		*ppvObject = NULL;
		return E_NOINTERFACE;
	}

	IUnknown_AddRef(idtThis);
	return NOERROR;
}

static ULONG CALLBACK
DockButton_AddRef(IDropTarget *idtThis)
{
	/* We manage the lifetimes of the dock buttons ourselves */
	return 1;
}

static ULONG CALLBACK
DockButton_Release(IDropTarget *idtThis)
{
	return 1;
}

static HRESULT CALLBACK
DockButton_DragEnter(IDropTarget *idtThis, IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, IN OUT DWORD *pdwEffect)
{
	DockButton *This = (DockButton*)idtThis;
	FORMATETC formatEtc;
	STGMEDIUM stgMedium;
	CIDA *pItemIDList;
	LPCITEMIDLIST pidlParent, pidlDrop;
	LPITEMIDLIST pidlFull;
	SHFILEINFO sfi;

	/* See if we are receiving one shell object */
	formatEtc.cfFormat = g_cfShellIDList;
	formatEtc.ptd      = NULL;
	formatEtc.dwAspect = DVASPECT_CONTENT;
	formatEtc.lindex   = -1;
	formatEtc.tymed    = TYMED_HGLOBAL;
	if(SUCCEEDED(IDataObject_GetData(pDataObj, &formatEtc, &stgMedium)))
	{
		pItemIDList = (CIDA*)GlobalLock(stgMedium.hGlobal);
		if(pItemIDList && pItemIDList->cidl == 1)
		{
			if(*pdwEffect & DROPEFFECT_LINK)
				*pdwEffect = DROPEFFECT_LINK;
			else
				*pdwEffect &= DROPEFFECT_COPY;
			if(*pdwEffect)
			{
				/* Get the file's icon and display it temporarily */
				pidlParent = CIDA_GetPIDLFolder(pItemIDList);
				pidlDrop = CIDA_GetPIDLItem(pItemIDList, 0);
				pidlFull = ILCombine(pidlParent, pidlDrop);
				CopyMemory(This->hIconsBak, This->hIcons, sizeof(This->hIcons));
				SHGetFileInfo((LPCTSTR)pidlFull, 0, &sfi, sizeof(SHFILEINFO), SHGFI_SELECTED | SHGFI_ICON | SHGFI_SMALLICON | SHGFI_PIDL);
				This->hIcons[0] = sfi.hIcon;
				SHGetFileInfo((LPCTSTR)pidlFull, 0, &sfi, sizeof(SHFILEINFO), SHGFI_SELECTED | SHGFI_ICON | SHGFI_LARGEICON | SHGFI_PIDL);
				This->hIcons[1] = sfi.hIcon;
				/* TODO: Get 48x48 icon */
				This->hIcons[2] = sfi.hIcon;
				/* Set the state */
				This->state.hot = TRUE;
				InvalidateRect(This->hWnd, NULL, TRUE);
			}
		}
		else
			*pdwEffect = DROPEFFECT_NONE;

		if(pItemIDList) GlobalUnlock(stgMedium.hGlobal);
	}
	else
		*pdwEffect = DROPEFFECT_NONE;

	This->dwDropEffect = *pdwEffect;

	return NOERROR;
}

static HRESULT CALLBACK
DockButton_DragOver(IDropTarget *idtThis, DWORD grfKeyState, POINTL pt, IN OUT DWORD *pdwEffect)
{
	DockButton *This = (DockButton*)idtThis;
	*pdwEffect = This->dwDropEffect;
	return NOERROR;
}

static HRESULT CALLBACK
DockButton_DragLeave(IDropTarget *idtThis)
{
	DockButton *This = (DockButton*)idtThis;
	UINT i;
	/* Restore the icon */
	for(i = 0; i < DOCK_ICONS; ++i)
		if(This->hIcons[i])
			DestroyIcon(This->hIcons[i]);
	CopyMemory(This->hIcons, This->hIconsBak, sizeof(This->hIcons));
	/* Restore the state */
	This->state.hot = FALSE;
	InvalidateRect(This->hWnd, NULL, TRUE);
	return NOERROR;
}

static HRESULT CALLBACK
DockButton_Drop(IDropTarget *idtThis, IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, IN OUT DWORD *pdwEffect)
{
	DockButton *This = (DockButton*)idtThis;
	FORMATETC formatEtc;
	STGMEDIUM stgMedium;
	CIDA *pItemIDList;
	LPCITEMIDLIST pidlParent, pidlDrop;
	LPITEMIDLIST pidlFull;
	UINT i;

	/* Restore the icon */
	for(i = 0; i < DOCK_ICONS; ++i)
		if(This->hIcons[i])
			DestroyIcon(This->hIcons[i]);
	CopyMemory(This->hIcons, This->hIconsBak, sizeof(This->hIcons));

	/* See if we are receiving one shell object */
	formatEtc.cfFormat = g_cfShellIDList;
	formatEtc.ptd      = NULL;
	formatEtc.dwAspect = DVASPECT_CONTENT;
	formatEtc.lindex   = -1;
	formatEtc.tymed    = TYMED_HGLOBAL;
	if(SUCCEEDED(IDataObject_GetData(pDataObj, &formatEtc, &stgMedium)))
	{
		pItemIDList = (CIDA*)GlobalLock(stgMedium.hGlobal);
		if(pItemIDList && pItemIDList->cidl == 1)
		{
			pidlParent = CIDA_GetPIDLFolder(pItemIDList);
			pidlDrop = CIDA_GetPIDLItem(pItemIDList, 0);
			pidlFull = ILCombine(pidlParent, pidlDrop);
			SetDockButtonObject(This, pidlFull);
		}
	}
	This->state.hot = FALSE;
	InvalidateRect(This->hWnd, NULL, TRUE);
	return NOERROR;
}

IDropTargetVtbl g_dockButtonDropTargetImpl = {
	DockButton_QueryInterface,
	DockButton_AddRef,
	DockButton_Release,
	DockButton_DragEnter,
	DockButton_DragOver,
	DockButton_DragLeave,
	DockButton_Drop,
};


