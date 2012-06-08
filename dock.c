#include "stdafx.h"
#include "dock.h"
#include "dockbutton.h"
#include "resource.h"

TCHAR g_dockAppDataDir[MAX_PATH+1];
HINSTANCE g_hInstance = NULL;
UINT g_dockEdge = ABE_RIGHT;
UINT g_dockButtonSize = 48;
HWND g_dockHWnd = NULL;
HWND g_dockHOptionsDialog = NULL;
HWND g_dockHWndTooltip = NULL;
HICON g_dockIcons[DOCK_ICONS] = { NULL, NULL, NULL };
UINT g_dockWindowHeight = 0;
BOOL g_hasThemes = FALSE;
DWORD g_dockDirMonThreadID = 0;
HANDLE g_dockDirMonThreadHandle = NULL;
HANDLE g_dockDirMonQuitEvent = NULL;
struct _DockConfirmations g_dockConfirmations = { TRUE, TRUE, TRUE };
UINT g_cfShellIDList = 0;
BOOL g_dockReady = FALSE;

HTHEME	(WINAPI *pOpenThemeData)(HWND hWnd, LPCWSTR lpszClassList) = NULL;
HRESULT	(WINAPI *pCloseThemeData)(HTHEME hTheme) = NULL;
HRESULT	(WINAPI *pDrawThemeBackground)(HTHEME hTheme, HDC hDc, int iPartId, int iStateId, LPCRECT lpRect, LPCRECT lpClipRect) = NULL;

BOOL
InitDock(void)
{
	if(OleInitialize(NULL) != NOERROR)
		return FALSE;
	if(!(g_cfShellIDList = RegisterClipboardFormat(CFSTR_SHELLIDLIST)))
		return FALSE;

	g_dockAppDataDir[MAX_PATH] = TEXT('\0');
	/* Get our app data dir and change to it */
	if(!SHGetSpecialFolderPath(NULL, g_dockAppDataDir, CSIDL_APPDATA, TRUE))
		return FALSE;
	_tcsncat(g_dockAppDataDir, TEXT("\\WinDock"), MAX_PATH);
	CreateDirectory(g_dockAppDataDir, NULL);
	if(!SetCurrentDirectory(g_dockAppDataDir))
		return FALSE;

	LoadDockSettings();

	/* Load the dock icon */
	LoadIconSizes(g_hInstance, MAKEINTRESOURCE(IDI_WINDOCK), g_dockIcons);

	/* Create the dock window */
	g_dockHWnd = CreateDockWindow();
	if(!g_dockHWnd) return FALSE;

	/* Create the tooltip control */
	g_dockHWndTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g_dockHWnd, NULL, g_hInstance, NULL);
	if(g_dockHWndTooltip)
	{
		SetWindowPos(g_dockHWndTooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}

	/* Load the theme functions */
	g_hasThemes = InitThemes();

	/* Fill the dock with dock buttons */
	if(!CreateDockButtons(g_dockHWnd))
	{
		DestroyWindow(g_dockHWnd);
		return FALSE;
	}

	/* Register it as an appbar */
	if(!RegisterDockAppbar(g_dockHWnd))
	{
		DestroyWindow(g_dockHWnd);
		return FALSE;
	}

	ShowWindow(g_dockHWnd, SW_SHOW);

	/* Start the directory monitor */
	InitDirectoryMonitor();

	g_dockReady = TRUE;
	return TRUE;
}

void
DestroyDock(void)
{
	DestroyDirectoryMonitor();
	DestroyWindow(g_dockHWnd);
	DestroyThemes();
	OleUninitialize();
}

