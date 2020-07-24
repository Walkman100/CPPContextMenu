// https://devblogs.microsoft.com/oldnewthing/20040928-00/?p=37723
#define STRICT
#include <windows.h>
#include <windowsx.h>
#include <ole2.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>

HINSTANCE g_hinst;                          /* This application's HINSTANCE */
HWND g_hwndChild;                           /* Optional child window */

HRESULT GetUIObjectOfFile(HWND hwnd, LPCWSTR pszPath, REFIID riid, void **ppv)
{
    *ppv = NULL;
    HRESULT hr;
    LPITEMIDLIST pidl;
    SFGAOF sfgao;
    if (SUCCEEDED(hr = SHParseDisplayName(pszPath, NULL, &pidl, 0, &sfgao)))
    {
        IShellFolder *psf;
        LPCITEMIDLIST pidlChild;
        if (SUCCEEDED(hr = SHBindToParent(pidl, IID_IShellFolder, (void**)&psf, &pidlChild)))
        {
            hr = psf->GetUIObjectOf(hwnd, 1, &pidlChild, riid, NULL, ppv);
            psf->Release();
        }
        CoTaskMemFree(pidl);
    }
    return hr;
}

#define SCRATCH_QCM_FIRST 1
#define SCRATCH_QCM_LAST  0x7FFF

IContextMenu *g_pcm;
IContextMenu2 *g_pcm2;
IContextMenu3 *g_pcm3;

void OnContextMenu(HWND hwnd, HWND hwndContext, int xPos, int yPos)
{
    POINT pt = { xPos, yPos };
    if (pt.x == -1 && pt.y == -1)
    {
        pt.x = pt.y = 0;
        ClientToScreen(hwnd, &pt);
    }

    IContextMenu *pcm;
    if (SUCCEEDED(GetUIObjectOfFile(hwnd, L"C:\\pagefile.sys", IID_IContextMenu, (void**)&pcm)))
    {
        HMENU hmenu = CreatePopupMenu();
        if (hmenu)
        {
            if (SUCCEEDED(pcm->QueryContextMenu(hmenu, 0, SCRATCH_QCM_FIRST, SCRATCH_QCM_LAST, CMF_NORMAL)))
            {
                pcm->QueryInterface(IID_IContextMenu2, (void**)&g_pcm2);
                pcm->QueryInterface(IID_IContextMenu3, (void**)&g_pcm3);

                g_pcm = pcm;
                int iCmd = TrackPopupMenuEx(hmenu, TPM_RETURNCMD, pt.x, pt.y, hwnd, NULL);
                g_pcm = NULL;

                if (g_pcm2)
                {
                    g_pcm2->Release();
                    g_pcm2 = NULL;
                }
                if (g_pcm3)
                {
                    g_pcm3->Release();
                    g_pcm3 = NULL;
                }

                if (iCmd > 0) {
                    CMINVOKECOMMANDINFOEX info = { 0 };
                    info.cbSize = sizeof(info);
                    info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                    if (GetKeyState(VK_CONTROL) < 0)
                    {
                        info.fMask |= CMIC_MASK_CONTROL_DOWN;
                    }
                    if (GetKeyState(VK_SHIFT) < 0)
                    {
                        info.fMask |= CMIC_MASK_SHIFT_DOWN;
                    }
                    info.hwnd = hwnd;
                    info.lpVerb  = MAKEINTRESOURCEA(iCmd - SCRATCH_QCM_FIRST);
                    info.lpVerbW = MAKEINTRESOURCEW(iCmd - SCRATCH_QCM_FIRST);
                    info.nShow = SW_SHOWNORMAL;
                    info.ptInvoke = pt;
                    pcm->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
                }
            }
            DestroyMenu(hmenu);
        }
        pcm->Release();
    }
}

HRESULT IContextMenu_GetCommandString(IContextMenu *pcm, UINT_PTR idCmd, UINT uFlags, UINT *pwReserved, LPWSTR pszName, UINT cchMax)
{
    // Callers are expected to be using Unicode.
    if (!(uFlags & GCS_UNICODE)) return E_INVALIDARG;

    // Some context menu handlers have off-by-one bugs and will
    // overflow the output buffer. Let’s artificially reduce the
    // buffer size so a one-character overflow won’t corrupt memory.
    if (cchMax <= 1) return E_FAIL;
    cchMax--;

    // First try the Unicode message.  Preset the output buffer
    // with a known value because some handlers return S_OK without
    // doing anything.
    pszName[0] = L'\0';

    HRESULT hr = pcm->GetCommandString(idCmd, uFlags, pwReserved, (LPSTR)pszName, cchMax);
    if (SUCCEEDED(hr) && pszName[0] == L'\0')
    {
        // Rats, a buggy IContextMenu handler that returned success
        // even though it failed.
        hr = E_NOTIMPL;
    }

    if (FAILED(hr))
    {
        // try again with ANSI – pad the buffer with one extra character
        // to compensate for context menu handlers that overflow by
        // one character.
        LPSTR pszAnsi = (LPSTR)LocalAlloc(LMEM_FIXED, (cchMax + 1) * sizeof(CHAR));
        if (pszAnsi)
        {
            pszAnsi[0] = '\0';
            hr = pcm->GetCommandString(idCmd, uFlags & ~GCS_UNICODE, pwReserved, pszAnsi, cchMax);
            if (SUCCEEDED(hr) && pszAnsi[0] == '\0')
            {
                // Rats, a buggy IContextMenu handler that returned success
                // even though it failed.
                hr = E_NOTIMPL;
            }
            if (SUCCEEDED(hr))
            {
                if (MultiByteToWideChar(CP_ACP, 0, pszAnsi, -1, pszName, cchMax) == 0)
                {
                    hr = E_FAIL;
                }
            }
            LocalFree(pszAnsi);
        }
        else
        {
            hr = E_OUTOFMEMORY;
        }
    }
    return hr;
}

