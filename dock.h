#pragma once

/* Dock icon macros */
#define DOCK_ICONS 3
#define DOCK_ICON_SIZE(sz)	((sz)/16 - 1)
#define DOCK_SIZE_ICON(i)	(((i)+1)*16)

/* Dock application messages */
#define WM_APPBAR_CALLBACK	(WM_APP+1)
#define WM_DIRECTORY_CHANGE	(WM_APP+2)

/* Instance handle from WinMain */
extern HINSTANCE g_hInstance;

/* Path to our application data folder */
extern TCHAR g_dockAppDataDir[MAX_PATH+1];

/* Dock window handle */
extern HWND g_dockHWnd;
/* Options dialog handle, or NULL if the options dialog isn't currently open */
extern HWND g_dockHOptionsDialog;
/* Tooltip control handle */
extern HWND g_dockHWndTooltip;

/* Dock icon images */
extern HICON g_dockIcons[DOCK_ICONS];

/* Dock configuration */
extern UINT g_dockEdge;
extern UINT g_dockButtonSize;

/* Dock window dimensions */
#define g_dockWindowWidth (g_dockButtonSize+1)
extern UINT g_dockWindowHeight;

/* True if themes are available */
extern BOOL g_hasThemes;

/* Thread ID and handle for the directory monitor */
extern DWORD g_dockDirMonThreadID;
extern HANDLE g_dockDirMonThreadHandle;
/* Quit event handle for the directory monitor */
extern HANDLE g_dockDirMonQuitEvent;

/* FALSE until the dock window has been displayed */
extern BOOL g_dockReady;

/* Confirmation dialog flags */
extern struct _DockConfirmations
{
	unsigned confirmDelete	: 1;		/* Confirm deletion of dock buttons */
	unsigned confirmReplace	: 1;		/* Confirm replacement of dock button contents */
	unsigned confirmExit	: 1;		/* Confirm app exit */
} g_dockConfirmations;

/* Shell clipboard formats */
extern UINT g_cfShellIDList;

/* Initialise the dock */
BOOL InitDock(void);

/* Clean up the dock */
void DestroyDock(void);

/* Window callback functions */
LRESULT	CALLBACK DockWindowProc	  (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
INT_PTR	CALLBACK OptionsDialogProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
/* Callback subroutines for the above */
void DockAppbarCallback(HWND hWnd, UINT Msg, LPARAM lParam);

/* Open the Options dialog */
void OpenOptionsDialog(HWND hWndParent);

/* Load uxtheme.dll and set up pointers to the theme functions, if available */
BOOL InitThemes(void);

/* Release uxtheme.dll */
void DestroyThemes(void);

/* Start the directory monitor */
BOOL InitDirectoryMonitor(void);
/* Stop the directory monitor */
void DestroyDirectoryMonitor(void);

/* Wait for a thread to end and return its error code */
DWORD JoinThread(HANDLE hThread);

/* Thread function that runs the directory monitor */
DWORD CALLBACK DirectoryMonitorProc(LPVOID param);

/* Pointers to theme functions */
extern HTHEME	(WINAPI *pOpenThemeData)(HWND hWnd, LPCWSTR lpszClassList);
extern HRESULT	(WINAPI *pCloseThemeData)(HTHEME hTheme);
extern HRESULT	(WINAPI *pDrawThemeBackground)(HTHEME hTheme, HDC hDc, int iPartId, int iStateId, LPCRECT lpRect, LPCRECT lpClipRect);

/* Load dock settings from the registry */
void LoadDockSettings(void);

/* Window creation functions */
HWND CreateDockWindow(void);
ATOM RegisterDockClass(void);

/* Appbar management functions */
BOOL RegisterDockAppbar(HWND hWnd);
void UnregisterDockAppbar(HWND hWnd);
void SetDockAppbarPosition(HWND hWnd);
void SetDockAppbarZOrder(HWND hWnd);

/* Dock settings */
void SetDockEdge(UINT uEdge);
void SetDockButtonSize(UINT uSize);
void SetDockConfirmations(struct _DockConfirmations confirm);

/* Load the 16x16, 32x32 and 48x48 icons from an icon resource */
void LoadIconSizes(HINSTANCE hInstance, LPCTSTR lpszRsrcName, OUT HICON *hIcons);

/* Find the basename in a file path */
LPTSTR BaseName(LPTSTR path);