/* Window procedure for the dock */
LRESULT CALLBACK
DockWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
	case WM_APPBAR_CALLBACK:
		DockAppbarCallback(hWnd, (UINT)wParam, lParam);
		return 0;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hWnd, &ps);
			HPEN shadow = CreatePen(PS_SOLID, 0, GetSysColor(COLOR_BTNSHADOW));
			SelectObject(dc, shadow);
			switch(g_dockEdge)
			{
			case ABE_LEFT:
				MoveToEx(dc, g_dockWindowWidth-1, 0, NULL);
				LineTo(dc, g_dockWindowWidth-1, g_dockWindowHeight);
				break;

			case ABE_RIGHT:
				MoveToEx(dc, 0, 0, NULL);
				LineTo(dc, 0, g_dockWindowHeight);
				break;
			}
			MoveToEx(dc, 0, g_dockButtonSize, NULL);
			LineTo(dc, g_dockWindowWidth-1, g_dockButtonSize);
			DeleteObject(shadow);
			EndPaint(hWnd, &ps);
		}
		return 0;

	case WM_CLOSE:
		DestroyDock();
		return 0;

	case WM_DESTROY:
		UnregisterDockAppbar(g_dockHWnd);
		PostQuitMessage(0);
		return 0;

	case WM_CONTEXTMENU:
		{
			/* Pop up the context menu */
			HMENU contexts = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDR_CONTEXTS)),
				  hMenu = GetSubMenu(contexts, SUBMENU_CONTEXTS_HEADERBUTTON);
			MENUITEMINFO mii;
			int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
			
			if(x == -1 || y == -1)
			{
				/* Pop the menu up in the corner if opened by the keyboard */
				RECT wndRect;
				GetWindowRect(hWnd, &wndRect);
				x = (g_dockEdge == ABE_LEFT)? wndRect.left : wndRect.right;
				y = wndRect.top;
			}
			/* Set Options as the default item */
			mii.cbSize = sizeof(MENUITEMINFO);
			mii.fMask  = MIIM_STATE;
			mii.fState = MFS_ENABLED | MFS_DEFAULT;
			SetMenuItemInfo(hMenu, ID_HEADERBUTTON_OPTIONS, FALSE, &mii);

			TrackPopupMenu(
				hMenu,
				((g_dockEdge == ABE_LEFT)? TPM_LEFTALIGN : TPM_RIGHTALIGN) | TPM_RIGHTBUTTON,
				x, y, 0, hWnd, NULL
			);
		}
		return 0;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case ID_HEADERBUTTON_OPTIONS:
			OpenOptionsDialog(hWnd);
			break;

		case ID_HEADERBUTTON_EXIT:
			if(!g_dockConfirmations.confirmExit || MessageBox(hWnd, TEXT("Are you sure you want to exit WinDock?"), TEXT("Exit WinDock"), MB_YESNO | MB_ICONQUESTION) == IDYES)
                DestroyDock();
			break;
		}
		return 0;

	case WM_DIRECTORY_CHANGE:
		UpdateDockButtons();
		return 0;

	default:
		return DefWindowProc(hWnd, Msg, wParam, lParam);
	}
}

static void
_MoveShowButtonSize(HWND hWnd, LPCRECT baseRect, UINT size)
{
	UINT iconSize;
	UINT shrinkage;

	/* Change the icon to the proper size */
	if(size < 40)
		iconSize = 16;
	else if(size < 56)
		iconSize = 32;
	else
		iconSize = 48;
	SendMessage(hWnd, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)g_dockIcons[DOCK_ICON_SIZE(iconSize)]);
	/* Shrink the new rect as necessary */
	shrinkage = (64 - size)/2;
	MoveWindow(hWnd, baseRect->left + shrinkage, baseRect->top + shrinkage, size, size, TRUE);
}

