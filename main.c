#include "stdafx.h"
#include "dock.h"

int APIENTRY
_tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	MSG msg;
	HANDLE hMutex;

	/* Check if WinDock is already running */
	hMutex = CreateMutex(NULL, TRUE, TEXT("WinDock"));
	if(GetLastError() == ERROR_ALREADY_EXISTS)
		return 1;

	g_hInstance = hInstance;

	InitCommonControls();
	/* Create the dock */
	if(!InitDock())
	{
		MessageBox(NULL, TEXT("WinDock was unable to start. Try closing a few running applications and starting it again."), TEXT("WinDock Error"), MB_ICONSTOP|MB_OK);
		return 1;
	}

	/* Run the message loop */
	while(GetMessage(&msg, NULL, 0, 0))
	{
		if(!(g_dockHOptionsDialog && IsDialogMessage(g_dockHOptionsDialog, &msg)))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int)msg.wParam;
}