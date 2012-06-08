#pragma once
#include "dock.h"

/* Maximum number of dock buttons */
#define DOCKBUTTON_MAX 50

/* Dock button data structure */
typedef struct _DockButton
{
	IDropTarget iDropTarget;		/* IDropTarget interface for drag and drop ops */
	HWND hWnd;						/* Control window handle */
	HTHEME hTheme;					/* Theme data handle for the button */
	UINT uPosition;					/* Button's position within the dock */
	struct _DockButtonState
	{
		unsigned hot		: 1;	/* The mouse is over the button */
		unsigned pressed	: 1;	/* The button has been pressed */
		unsigned exists		: 1;	/* Flagged by the directory monitor if a file exists */
	} state;
	HICON hIcons[DOCK_ICONS];		/* Handle for the icon sizes */
	HICON hIconsBak[DOCK_ICONS];	/* Stores the real icons while a ghost icon is displayed during D&D */
	TCHAR szLinkFile[MAX_PATH+1];	/* The link file this button opens */
	FILETIME lastWriteTime;			/* Last write time for the link file */
	FILETIME creationTime;			/* Creation time for the link file */
	DWORD dwDropEffect;				/* Current drop effect */
} DockButton;

#define DOCKBUTTON_EMPTY(btn) ((btn).szLinkFile[0] == TEXT('\0'))

/* The current set of dock buttons */
extern DockButton g_dockButtons[DOCKBUTTON_MAX];
/* The count of currently active dock buttons */
extern UINT g_dockButtonsActive;
/* Registered window class for dock buttons */
extern ATOM g_dockButtonClass;
/* Button being dragged to */
extern UINT g_dockButtonDrag;

/* Implementation of IDropTarget's methods for dock buttons */
extern IDropTargetVtbl g_dockButtonDropTargetImpl;

/* Register the dock button class */
ATOM RegisterDockButtonClass(void);
/* Create a new dock button */
BOOL CreateDockButton(HWND hWndParent, UINT uPosition, OUT DockButton *newBtn);
/* Destroy a dock button */
void DestroyDockButton(DockButton *btn);

/* Create all the dock buttons */
BOOL CreateDockButtons(HWND hWndParent);
/* Destroy all the dock buttons */
void DestroyDockButtons(void);

/* Window procedure for dock buttons */
LRESULT CALLBACK DockButtonWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

/* Reposition a dock button after the dock has changed edge or size */
void ResizeDockButton(const DockButton *btn);
/* Reposition all dock buttons */
void ResizeDockButtons(void);
/* Change the number of active dock buttons */
void ShowDockButtons(UINT count);

/* Clear a dock button */
void ClearDockButton(DockButton *btn);
/* Open a dialog to choose a docked file */
void SelectDockButtonFile(DockButton *btn);
/* Launch a dock button */
void OpenDockButton(const DockButton *btn, LPCTSTR verb);
/* Set the linked file for a dock button */
BOOL SetDockButtonFile(DockButton *btn, LPCTSTR lpszFilename);
/* Set the linked shell object for a dock button */
BOOL SetDockButtonObject(DockButton *btn, LPCITEMIDLIST lpidlObject);

/* Opens the link file n.lnk, and sets the dock button's icon to match */
void GetDockButtonLinkFile(DockButton *btn);

/* Create a shell link to a file. */
BOOL CreateShellLinkToFile(LPCTSTR lpszLinkFilename, LPCTSTR lpszTargetFilename);
/* Create a shell link to a namespace object. */
BOOL CreateShellLinkToObject(LPCTSTR lpszLinkFilename, LPCITEMIDLIST lpidlTargetObject);

/* Update the dock buttons after a change in the directory */
void UpdateDockButtons(void);