/* Dialog procedure for the dock options dialog */
INT_PTR CALLBACK
OptionsDialogProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	static UINT edge, buttonSize;
	static HWND dockImage, dockButtonSizer, dockShowButtonSize;
	static RECT dockSBSBase; /* Starting position for the IDC_SHOWBUTTONSIZE control */
	static HBITMAP leftImage, rightImage;
	static struct _DockConfirmations confirm;

	switch(Msg)
	{
	case WM_INITDIALOG:
		edge = g_dockEdge;
		buttonSize = g_dockButtonSize;
		confirm = g_dockConfirmations;

		/* Check the radio button with the current setting */
		CheckRadioButton(
			hDlg, IDC_DOCKLEFT, IDC_DOCKRIGHT,
			(edge == ABE_LEFT)? IDC_DOCKLEFT : IDC_DOCKRIGHT
		);
		/* Set the bitmap to match */
		dockImage = GetDlgItem(hDlg, IDC_DOCKIMAGE);
		leftImage  = LoadBitmap(g_hInstance, MAKEINTRESOURCE(IDB_DOCKLEFT));
		rightImage = LoadBitmap(g_hInstance, MAKEINTRESOURCE(IDB_DOCKRIGHT));
		SendMessage(dockImage, STM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)((edge == ABE_LEFT)? leftImage : rightImage));

		/* Check the confirm checkboxes */
		CheckDlgButton(hDlg, IDC_CONFIRM_DELETE,  confirm.confirmDelete?  BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CONFIRM_REPLACE, confirm.confirmReplace? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_CONFIRM_EXIT,    confirm.confirmExit?    BST_CHECKED : BST_UNCHECKED);
		
		/* Prepare the size controls */
		dockButtonSizer = GetDlgItem(hDlg, IDC_BUTTONSIZE);
		dockShowButtonSize = GetDlgItem(hDlg, IDC_SHOWBUTTONSIZE);
		GetWindowRect(dockShowButtonSize, &dockSBSBase);
		ScreenToClient(hDlg, (LPPOINT)&dockSBSBase);
		_MoveShowButtonSize(dockShowButtonSize, &dockSBSBase, g_dockButtonSize);
		SendMessage(dockButtonSizer, TBM_SETRANGE,	(WPARAM)FALSE, (LPARAM)MAKELONG(12,32));
		SendMessage(dockButtonSizer, TBM_SETPOS,	(WPARAM)TRUE,  (LPARAM)buttonSize/2);
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
		case IDC_APPLY:
			SetDockEdge(edge);
			SetDockButtonSize(buttonSize);
			SetDockConfirmations(confirm);
			if(LOWORD(wParam) == IDOK)
				DestroyWindow(hDlg);
			break;

		case IDCANCEL:
			DestroyWindow(hDlg);
			break;

		case IDC_DOCKLEFT:
			edge = ABE_LEFT;
			SendMessage(dockImage, STM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)leftImage);
			break;

		case IDC_DOCKRIGHT:
			edge = ABE_RIGHT;
			SendMessage(dockImage, STM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)rightImage);
			break;

		case IDC_CONFIRM_DELETE:
			confirm.confirmDelete = !confirm.confirmDelete;
			break;

		case IDC_CONFIRM_REPLACE:
			confirm.confirmReplace = !confirm.confirmReplace;
			break;

		case IDC_CONFIRM_EXIT:
			confirm.confirmExit = !confirm.confirmExit;
			break;
		}
		break;

	case WM_HSCROLL:
		{
			UINT position = (UINT)SendMessage(dockButtonSizer, TBM_GETPOS, 0, 0);
			buttonSize = position*2;
			_MoveShowButtonSize(dockShowButtonSize, &dockSBSBase, buttonSize);
		}
		break;

	case WM_DESTROY:
		DeleteObject(leftImage);
		DeleteObject(rightImage);
		g_dockHOptionsDialog = NULL;
		return TRUE;

	default:
		return FALSE;
	}
	return TRUE;
}

