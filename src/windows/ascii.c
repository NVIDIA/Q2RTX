#include <windows.h>

HANDLE WINAPI FindFirstFileA(LPCSTR path, LPWIN32_FIND_DATAA data)
{
    WCHAR wbuffer[MAX_PATH];
    HANDLE ret;
    WIN32_FIND_DATAW wdata;

    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wbuffer, MAX_PATH)) {
        return INVALID_HANDLE_VALUE;
    }

    ret = FindFirstFileW(wbuffer, &wdata);
    if (ret != INVALID_HANDLE_VALUE) {
        memcpy(data, &wdata, FIELD_OFFSET(WIN32_FIND_DATAA, cFileName));
        WideCharToMultiByte(CP_ACP, 0, wdata.cFileName, -1, data->cFileName, MAX_PATH, NULL, NULL);
    }

    return ret;
}

BOOL WINAPI FindNextFileA(HANDLE handle, LPWIN32_FIND_DATAA data)
{
    BOOL ret;
    WIN32_FIND_DATAW wdata;

    ret = FindNextFileW(handle, &wdata);
    if (ret != FALSE) {
        memcpy(data, &wdata, FIELD_OFFSET(WIN32_FIND_DATAA, cFileName));
        WideCharToMultiByte(CP_ACP, 0, wdata.cFileName, -1, data->cFileName, MAX_PATH, NULL, NULL);
    }

    return ret;
}

HINSTANCE WINAPI LoadLibraryA(LPCSTR path)
{
    WCHAR wbuffer[MAX_PATH];

    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wbuffer, MAX_PATH)) {
        return NULL;
    }

    return LoadLibraryW(wbuffer);
}

int WINAPI MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
    WCHAR wText[4096];
    WCHAR wCaption[256];

    if (!MultiByteToWideChar(CP_ACP, 0, lpText, -1, wText, 4096)) {
        return 0;
    }
    if (!MultiByteToWideChar(CP_ACP, 0, lpCaption, -1, wCaption, 256)) {
        return 0;
    }

    return MessageBoxW(hWnd, wText, wCaption, uType);
}

BOOL WINAPI CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    WCHAR wbuffer[MAX_PATH];

    if (!MultiByteToWideChar(CP_ACP, 0, lpPathName, -1, wbuffer, MAX_PATH)) {
        return FALSE;
    }

    return CreateDirectoryW(wbuffer, lpSecurityAttributes);
}

BOOL WINAPI GetFileAttributesExA(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
    WCHAR wbuffer[MAX_PATH];

    if (!MultiByteToWideChar(CP_ACP, 0, lpFileName, -1, wbuffer, MAX_PATH)) {
        return FALSE;
    }

    return GetFileAttributesExW(wbuffer, fInfoLevelId, lpFileInformation);
}

DWORD WINAPI GetModuleFileNameA(HMODULE hModule, LPSTR lpFileName, DWORD nSize)
{
    WCHAR wbuffer[MAX_PATH];
    DWORD ret;

    if (nSize > MAX_PATH) {
        nSize = MAX_PATH;
    }

    ret = GetModuleFileNameW(hModule, wbuffer, nSize);
    if (ret) {
        if (!WideCharToMultiByte(CP_ACP, 0, wbuffer, ret, lpFileName, ret, NULL, NULL)) {
            return 0;
        }
    }

    return ret;
}