void OnMenuSelect(HWND hwnd, HMENU hmenu, int item, HMENU hmenuPopup, UINT flags)
{
    if (g_pcm && item >= SCRATCH_QCM_FIRST && item <= SCRATCH_QCM_LAST)
    {
        WCHAR szBuf[MAX_PATH];
        if (FAILED(IContextMenu_GetCommandString(g_pcm, item - SCRATCH_QCM_FIRST, GCS_HELPTEXTW, NULL, szBuf, MAX_PATH)))
        {
            lstrcpynW(szBuf, L"No help available. (Failed to get text)", MAX_PATH);
        }
        SetWindowTextW(hwnd, szBuf);
    }
}

/*
 *  OnSize
 *      If we have an inner child, resize it to fit.
 */
void OnSize(HWND hwnd, UINT state, int cx, int cy)
{
    if (g_hwndChild)
    {
        MoveWindow(g_hwndChild, 0, 0, cx, cy, TRUE);
    }
}

/*
 *  OnCreate
 *      Applications will typically override this and maybe even
 *      create a child window.
 */
BOOL OnCreate(HWND hwnd, LPCREATESTRUCT lpcs)
{
    return TRUE;
}

/*
 *  OnDestroy
 *      Post a quit message because our application is over when the
 *      user closes this window.
 */
void OnDestroy(HWND hwnd)
{
    PostQuitMessage(0);
}

/*
 *  PaintContent
 *      Interesting things will be painted here eventually.
 */
void PaintContent(HWND hwnd, PAINTSTRUCT *pps)
{
}

/*
 *  OnPaint
 *      Paint the content as part of the paint cycle.
 */
void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    PaintContent(hwnd, &ps);
    EndPaint(hwnd, &ps);
}

/*
 *  OnPrintClient
 *      Paint the content as requested by USER.
 */
void OnPrintClient(HWND hwnd, HDC hdc)
{
    PAINTSTRUCT ps;
    ps.hdc = hdc;
    GetClientRect(hwnd, &ps.rcPaint);
    PaintContent(hwnd, &ps);
}

/*
 *  Window procedure
 */
LRESULT CALLBACK WndProc(HWND hwnd, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
    if (uiMsg == WM_MENUSELECT)
    {
        HANDLE_WM_MENUSELECT(hwnd, wParam, lParam, OnMenuSelect);
    }
    if (g_pcm3)
    {
        LRESULT lres;
        if (SUCCEEDED(g_pcm3->HandleMenuMsg2(uiMsg, wParam, lParam, &lres)))
        {
            return lres;
        }
    }
    else if (g_pcm2)
    {
        if (SUCCEEDED(g_pcm2->HandleMenuMsg(uiMsg, wParam, lParam)))
        {
            return 0;
        }
    }

    switch (uiMsg) {
        HANDLE_MSG(hwnd, WM_CREATE, OnCreate);
        HANDLE_MSG(hwnd, WM_SIZE, OnSize);
        HANDLE_MSG(hwnd, WM_DESTROY, OnDestroy);
        HANDLE_MSG(hwnd, WM_PAINT, OnPaint);
        HANDLE_MSG(hwnd, WM_CONTEXTMENU, OnContextMenu);
        case WM_PRINTCLIENT: OnPrintClient(hwnd, (HDC)wParam); return 0;
    }
    return DefWindowProc(hwnd, uiMsg, wParam, lParam);
}

BOOL InitApp(void)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = TEXT("Scratch");
    if (!RegisterClass(&wc)) return FALSE;
    InitCommonControls();               /* In case we use a common control */
    return TRUE;
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hinstPrev,
                   LPSTR lpCmdLine, int nShowCmd)
{
    MSG msg;
    HWND hwnd;
    g_hinst = hinst;
    if (!InitApp()) return 0;
    if (SUCCEEDED(CoInitialize(NULL)))
    {/* In case we use COM */
        hwnd = CreateWindow(
            TEXT("Scratch"),                /* Class Name */
            TEXT("Scratch"),                /* Title */
            WS_OVERLAPPEDWINDOW,            /* Style */
            CW_USEDEFAULT, CW_USEDEFAULT,   /* Position */
            CW_USEDEFAULT, CW_USEDEFAULT,   /* Size */
            NULL,                           /* Parent */
            NULL,                           /* No menu */
            hinst,                          /* Instance */
            0);                             /* No special parameters */
        ShowWindow(hwnd, nShowCmd);
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        CoUninitialize();
    }
    return 0;
}