/* Appbar callback for the dock */
void
DockAppbarCallback(HWND hWnd, UINT Msg, LPARAM lParam)
{
	static HWND hWndPrev = NULL;
    APPBARDATA abd;
	
	abd.cbSize	= sizeof(abd);
	abd.hWnd	= hWnd;

	switch(Msg)
	{
	case ABN_STATECHANGE:
		SetDockAppbarZOrder(hWnd);
		break;

	case ABN_FULLSCREENAPP:
		if((BOOL)lParam)
		{
			/* A fullscreen app is starting. Go to the back */
			hWndPrev = GetWindow(hWnd, GW_HWNDPREV);
			SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		else
		{
			/* The last fullscreen app quit. Return to our former position */
			if(hWndPrev)
				SetWindowPos(hWnd, hWndPrev, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		break;

	case ABN_POSCHANGED:
		/* The taskbar or another appbar moved */
		SetDockAppbarPosition(hWnd);
		break;

	case ABN_WINDOWARRANGE:
		/* Keep ourselves out of the window arrange operation by hiding when it starts
		 * and coming back when it ends */
		ShowWindow(hWnd, lParam? SW_HIDE : SW_SHOW);
		break;
	}
}

void
OpenOptionsDialog(HWND hWndParent)
{
	if(!g_dockHOptionsDialog)
	{
		g_dockHOptionsDialog = CreateDialog(g_hInstance, MAKEINTRESOURCE(IDD_DOCKOPTIONS), hWndParent, OptionsDialogProc);
		ShowWindow(g_dockHOptionsDialog, SW_SHOW);
	}
	else
		SetActiveWindow(g_dockHOptionsDialog);
}


BOOL
InitThemes(void)
{
/* Stop VC whining about the mismatching function pointer types */
#pragma warning(push)
#pragma warning(disable: 4113 4047)
	HMODULE hUxTheme = LoadLibrary(TEXT("uxtheme.dll"));
	if(!hUxTheme) return FALSE;

	pOpenThemeData		 = GetProcAddress(hUxTheme, "OpenThemeData");
	pCloseThemeData		 = GetProcAddress(hUxTheme, "CloseThemeData");
	pDrawThemeBackground = GetProcAddress(hUxTheme, "DrawThemeBackground");
	return TRUE;
#pragma warning(pop)
}

void
DestroyThemes(void)
{
	/*empty*/
}

BOOL
InitDirectoryMonitor(void)
{
	g_dockDirMonQuitEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_dockDirMonThreadHandle = CreateThread(NULL, 0, DirectoryMonitorProc, NULL, 0, &g_dockDirMonThreadID);

	if(!g_dockDirMonThreadHandle)
		return FALSE;
	return TRUE;
}

void
DestroyDirectoryMonitor(void)
{
	SetEvent(g_dockDirMonQuitEvent);
	JoinThread(g_dockDirMonThreadHandle);
}

DWORD
JoinThread(HANDLE hThread)
{
	DWORD exitCode;
	while(GetExitCodeThread(hThread, &exitCode))
		if(exitCode != STILL_ACTIVE)
			return exitCode;
	return -1;
}

DWORD CALLBACK
DirectoryMonitorProc(LPVOID param)
{
	HANDLE hChangeNotify = FindFirstChangeNotification(g_dockAppDataDir, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
	if(hChangeNotify)
	{
		HANDLE waitObjects[2] = { hChangeNotify, g_dockDirMonQuitEvent };
		for(;;)
		{
			switch(WaitForMultipleObjects(2, waitObjects, FALSE, INFINITE))
			{
			case WAIT_OBJECT_0:
				/* Change Notification */
				PostMessage(g_dockHWnd, WM_DIRECTORY_CHANGE, 0, 0);
				FindNextChangeNotification(hChangeNotify);
				break;
			case WAIT_OBJECT_0 + 1:
				/* Quit */
				FindCloseChangeNotification(hChangeNotify);
				return 0;
			}
		}
	}
	else
		return 1;
}

void
LoadDockSettings(void)
{
	HKEY dockSettings;
	DWORD valueSize, valueType = REG_NONE;
	DWORD edge, buttonSize;
	DWORD confDelete, confReplace, confExit;

	/* Fallback defaults */
	g_dockEdge = ABE_RIGHT;
	g_dockButtonSize = 48;
	g_dockConfirmations.confirmDelete  = FALSE;
	g_dockConfirmations.confirmReplace = FALSE;
	g_dockConfirmations.confirmExit    = FALSE;
    
	if(!SUCCEEDED(RegCreateKeyEx(HKEY_CURRENT_USER, TEXT("Software\\WinDock"), 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &dockSettings, NULL)))
	{
		MessageBox(NULL, TEXT("Unable to read dock settings from registry. You may need to reinstall WinDock."), TEXT("Registry Error"), MB_ICONEXCLAMATION);
        return;
	}
	
	valueSize = sizeof(DWORD);
	if(SUCCEEDED(RegQueryValueEx(dockSettings, TEXT("Edge"), NULL, &valueType, (LPBYTE)&edge, &valueSize))
		&& valueType == REG_DWORD && (edge == ABE_LEFT || edge == ABE_RIGHT))
		g_dockEdge = (UINT)edge;
	else
	{
		edge = (DWORD)g_dockEdge;
		RegSetValueEx(dockSettings, TEXT("Edge"), 0, REG_DWORD, (const LPBYTE)&edge, sizeof(DWORD));
	}
	
	valueSize = sizeof(DWORD);
	if(SUCCEEDED(RegQueryValueEx(dockSettings, TEXT("ButtonSize"), NULL, &valueType, (LPBYTE)&buttonSize, &valueSize))
		&& valueType == REG_DWORD)
		g_dockButtonSize = (UINT)buttonSize;
	else
	{
		buttonSize = (DWORD)g_dockButtonSize;
		RegSetValueEx(dockSettings, TEXT("ButtonSize"), 0, REG_DWORD, (const LPBYTE)&buttonSize, sizeof(DWORD));
	}

	valueSize = sizeof(DWORD);
	if(SUCCEEDED(RegQueryValueEx(dockSettings, TEXT("ConfirmDelete"), NULL, &valueType, (LPBYTE)&confDelete, &valueSize))
		&& valueType == REG_DWORD)
		g_dockConfirmations.confirmDelete = (confDelete? TRUE : FALSE);
	else
	{
		confDelete = g_dockConfirmations.confirmDelete;
		RegSetValueEx(dockSettings, TEXT("ConfirmDelete"), 0, REG_DWORD, (const LPBYTE)&confDelete, sizeof(DWORD));
	}

	valueSize = sizeof(DWORD);
	if(SUCCEEDED(RegQueryValueEx(dockSettings, TEXT("ConfirmReplace"), NULL, &valueType, (LPBYTE)&confReplace, &valueSize))
		&& valueType == REG_DWORD)
		g_dockConfirmations.confirmReplace = (confReplace? TRUE : FALSE);
	else
	{
		confReplace = g_dockConfirmations.confirmReplace;
		RegSetValueEx(dockSettings, TEXT("ConfirmReplace"), 0, REG_DWORD, (const LPBYTE)&confReplace, sizeof(DWORD));
	}

	valueSize = sizeof(DWORD);
	if(SUCCEEDED(RegQueryValueEx(dockSettings, TEXT("ConfirmExit"), NULL, &valueType, (LPBYTE)&confExit, &valueSize))
		&& valueType == REG_DWORD)
		g_dockConfirmations.confirmExit = (confExit? TRUE : FALSE);
	else
	{
		confExit = g_dockConfirmations.confirmExit;
		RegSetValueEx(dockSettings, TEXT("ConfirmExit"), 0, REG_DWORD, (const LPBYTE)&confExit, sizeof(DWORD));
	}

	RegCloseKey(dockSettings);
}

HWND
CreateDockWindow(void)
{
	ATOM wndClass = RegisterDockClass();

	if(wndClass)
		return CreateWindowEx(WS_EX_TOOLWINDOW, (LPCTSTR)wndClass, TEXT("WinDock"), WS_POPUP, 0, 0, g_dockButtonSize, g_dockButtonSize, NULL, NULL, g_hInstance, NULL);
	else
		return NULL;
}

/* Register the dock class */
ATOM
RegisterDockClass(void)
{
	WNDCLASSEX wc;

	wc.cbSize		 = sizeof(WNDCLASSEX);
	wc.style		 = 0;
	wc.lpfnWndProc	 = DockWindowProc;
	wc.cbClsExtra	 = 0;
	wc.cbWndExtra	 = 0;
	wc.hInstance	 = g_hInstance;
	wc.hIcon		 = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_WINDOCK));
	wc.hCursor		 = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
	wc.lpszMenuName	 = NULL;
	wc.lpszClassName = TEXT("WinDock");
	wc.hIconSm		 = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_WINDOCK));

	return RegisterClassEx(&wc);
}

/* Register the dock as an appbar */
BOOL
RegisterDockAppbar(HWND hWnd)
{
	APPBARDATA abd;

	abd.cbSize				= sizeof(APPBARDATA);
	abd.hWnd				= hWnd;
	abd.uCallbackMessage	= WM_APPBAR_CALLBACK;

	if(!SHAppBarMessage(ABM_NEW, &abd))
		return FALSE;

	SetDockAppbarPosition(hWnd);
	return TRUE;
}

/* Unregister the dock window as an appbar */
void
UnregisterDockAppbar(HWND hWnd)
{
	APPBARDATA abd;

	abd.cbSize	= sizeof(APPBARDATA);
	abd.hWnd	= hWnd;
	SHAppBarMessage(ABM_REMOVE, &abd);
}

/* Get a recommended position from the shell, and set it */
void
SetDockAppbarPosition(HWND hWnd)
{
	APPBARDATA abd;

	abd.cbSize	= sizeof(APPBARDATA);
	abd.hWnd	= hWnd;
	abd.uEdge	= g_dockEdge;
	abd.rc.top	  = 0;
	abd.rc.bottom = GetSystemMetrics(SM_CYSCREEN);
	switch(g_dockEdge)
	{
	case ABE_LEFT:
		abd.rc.left  = 0;
		abd.rc.right = g_dockWindowWidth;
		break;
	case ABE_RIGHT:
		abd.rc.right = GetSystemMetrics(SM_CXSCREEN);
		abd.rc.left  = abd.rc.right - g_dockWindowWidth;
		break;
	}
	SHAppBarMessage(ABM_QUERYPOS, &abd);
	/* Adjust the width of the appbar */
	switch(abd.uEdge)
	{
	case ABE_LEFT:
		abd.rc.right = abd.rc.left + g_dockWindowWidth;
		break;
	case ABE_RIGHT:
		abd.rc.left = abd.rc.right - g_dockWindowWidth;
		break;
	}
	SHAppBarMessage(ABM_SETPOS, &abd);
	/* Move the window to conform */
	MoveWindow(hWnd, abd.rc.left, abd.rc.top, abd.rc.right - abd.rc.left, abd.rc.bottom - abd.rc.top, TRUE);
	/* Add or remove dock buttons to fit the new dock */
	g_dockWindowHeight = abd.rc.bottom - abd.rc.top;
	ResizeDockButtons();
	ShowDockButtons(g_dockWindowHeight / g_dockButtonSize);
	/* Redraw the entire window */
	InvalidateRect(hWnd, NULL, TRUE);
}

void
SetDockAppbarZOrder(HWND hWnd)
{
	APPBARDATA abd;
	UINT_PTR uState;

	/* Set our always on top state to match the taskbar */
	uState = SHAppBarMessage(ABM_GETSTATE, &abd);
	SetWindowPos(hWnd, (uState & ABS_ALWAYSONTOP)? HWND_TOPMOST : HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void
SetDockEdge(UINT uEdge)
{
	HKEY dockSettings;
	DWORD dwEdge;

	if(uEdge != ABE_LEFT && uEdge != ABE_RIGHT) return;
	if(uEdge == g_dockEdge) return;
	g_dockEdge = uEdge;
	SetDockAppbarPosition(g_dockHWnd);

	if(RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\WinDock"), 0, KEY_WRITE, &dockSettings) == ERROR_SUCCESS)
	{
		dwEdge = (DWORD)uEdge;
		RegSetValueEx(dockSettings, TEXT("Edge"), 0, REG_DWORD, (const LPBYTE)&dwEdge, sizeof(DWORD));
		RegCloseKey(dockSettings);
	}
}

void
SetDockButtonSize(UINT uSize)
{
	HKEY dockSettings;
	DWORD dwButtonSize;

	if(uSize == g_dockButtonSize) return;
	g_dockButtonSize = uSize;
	SetDockAppbarPosition(g_dockHWnd);

	if(RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\WinDock"), 0, KEY_WRITE, &dockSettings) == ERROR_SUCCESS)
	{
		dwButtonSize = (DWORD)uSize;
		RegSetValueEx(dockSettings, TEXT("ButtonSize"), 0, REG_DWORD, (const LPBYTE)&dwButtonSize, sizeof(DWORD));
		RegCloseKey(dockSettings);
	}
}

void
SetDockConfirmations(struct _DockConfirmations confirm)
{
	HKEY dockSettings;

	g_dockConfirmations = confirm;
	if(RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\WinDock"), 0, KEY_WRITE, &dockSettings) == ERROR_SUCCESS)
	{
		DWORD confDelete  = confirm.confirmDelete,
			  confReplace = confirm.confirmReplace,
			  confExit    = confirm.confirmExit;
		RegSetValueEx(dockSettings, TEXT("ConfirmDelete"),  0, REG_DWORD, (const LPBYTE)&confDelete,  sizeof(DWORD));
		RegSetValueEx(dockSettings, TEXT("ConfirmReplace"), 0, REG_DWORD, (const LPBYTE)&confReplace, sizeof(DWORD));
		RegSetValueEx(dockSettings, TEXT("ConfirmExit"),    0, REG_DWORD, (const LPBYTE)&confExit,    sizeof(DWORD));
		RegCloseKey(dockSettings);
	}
}

void
LoadIconSizes(HINSTANCE hInstance, LPCTSTR lpszRsrcName, OUT HICON *hIcons)
{
	HRSRC hRsrc;
	HGLOBAL hRsrcMem;
	LPBYTE lpRsrcMem;
	int nID[DOCK_ICONS], i;

	/* Load the group icon resource */
	hRsrc = FindResource(hInstance, lpszRsrcName, MAKEINTRESOURCE(RT_GROUP_ICON));
	if(!hRsrc) return;
	hRsrcMem = LoadResource(hInstance, hRsrc);
	if(!hRsrcMem) return;
	lpRsrcMem = LockResource(hRsrcMem);
	if(!lpRsrcMem) return;

	/* Get ids for the 16x16, 32x32 and 48x48 sizes */
	nID[0] = LookupIconIdFromDirectoryEx(lpRsrcMem, TRUE, 16, 16, LR_DEFAULTCOLOR);
	nID[1] = LookupIconIdFromDirectoryEx(lpRsrcMem, TRUE, 32, 32, LR_DEFAULTCOLOR);
	nID[2] = LookupIconIdFromDirectoryEx(lpRsrcMem, TRUE, 48, 48, LR_DEFAULTCOLOR);

	/* Now load each icon in turn */
	for(i = 0; i < DOCK_ICONS; ++i)
	{
		if(!nID[i]) continue;
		hRsrc = FindResource(hInstance, MAKEINTRESOURCE(nID[i]), MAKEINTRESOURCE(RT_ICON));
		if(!hRsrc) continue;
		hRsrcMem = LoadResource(hInstance, hRsrc);
		if(!hRsrcMem) continue;
		lpRsrcMem = LockResource(hRsrcMem);
		if(!lpRsrcMem) continue;
		
		hIcons[i] = CreateIconFromResourceEx(lpRsrcMem, SizeofResource(hInstance, hRsrc), TRUE, 0x00030000, DOCK_SIZE_ICON(i), DOCK_SIZE_ICON(i), LR_DEFAULTCOLOR);
	}
}

LPTSTR
BaseName(LPTSTR path)
{
	return _tcsrchr(path, TEXT('\\')) + 1;
}