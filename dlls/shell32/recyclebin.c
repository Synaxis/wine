/*
 * Trash virtual folder support
 *
 * Copyright (C) 2006 Mikolaj Zalewski
 * Copyright 2011 Jay Yang
 * Copyright 2021 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"

#define COBJMACROS
#define NONAMELESSUNION

#include <stdarg.h>

#include "winerror.h"
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winuser.h"
#include "shlwapi.h"
#include "ntquery.h"
#include "shlobj.h"
#include "shresdef.h"
#include "shellfolder.h"
#include "shellapi.h"
#include "knownfolders.h"
#include "wine/debug.h"

#include "shell32_main.h"
#include "pidl.h"
#include "shfldr.h"

WINE_DEFAULT_DEBUG_CHANNEL(recyclebin);

static const shvheader RecycleBinColumns[] =
{
    {&FMTID_Storage,   PID_STG_NAME,       IDS_SHV_COLUMN1,        SHCOLSTATE_TYPE_STR|SHCOLSTATE_ONBYDEFAULT,  LVCFMT_LEFT,  30},
    {&FMTID_Displaced, PID_DISPLACED_FROM, IDS_SHV_COLUMN_DELFROM, SHCOLSTATE_TYPE_STR|SHCOLSTATE_ONBYDEFAULT,  LVCFMT_LEFT,  30},
    {&FMTID_Displaced, PID_DISPLACED_DATE, IDS_SHV_COLUMN_DELDATE, SHCOLSTATE_TYPE_DATE|SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT,  20},
    {&FMTID_Storage,   PID_STG_SIZE,       IDS_SHV_COLUMN2,        SHCOLSTATE_TYPE_INT|SHCOLSTATE_ONBYDEFAULT,  LVCFMT_RIGHT, 20},
    {&FMTID_Storage,   PID_STG_STORAGETYPE,IDS_SHV_COLUMN3,        SHCOLSTATE_TYPE_INT|SHCOLSTATE_ONBYDEFAULT,  LVCFMT_LEFT,  20},
    {&FMTID_Storage,   PID_STG_WRITETIME,  IDS_SHV_COLUMN4,        SHCOLSTATE_TYPE_DATE|SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT,  20},
/*    {&FMTID_Storage,   PID_STG_CREATETIME, "creation time",  SHCOLSTATE_TYPE_DATE,                        LVCFMT_LEFT,  20}, */
/*    {&FMTID_Storage,   PID_STG_ATTRIBUTES, "attribs",        SHCOLSTATE_TYPE_STR,                         LVCFMT_LEFT,  20},       */
};

#define COLUMN_NAME    0
#define COLUMN_DELFROM 1
#define COLUMN_DATEDEL 2
#define COLUMN_SIZE    3
#define COLUMN_TYPE    4
#define COLUMN_MTIME   5

#define COLUMNS_COUNT  6

static const WIN32_FIND_DATAW *get_trash_item_data( LPCITEMIDLIST id )
{
    if (id->mkid.cb < 2 + 1 + sizeof(WIN32_FIND_DATAW) + 2) return NULL;
    if (id->mkid.abID[0] != 0) return NULL;
    return (const WIN32_FIND_DATAW *)(id->mkid.abID + 1);
}

static INIT_ONCE trash_dir_once;
static WCHAR *trash_dir;
static WCHAR *trash_info_dir;
static ULONG random_seed;

static BOOL WINAPI init_trash_dirs( INIT_ONCE *once, void *param, void **context )
{
    static const WCHAR homedirW[] = {'W','I','N','E','H','O','M','E','D','I','R',0};
    WCHAR var[MAX_PATH], *info = NULL, *files = NULL;

#ifdef __APPLE__
    static const WCHAR trashW[] = {'\\','.','T','r','a','s','h',0};

    if (!GetEnvironmentVariableW( homedirW, var, MAX_PATH )) return TRUE;
    files = heap_alloc( (strlenW(var) + strlenW(trashW) + 1) * sizeof(WCHAR) );
    strcpyW( files, var );
    strcatW( files, trashW );
    files[1] = '\\';  /* change \??\ to \\?\ */
#else
    static const WCHAR dataW[] = {'X','D','G','_','D','A','T','A','_','H','O','M','E',0};
    static const WCHAR infoW[] = {'\\','i','n','f','o',0};
    static const WCHAR filesW[] = {'\\','f','i','l','e','s',0};
    static const WCHAR home_fmtW[] = {'%','s','/','.','l','o','c','a','l','/','s','h','a','r','e','/','T','r','a','s','h',0};
    static const WCHAR config_fmtW[] = {'\\','?','?','\\','u','n','i','x','%','s','/','T','r','a','s','h',0};
    const WCHAR *fmt = config_fmtW;
    WCHAR *p;

    if (!GetEnvironmentVariableW( dataW, var + 8, MAX_PATH - 8 ) || !var[8])
    {
        if (!GetEnvironmentVariableW( homedirW, var, MAX_PATH )) return TRUE;
        fmt = home_fmtW;
    }
    files = heap_alloc( (strlenW(var) + strlenW(fmt) + strlenW(filesW) + 1) * sizeof(WCHAR) );
    sprintfW( files, fmt, var );
    files[1] = '\\';  /* change \??\ to \\?\ */
    for (p = files; *p; p++) if (*p == '/') *p = '\\';
    CreateDirectoryW( files, NULL );
    info = heap_alloc( (strlenW(var) + strlenW(fmt) + strlenW(infoW) + 1) * sizeof(WCHAR) );
    strcpyW( info, files );
    strcatW( files, filesW );
    strcatW( info, infoW );
    if (!CreateDirectoryW( info, NULL ) && GetLastError() != ERROR_ALREADY_EXISTS) goto done;
    trash_info_dir = info;
#endif

    if (!CreateDirectoryW( files, NULL ) && GetLastError() != ERROR_ALREADY_EXISTS) goto done;
    trash_dir = files;
    random_seed = GetTickCount();
    return TRUE;

done:
    heap_free( files );
    heap_free( info );
    return TRUE;
}

BOOL is_trash_available(void)
{
    InitOnceExecuteOnce( &trash_dir_once, init_trash_dirs, NULL, NULL );
    return trash_dir != NULL;
}

static void encode( const char *src, char *dst )
{
    static const char escape_chars[] = "^&`{}|[]'<>\\#%\"+";
    static const char hexchars[] = "0123456789ABCDEF";

    for ( ; *src; src++)
    {
        if (*src <= 0x20 || *src >= 0x7f || strchr( escape_chars, *src ))
        {
            *dst++ = '%';
            *dst++ = hexchars[(unsigned char)*src / 16];
            *dst++ = hexchars[(unsigned char)*src % 16];
        }
        else *dst++ = *src;
    }
    *dst = 0;
}

static char *decode( char *buf )
{
    const char *src = buf;
    char *dst = buf;  /* decode in place */

    while (*src)
    {
        if (*src == '%')
        {
            unsigned char v;

            if (src[1] >= '0' && src[1] <= '9') v = src[1] - '0';
            else if (src[1] >= 'A' && src[1] <= 'F') v = src[1] - 'A' + 10;
            else if (src[1] >= 'a' && src[1] <= 'f') v = src[1] - 'a' + 10;
            else goto next;
            v <<= 4;
            if (src[2] >= '0' && src[2] <= '9') v += src[2] - '0';
            else if (src[2] >= 'A' && src[2] <= 'F') v += src[2] - 'A' + 10;
            else if (src[2] >= 'a' && src[2] <= 'f') v += src[2] - 'a' + 10;
            else goto next;
            *dst++ = v;
        next:
            if (src[1] && src[2]) src += 3;
            else break;
        }
        else *dst++ = *src++;
    }
    *dst = 0;
    return buf;
}

static BOOL write_trashinfo_file( HANDLE handle, const WCHAR *orig_path )
{
    char *path, *buffer;
    SYSTEMTIME now;

    if (!(path = wine_get_unix_file_name( orig_path ))) return FALSE;
    GetLocalTime( &now );

    buffer = heap_alloc( strlen(path) * 3 + 128 );
    strcpy( buffer, "[Trash Info]\nPath=" );
    encode( path, buffer + strlen(buffer) );
    sprintf( buffer + strlen(buffer), "\nDeletionDate=%04u-%02u-%02uT%02u:%02u:%02u\n",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    WriteFile( handle, buffer, strlen(buffer), NULL, NULL );
    heap_free( path );
    heap_free( buffer );
    return TRUE;
}

static void read_trashinfo_file( HANDLE handle, WIN32_FIND_DATAW *data )
{
    ULONG size;
    WCHAR *path;
    char *buffer, *next, *p;
    int header = 0;

    size = GetFileSize( handle, NULL );
    if (!(buffer = heap_alloc( size + 1 ))) return;
    if (!ReadFile( handle, buffer, size, &size, NULL )) size = 0;
    buffer[size] = 0;
    next = buffer;
    while (next)
    {
        p = next;
        if ((next = strchr( next, '\n' ))) *next++ = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncmp( p, "[Trash Info]", 12 ))
        {
            header++;
            continue;
        }
        if (header && !strncmp( p, "Path=", 5 ))
        {
            p += 5;
            if ((path = wine_get_dos_file_name( decode( p ))))
            {
                lstrcpynW( data->cFileName, path, MAX_PATH );
                heap_free( path );
            }
            else
            {
                /* show only the file name */
                char *file = strrchr( p, '/' );
                if (!file) file = p;
                else file++;
                MultiByteToWideChar( CP_UNIXCP, 0, file, -1, data->cFileName, MAX_PATH );
            }
            continue;
        }
        if (header && !strncmp( p, "DeletionDate=", 13 ))
        {
            int year, mon, day, hour, min, sec;
            if (sscanf( p + 13, "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &min, &sec ) == 6)
            {
                SYSTEMTIME time = { year, mon, 0, day, hour, min, sec };
                FILETIME ft;
                if (SystemTimeToFileTime( &time, &ft ))
                    LocalFileTimeToFileTime( &ft, &data->ftLastAccessTime );
            }
        }
    }
    heap_free( buffer );
}


BOOL trash_file( const WCHAR *path )
{
    WCHAR *dest = NULL, *file = PathFindFileNameW( path );
    BOOL ret = TRUE;
    ULONG i;

    InitOnceExecuteOnce( &trash_dir_once, init_trash_dirs, NULL, NULL );
    if (!trash_dir) return FALSE;

    dest = heap_alloc( (strlenW(trash_dir) + strlenW(file) + 11) * sizeof(WCHAR) );

    if (trash_info_dir)
    {
        static const WCHAR fmt[] = {'%','s','\\','%','s','.','t','r','a','s','h','i','n','f','o',0};
        static const WCHAR fmt2[] = {'%','s','\\','%','s','-','%','0','8','x','.','t','r','a','s','h','i','n','f','o',0};
        HANDLE handle;
        WCHAR *info = heap_alloc( (strlenW(trash_info_dir) + strlenW(file) + 21) * sizeof(WCHAR) );

        sprintfW( info, fmt, trash_info_dir, file );
        for (i = 0; i < 1000; i++)
        {
            handle = CreateFileW( info, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, 0 );
            if (handle != INVALID_HANDLE_VALUE) break;
            sprintfW( info, fmt2, trash_info_dir, file, RtlRandom( &random_seed ));
        }
        if (handle != INVALID_HANDLE_VALUE)
        {
            if ((ret = write_trashinfo_file( handle, path )))
            {
                static const WCHAR fmt[] = {'%','s','%','.','*','s',0};
                ULONG len = strlenW(info) - strlenW(trash_info_dir) - 10 /* .trashinfo */;
                sprintfW( dest, fmt, trash_dir, len, info + strlenW(trash_info_dir) );
                ret = MoveFileW( path, dest );
            }
            CloseHandle( handle );
            if (!ret) DeleteFileW( info );
        }
    }
    else
    {
        static const WCHAR fmt[] = {'%','s','\\','%','s',0};
        static const WCHAR fmt2[] = {'%','s','\\','%','s','-','%','0','8','x',0};

        sprintfW( dest, fmt, trash_dir, file );
        for (i = 0; i < 1000; i++)
        {
            ret = MoveFileW( path, dest );
            if (ret || GetLastError() != ERROR_ALREADY_EXISTS) break;
            sprintfW( dest, fmt2, trash_dir, file, RtlRandom( &random_seed ));
        }
    }
    if (ret) TRACE( "%s -> %s\n", debugstr_w(path), debugstr_w(dest) );
    heap_free( dest );
    return ret;
}

static BOOL get_trash_item_info( const WCHAR *filename, WIN32_FIND_DATAW *data )
{
    if (!trash_info_dir)
    {
        static const WCHAR dsstoreW[] = {'.','D','S','_','S','t','o','r','e',0};

        return !!strcmpW( filename, dsstoreW );
    }
    else
    {
        static const WCHAR fmt[] = {'%','s','\\','%','s','.','t','r','a','s','h','i','n','f','o',0};
        HANDLE handle;
        WCHAR *info = heap_alloc( (strlenW(trash_info_dir) + strlenW(filename) + 12) * sizeof(WCHAR) );

        sprintfW( info, fmt, trash_info_dir, filename );
        handle = CreateFileW( info, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0 );
        heap_free( info );
        if (handle != INVALID_HANDLE_VALUE) return FALSE;
        read_trashinfo_file( handle, data );
        CloseHandle( handle );
        return TRUE;
    }
}

static HRESULT add_trash_item( WIN32_FIND_DATAW *orig_data, LPITEMIDLIST **pidls,
                               ULONG *count, ULONG *size )
{
    ITEMIDLIST *pidl;
    WIN32_FIND_DATAW *data;
    const WCHAR *filename = orig_data->cFileName;
    ULONG len = offsetof( ITEMIDLIST, mkid.abID[1 + sizeof(*data) + (strlenW(filename)+1) * sizeof(WCHAR)]);

    if (!(pidl = SHAlloc( len + 2 ))) return E_OUTOFMEMORY;
    pidl->mkid.cb = len;
    pidl->mkid.abID[0] = 0;
    data = (WIN32_FIND_DATAW *)(pidl->mkid.abID + 1);
    memcpy( data, orig_data, sizeof(*data) );
    strcpyW( (WCHAR *)(data + 1), filename );
    *(USHORT *)((char *)pidl + len) = 0;

    if (get_trash_item_info( filename, data ))
    {
        if (*count == *size)
        {
            LPITEMIDLIST *new;

            *size = max( *size * 2, 32 );
            new = heap_realloc( *pidls, *size * sizeof(**pidls) );
            if (!new)
            {
                SHFree( pidl );
                return E_OUTOFMEMORY;
            }
            *pidls = new;
        }
        (*pidls)[(*count)++] = pidl;
    }
    else SHFree( pidl );  /* ignore it */
    return S_OK;
}

static HRESULT enum_trash_items( LPITEMIDLIST **pidls, int *ret_count )
{
    static const WCHAR wildcardW[] = {'\\','*',0};
    HANDLE handle;
    WCHAR *file;
    WIN32_FIND_DATAW data;
    LPITEMIDLIST *ret = NULL;
    ULONG count = 0, size = 0;
    HRESULT hr = S_OK;

    InitOnceExecuteOnce( &trash_dir_once, init_trash_dirs, NULL, NULL );
    if (!trash_dir) return E_FAIL;

    file = heap_alloc( (strlenW(trash_dir) + strlenW(wildcardW) + 1) * sizeof(WCHAR) );
    strcpyW( file, trash_dir );
    strcatW( file, wildcardW );
    handle = FindFirstFileW( file, &data );
    if (handle != INVALID_HANDLE_VALUE)
    {
        do
        {
            static const WCHAR dotW[] = {'.',0};
            static const WCHAR dotdotW[] = {'.','.',0};

            if (!strcmpW( data.cFileName, dotW )) continue;
            if (!strcmpW( data.cFileName, dotdotW )) continue;
            hr = add_trash_item( &data, &ret, &count, &size );
        } while (hr == S_OK && FindNextFileW( handle, &data ));
        FindClose( handle );
    }

    if (FAILED(hr)) while (count) SHFree( &ret[--count] );
    else if (count)
    {
        *pidls = SHAlloc( count * sizeof(**pidls) );
        memcpy( *pidls, ret, count * sizeof(**pidls) );
    }
    heap_free( ret );
    *ret_count = count;
    return hr;
}

static void remove_trashinfo( const WCHAR *filename )
{
    static const WCHAR fmt[] = {'%','s','\\','%','s','.','t','r','a','s','h','i','n','f','o',0};
    WCHAR *info;

    if (!trash_info_dir) return;
    info = heap_alloc( (strlenW(trash_info_dir) + strlenW(filename) + 12) * sizeof(WCHAR) );
    sprintfW( info, fmt, trash_info_dir, filename );
    DeleteFileW( info );
    heap_free( info );
}

static HRESULT restore_trash_item( LPCITEMIDLIST pidl )
{
    static const WCHAR fmt[] = {'%','s','\\','%','s',0};
    const WIN32_FIND_DATAW *data = get_trash_item_data( pidl );
    WCHAR *from, *filename = (WCHAR *)(data + 1);

    if (!strchrW( data->cFileName, '\\' ))
    {
        FIXME( "original name for %s not available\n", debugstr_w(data->cFileName) );
        return E_NOTIMPL;
    }
    from = heap_alloc( (strlenW(trash_dir) + strlenW(filename) + 2) * sizeof(WCHAR) );
    sprintfW( from, fmt, trash_dir, filename );
    if (MoveFileW( from, data->cFileName )) remove_trashinfo( filename );
    else WARN( "failed to restore %s to %s\n", debugstr_w(from), debugstr_w(data->cFileName) );
    heap_free( from );
    return S_OK;
}

static HRESULT erase_trash_item( LPCITEMIDLIST pidl )
{
    static const WCHAR fmt[] = {'%','s','\\','%','s',0};
    const WIN32_FIND_DATAW *data = get_trash_item_data( pidl );
    WCHAR *from, *filename = (WCHAR *)(data + 1);

    from = heap_alloc( (strlenW(trash_dir) + strlenW(filename) + 2) * sizeof(WCHAR) );
    sprintfW( from, fmt, trash_dir, filename );
    if (DeleteFileW( from )) remove_trashinfo( filename );
    heap_free( from );
    return S_OK;
}


static HRESULT FormatDateTime(LPWSTR buffer, int size, FILETIME ft)
{
    FILETIME lft;
    SYSTEMTIME time;
    int ret;

    FileTimeToLocalFileTime(&ft, &lft);
    FileTimeToSystemTime(&lft, &time);

    ret = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, NULL, buffer, size);
    if (ret>0 && ret<size)
    {
        /* Append space + time without seconds */
        buffer[ret-1] = ' ';
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &time, NULL, &buffer[ret], size - ret);
    }

    return (ret!=0 ? E_FAIL : S_OK);
}

typedef struct tagRecycleBinMenu
{
    IContextMenu2 IContextMenu2_iface;
    LONG refCount;

    UINT cidl;
    LPITEMIDLIST *apidl;
    IShellFolder2 *folder;
} RecycleBinMenu;

static const IContextMenu2Vtbl recycleBinMenuVtbl;

static RecycleBinMenu *impl_from_IContextMenu2(IContextMenu2 *iface)
{
    return CONTAINING_RECORD(iface, RecycleBinMenu, IContextMenu2_iface);
}

static IContextMenu2* RecycleBinMenu_Constructor(UINT cidl, LPCITEMIDLIST *apidl, IShellFolder2 *folder)
{
    RecycleBinMenu *This = SHAlloc(sizeof(RecycleBinMenu));
    TRACE("(%u,%p)\n",cidl,apidl);
    This->IContextMenu2_iface.lpVtbl = &recycleBinMenuVtbl;
    This->cidl = cidl;
    This->apidl = _ILCopyaPidl(apidl,cidl);
    IShellFolder2_AddRef(folder);
    This->folder = folder;
    This->refCount = 1;
    return &This->IContextMenu2_iface;
}

static HRESULT WINAPI RecycleBinMenu_QueryInterface(IContextMenu2 *iface,
                                                    REFIID riid,
                                                    void **ppvObject)
{
    RecycleBinMenu *This = impl_from_IContextMenu2(iface);
    TRACE("(%p, %s, %p) - stub\n", This, debugstr_guid(riid), ppvObject);
    return E_NOTIMPL;
}

static ULONG WINAPI RecycleBinMenu_AddRef(IContextMenu2 *iface)
{
    RecycleBinMenu *This = impl_from_IContextMenu2(iface);
    TRACE("(%p)\n", This);
    return InterlockedIncrement(&This->refCount);

}

static ULONG WINAPI RecycleBinMenu_Release(IContextMenu2 *iface)
{
    RecycleBinMenu *This = impl_from_IContextMenu2(iface);
    UINT result;
    TRACE("(%p)\n", This);
    result = InterlockedDecrement(&This->refCount);
    if (result == 0)
    {
        TRACE("Destroying object\n");
        _ILFreeaPidl(This->apidl,This->cidl);
        IShellFolder2_Release(This->folder);
        SHFree(This);
    }
    return result;
}

static HRESULT WINAPI RecycleBinMenu_QueryContextMenu(IContextMenu2 *iface,
                                                      HMENU hmenu,
                                                      UINT indexMenu,
                                                      UINT idCmdFirst,
                                                      UINT idCmdLast,
                                                      UINT uFlags)
{
    HMENU menures = LoadMenuW(shell32_hInstance,MAKEINTRESOURCEW(MENU_RECYCLEBIN));
    if(uFlags & CMF_DEFAULTONLY)
        return E_NOTIMPL;
    else{
        UINT idMax = Shell_MergeMenus(hmenu,GetSubMenu(menures,0),indexMenu,idCmdFirst,idCmdLast,MM_SUBMENUSHAVEIDS);
        TRACE("Added %d id(s)\n",idMax-idCmdFirst);
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, idMax-idCmdFirst+1);
    }
}

static void DoErase(RecycleBinMenu *This)
{
    ISFHelper *helper;
    IShellFolder2_QueryInterface(This->folder,&IID_ISFHelper,(void**)&helper);
    if(helper)
        ISFHelper_DeleteItems(helper,This->cidl,(LPCITEMIDLIST*)This->apidl);
}

static void DoRestore(RecycleBinMenu *This)
{

    /*TODO add prompts*/
    UINT i;
    for(i=0;i<This->cidl;i++)
    {
        const WIN32_FIND_DATAW *data = get_trash_item_data( This->apidl[i] );

        if(PathFileExistsW(data->cFileName))
        {
            PIDLIST_ABSOLUTE dest_pidl = ILCreateFromPathW(data->cFileName);
            WCHAR message[100];
            WCHAR caption[50];
            if(_ILIsFolder(ILFindLastID(dest_pidl)))
                LoadStringW(shell32_hInstance, IDS_RECYCLEBIN_OVERWRITEFOLDER, message, ARRAY_SIZE(message));
            else
                LoadStringW(shell32_hInstance, IDS_RECYCLEBIN_OVERWRITEFILE, message, ARRAY_SIZE(message));
            LoadStringW(shell32_hInstance, IDS_RECYCLEBIN_OVERWRITE_CAPTION, caption, ARRAY_SIZE(caption));

            if(ShellMessageBoxW(shell32_hInstance,GetActiveWindow(),message,
                                caption,MB_YESNO|MB_ICONEXCLAMATION,
                                data->cFileName)!=IDYES)
                continue;
        }
        if(SUCCEEDED(restore_trash_item(This->apidl[i])))
        {
            IPersistFolder2 *persist;
            LPITEMIDLIST root_pidl;
            PIDLIST_ABSOLUTE dest_pidl = ILCreateFromPathW(data->cFileName);
            BOOL is_folder = _ILIsFolder(ILFindLastID(dest_pidl));
            IShellFolder2_QueryInterface(This->folder,&IID_IPersistFolder2,
                                         (void**)&persist);
            IPersistFolder2_GetCurFolder(persist,&root_pidl);
            SHChangeNotify(is_folder ? SHCNE_RMDIR : SHCNE_DELETE,
                           SHCNF_IDLIST,ILCombine(root_pidl,This->apidl[i]),0);
            SHChangeNotify(is_folder ? SHCNE_MKDIR : SHCNE_CREATE,
                           SHCNF_IDLIST,dest_pidl,0);
            ILFree(dest_pidl);
            ILFree(root_pidl);
        }
    }
}

static HRESULT WINAPI RecycleBinMenu_InvokeCommand(IContextMenu2 *iface,
                                                   LPCMINVOKECOMMANDINFO pici)
{
    RecycleBinMenu *This = impl_from_IContextMenu2(iface);
    LPCSTR verb = pici->lpVerb;
    if(IS_INTRESOURCE(verb))
    {
        switch(LOWORD(verb))
        {
        case IDM_RECYCLEBIN_ERASE:
            DoErase(This);
            break;
        case IDM_RECYCLEBIN_RESTORE:
            DoRestore(This);
            break;
        default:
            return E_NOTIMPL;
        }
    }
    return S_OK;
}

static HRESULT WINAPI RecycleBinMenu_GetCommandString(IContextMenu2 *iface,
                                                      UINT_PTR idCmd,
                                                      UINT uType,
                                                      UINT *pwReserved,
                                                      LPSTR pszName,
                                                      UINT cchMax)
{
    TRACE("(%p, %lu, %u, %p, %s, %u) - stub\n",iface,idCmd,uType,pwReserved,debugstr_a(pszName),cchMax);
    return E_NOTIMPL;
}

static HRESULT WINAPI RecycleBinMenu_HandleMenuMsg(IContextMenu2 *iface,
                                                   UINT uMsg, WPARAM wParam,
                                                   LPARAM lParam)
{
    TRACE("(%p, %u, 0x%lx, 0x%lx) - stub\n",iface,uMsg,wParam,lParam);
    return E_NOTIMPL;
}


static const IContextMenu2Vtbl recycleBinMenuVtbl =
{
    RecycleBinMenu_QueryInterface,
    RecycleBinMenu_AddRef,
    RecycleBinMenu_Release,
    RecycleBinMenu_QueryContextMenu,
    RecycleBinMenu_InvokeCommand,
    RecycleBinMenu_GetCommandString,
    RecycleBinMenu_HandleMenuMsg,
};

/*
 * Recycle Bin folder
 */

typedef struct tagRecycleBin
{
    IShellFolder2 IShellFolder2_iface;
    IPersistFolder2 IPersistFolder2_iface;
    ISFHelper ISFHelper_iface;
    LONG refCount;

    LPITEMIDLIST pidl;
} RecycleBin;

static const IShellFolder2Vtbl recycleBinVtbl;
static const IPersistFolder2Vtbl recycleBinPersistVtbl;
static const ISFHelperVtbl sfhelperVtbl;

static inline RecycleBin *impl_from_IShellFolder2(IShellFolder2 *iface)
{
    return CONTAINING_RECORD(iface, RecycleBin, IShellFolder2_iface);
}

static RecycleBin *impl_from_IPersistFolder2(IPersistFolder2 *iface)
{
    return CONTAINING_RECORD(iface, RecycleBin, IPersistFolder2_iface);
}

static RecycleBin *impl_from_ISFHelper(ISFHelper *iface)
{
    return CONTAINING_RECORD(iface, RecycleBin, ISFHelper_iface);
}

static void RecycleBin_Destructor(RecycleBin *This);

HRESULT WINAPI RecycleBin_Constructor(IUnknown *pUnkOuter, REFIID riid, LPVOID *ppOutput)
{
    RecycleBin *obj;
    HRESULT ret;
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    obj = SHAlloc(sizeof(RecycleBin));
    if (obj == NULL)
        return E_OUTOFMEMORY;
    ZeroMemory(obj, sizeof(RecycleBin));
    obj->IShellFolder2_iface.lpVtbl = &recycleBinVtbl;
    obj->IPersistFolder2_iface.lpVtbl = &recycleBinPersistVtbl;
    obj->ISFHelper_iface.lpVtbl = &sfhelperVtbl;
    if (FAILED(ret = IPersistFolder2_QueryInterface(&obj->IPersistFolder2_iface, riid, ppOutput)))
    {
        RecycleBin_Destructor(obj);
        return ret;
    }
/*    InterlockedIncrement(&objCount);*/
    return S_OK;
}

static void RecycleBin_Destructor(RecycleBin *This)
{
/*    InterlockedDecrement(&objCount);*/
    SHFree(This->pidl);
    SHFree(This);
}

static HRESULT WINAPI RecycleBin_QueryInterface(IShellFolder2 *iface, REFIID riid, void **ppvObject)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    TRACE("(%p, %s, %p)\n", This, debugstr_guid(riid), ppvObject);

    *ppvObject = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IShellFolder)
            || IsEqualGUID(riid, &IID_IShellFolder2))
        *ppvObject = &This->IShellFolder2_iface;

    if (IsEqualGUID(riid, &IID_IPersist) || IsEqualGUID(riid, &IID_IPersistFolder)
            || IsEqualGUID(riid, &IID_IPersistFolder2))
        *ppvObject = &This->IPersistFolder2_iface;
    if (IsEqualGUID(riid, &IID_ISFHelper))
        *ppvObject = &This->ISFHelper_iface;

    if (*ppvObject != NULL)
    {
        IUnknown_AddRef((IUnknown *)*ppvObject);
        return S_OK;
    }
    WARN("no interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI RecycleBin_AddRef(IShellFolder2 *iface)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    TRACE("(%p)\n", This);
    return InterlockedIncrement(&This->refCount);
}

static ULONG WINAPI RecycleBin_Release(IShellFolder2 *iface)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    LONG result;

    TRACE("(%p)\n", This);
    result = InterlockedDecrement(&This->refCount);
    if (result == 0)
    {
        TRACE("Destroy object\n");
        RecycleBin_Destructor(This);
    }
    return result;
}

static HRESULT WINAPI RecycleBin_ParseDisplayName(IShellFolder2 *This, HWND hwnd, LPBC pbc,
            LPOLESTR pszDisplayName, ULONG *pchEaten, LPITEMIDLIST *ppidl,
            ULONG *pdwAttributes)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI RecycleBin_EnumObjects(IShellFolder2 *iface, HWND hwnd, SHCONTF grfFlags, IEnumIDList **ppenumIDList)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    IEnumIDListImpl *list;
    LPITEMIDLIST *pidls;
    HRESULT ret = E_OUTOFMEMORY;
    int pidls_count = 0;
    int i=0;

    TRACE("(%p, %p, %x, %p)\n", This, hwnd, grfFlags, ppenumIDList);

    *ppenumIDList = NULL;
    list = IEnumIDList_Constructor();
    if (!list)
        return E_OUTOFMEMORY;

    if (grfFlags & SHCONTF_NONFOLDERS)
    {
        if (FAILED(ret = enum_trash_items(&pidls, &pidls_count)))
            goto failed;
        for (i=0; i<pidls_count; i++)
            if (!AddToEnumList(list, pidls[i]))
                goto failed;
    }

    *ppenumIDList = &list->IEnumIDList_iface;
    return S_OK;

failed:
    IEnumIDList_Release(&list->IEnumIDList_iface);
    for (; i<pidls_count; i++)
        ILFree(pidls[i]);
    SHFree(pidls);
    return ret;
}

static HRESULT WINAPI RecycleBin_BindToObject(IShellFolder2 *This, LPCITEMIDLIST pidl, LPBC pbc, REFIID riid, void **ppv)
{
    FIXME("(%p, %p, %p, %s, %p) - stub\n", This, pidl, pbc, debugstr_guid(riid), ppv);
    return E_NOTIMPL;
}

static HRESULT WINAPI RecycleBin_BindToStorage(IShellFolder2 *This, LPCITEMIDLIST pidl, LPBC pbc, REFIID riid, void **ppv)
{
    FIXME("(%p, %p, %p, %s, %p) - stub\n", This, pidl, pbc, debugstr_guid(riid), ppv);
    return E_NOTIMPL;
}

static HRESULT WINAPI RecycleBin_CompareIDs(IShellFolder2 *iface, LPARAM lParam, LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    int ret;

    /* TODO */
    TRACE("(%p, %p, %p, %p)\n", This, (void *)lParam, pidl1, pidl2);
    if (pidl1->mkid.cb != pidl2->mkid.cb)
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, pidl1->mkid.cb - pidl2->mkid.cb);
    /* Looks too complicated, but in optimized memcpy we might get
     * a 32bit wide difference and would truncate it to 16 bit, so
     * erroneously returning equality. */
    ret = memcmp(pidl1->mkid.abID, pidl2->mkid.abID, pidl1->mkid.cb);
    if (ret < 0) ret = -1;
    if (ret > 0) ret =  1;
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (unsigned short)ret);
}

static HRESULT WINAPI RecycleBin_CreateViewObject(IShellFolder2 *iface, HWND hwndOwner, REFIID riid, void **ppv)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    HRESULT ret;
    TRACE("(%p, %p, %s, %p)\n", This, hwndOwner, debugstr_guid(riid), ppv);

    *ppv = NULL;
    if (IsEqualGUID(riid, &IID_IShellView))
    {
        IShellView *tmp;
        CSFV sfv;

        ZeroMemory(&sfv, sizeof(sfv));
        sfv.cbSize = sizeof(sfv);
        sfv.pshf = (IShellFolder *)&This->IShellFolder2_iface;

        TRACE("Calling SHCreateShellFolderViewEx\n");
        ret = SHCreateShellFolderViewEx(&sfv, &tmp);
        TRACE("Result: %08x, output: %p\n", (unsigned int)ret, tmp);
        *ppv = tmp;
        return ret;
    }
    else
        FIXME("invalid/unsupported interface %s\n", debugstr_guid(riid));

    return E_NOINTERFACE;
}

static HRESULT WINAPI RecycleBin_GetAttributesOf(IShellFolder2 *This, UINT cidl, LPCITEMIDLIST *apidl,
                                   SFGAOF *rgfInOut)
{
    TRACE("(%p, %d, {%p, ...}, {%x})\n", This, cidl, apidl[0], *rgfInOut);
    *rgfInOut &= SFGAO_CANMOVE|SFGAO_CANDELETE|SFGAO_HASPROPSHEET|SFGAO_FILESYSTEM;
    return S_OK;
}

static HRESULT WINAPI RecycleBin_GetUIObjectOf(IShellFolder2 *iface, HWND hwndOwner, UINT cidl, LPCITEMIDLIST *apidl,
                      REFIID riid, UINT *rgfReserved, void **ppv)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    *ppv = NULL;
    if(IsEqualGUID(riid, &IID_IContextMenu) || IsEqualGUID(riid, &IID_IContextMenu2))
    {
        TRACE("(%p, %p, %d, {%p, ...}, %s, %p, %p)\n", This, hwndOwner, cidl, apidl[0], debugstr_guid(riid), rgfReserved, ppv);
        *ppv = RecycleBinMenu_Constructor(cidl,apidl,&(This->IShellFolder2_iface));
        return S_OK;
    }
    FIXME("(%p, %p, %d, {%p, ...}, %s, %p, %p): stub!\n", iface, hwndOwner, cidl, apidl[0], debugstr_guid(riid), rgfReserved, ppv);

    return E_NOTIMPL;
}

static HRESULT WINAPI RecycleBin_GetDisplayNameOf(IShellFolder2 *This, LPCITEMIDLIST pidl, SHGDNF uFlags, STRRET *pName)
{
    const WIN32_FIND_DATAW *data = get_trash_item_data( pidl );

    TRACE("(%p, %p, %x, %p)\n", This, pidl, uFlags, pName);
    pName->uType = STRRET_WSTR;
    return SHStrDupW(PathFindFileNameW(data->cFileName), &pName->u.pOleStr);
}

static HRESULT WINAPI RecycleBin_SetNameOf(IShellFolder2 *This, HWND hwnd, LPCITEMIDLIST pidl, LPCOLESTR pszName,
            SHGDNF uFlags, LPITEMIDLIST *ppidlOut)
{
    TRACE("\n");
    return E_FAIL; /* not supported */
}

static HRESULT WINAPI RecycleBin_GetClassID(IPersistFolder2 *This, CLSID *pClassID)
{
    TRACE("(%p, %p)\n", This, pClassID);
    if (This == NULL || pClassID == NULL)
        return E_INVALIDARG;
    *pClassID = CLSID_RecycleBin;
    return S_OK;
}

static HRESULT WINAPI RecycleBin_Initialize(IPersistFolder2 *iface, LPCITEMIDLIST pidl)
{
    RecycleBin *This = impl_from_IPersistFolder2(iface);
    TRACE("(%p, %p)\n", This, pidl);

    This->pidl = ILClone(pidl);
    if (This->pidl == NULL)
        return E_OUTOFMEMORY;
    return S_OK;
}

static HRESULT WINAPI RecycleBin_GetCurFolder(IPersistFolder2 *iface, LPITEMIDLIST *ppidl)
{
    RecycleBin *This = impl_from_IPersistFolder2(iface);
    TRACE("\n");
    *ppidl = ILClone(This->pidl);
    return S_OK;
}

static HRESULT WINAPI RecycleBin_GetDefaultSearchGUID(IShellFolder2 *iface, GUID *guid)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    TRACE("(%p)->(%p)\n", This, guid);
    return E_NOTIMPL;
}

static HRESULT WINAPI RecycleBin_EnumSearches(IShellFolder2 *iface, IEnumExtraSearch **ppEnum)
{
    FIXME("stub\n");
    *ppEnum = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI RecycleBin_GetDefaultColumn(IShellFolder2 *iface, DWORD reserved, ULONG *sort, ULONG *display)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);

    TRACE("(%p)->(%#x, %p, %p)\n", This, reserved, sort, display);

    return E_NOTIMPL;
}

static HRESULT WINAPI RecycleBin_GetDefaultColumnState(IShellFolder2 *iface, UINT iColumn, SHCOLSTATEF *pcsFlags)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    TRACE("(%p, %d, %p)\n", This, iColumn, pcsFlags);
    if (iColumn >= COLUMNS_COUNT)
        return E_INVALIDARG;
    *pcsFlags = RecycleBinColumns[iColumn].pcsFlags;
    return S_OK;
}

static HRESULT WINAPI RecycleBin_GetDetailsEx(IShellFolder2 *iface, LPCITEMIDLIST pidl, const SHCOLUMNID *pscid, VARIANT *pv)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI RecycleBin_GetDetailsOf(IShellFolder2 *iface, LPCITEMIDLIST pidl, UINT iColumn, LPSHELLDETAILS pDetails)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    const WIN32_FIND_DATAW *data;
    WCHAR buffer[MAX_PATH];

    TRACE("(%p, %p, %d, %p)\n", This, pidl, iColumn, pDetails);
    if (iColumn >= COLUMNS_COUNT)
        return E_FAIL;

    if (!pidl) return SHELL32_GetColumnDetails( RecycleBinColumns, iColumn, pDetails );

    if (iColumn == COLUMN_NAME)
        return RecycleBin_GetDisplayNameOf(iface, pidl, SHGDN_NORMAL, &pDetails->str);

    data = get_trash_item_data( pidl );
    switch (iColumn)
    {
        case COLUMN_DATEDEL:
            FormatDateTime(buffer, MAX_PATH, data->ftLastAccessTime);
            break;
        case COLUMN_DELFROM:
            lstrcpyW(buffer, data->cFileName);
            PathRemoveFileSpecW(buffer);
            break;
        case COLUMN_SIZE:
            StrFormatKBSizeW(((LONGLONG)data->nFileSizeHigh<<32)|data->nFileSizeLow, buffer, MAX_PATH);
            break;
        case COLUMN_MTIME:
            FormatDateTime(buffer, MAX_PATH, data->ftLastWriteTime);
            break;
        case COLUMN_TYPE:
            /* TODO */
            buffer[0] = 0;
            break;
        default:
            return E_FAIL;
    }
    
    pDetails->str.uType = STRRET_WSTR;
    return SHStrDupW(buffer, &pDetails->str.u.pOleStr);
}

static HRESULT WINAPI RecycleBin_MapColumnToSCID(IShellFolder2 *iface, UINT iColumn, SHCOLUMNID *pscid)
{
    RecycleBin *This = impl_from_IShellFolder2(iface);
    TRACE("(%p, %d, %p)\n", This, iColumn, pscid);
    if (iColumn>=COLUMNS_COUNT)
        return E_INVALIDARG;

    return shellfolder_map_column_to_scid(RecycleBinColumns, iColumn, pscid);
}

static const IShellFolder2Vtbl recycleBinVtbl = 
{
    /* IUnknown */
    RecycleBin_QueryInterface,
    RecycleBin_AddRef,
    RecycleBin_Release,

    /* IShellFolder */
    RecycleBin_ParseDisplayName,
    RecycleBin_EnumObjects,
    RecycleBin_BindToObject,
    RecycleBin_BindToStorage,
    RecycleBin_CompareIDs,
    RecycleBin_CreateViewObject,
    RecycleBin_GetAttributesOf,
    RecycleBin_GetUIObjectOf,
    RecycleBin_GetDisplayNameOf,
    RecycleBin_SetNameOf,

    /* IShellFolder2 */
    RecycleBin_GetDefaultSearchGUID,
    RecycleBin_EnumSearches,
    RecycleBin_GetDefaultColumn,
    RecycleBin_GetDefaultColumnState,
    RecycleBin_GetDetailsEx,
    RecycleBin_GetDetailsOf,
    RecycleBin_MapColumnToSCID
};

static HRESULT WINAPI RecycleBin_IPersistFolder2_QueryInterface(IPersistFolder2 *iface, REFIID riid,
        void **ppvObject)
{
    RecycleBin *This = impl_from_IPersistFolder2(iface);

    return IShellFolder2_QueryInterface(&This->IShellFolder2_iface, riid, ppvObject);
}

static ULONG WINAPI RecycleBin_IPersistFolder2_AddRef(IPersistFolder2 *iface)
{
    RecycleBin *This = impl_from_IPersistFolder2(iface);

    return IShellFolder2_AddRef(&This->IShellFolder2_iface);
}

static ULONG WINAPI RecycleBin_IPersistFolder2_Release(IPersistFolder2 *iface)
{
    RecycleBin *This = impl_from_IPersistFolder2(iface);

    return IShellFolder2_Release(&This->IShellFolder2_iface);
}

static const IPersistFolder2Vtbl recycleBinPersistVtbl =
{
    /* IUnknown */
    RecycleBin_IPersistFolder2_QueryInterface,
    RecycleBin_IPersistFolder2_AddRef,
    RecycleBin_IPersistFolder2_Release,

    /* IPersist */
    RecycleBin_GetClassID,
    /* IPersistFolder */
    RecycleBin_Initialize,
    /* IPersistFolder2 */
    RecycleBin_GetCurFolder
};

static HRESULT WINAPI RecycleBin_ISFHelper_QueryInterface(ISFHelper *iface, REFIID riid,
        void **ppvObject)
{
    RecycleBin *This = impl_from_ISFHelper(iface);

    return IShellFolder2_QueryInterface(&This->IShellFolder2_iface, riid, ppvObject);
}

static ULONG WINAPI RecycleBin_ISFHelper_AddRef(ISFHelper *iface)
{
    RecycleBin *This = impl_from_ISFHelper(iface);

    return IShellFolder2_AddRef(&This->IShellFolder2_iface);
}

static ULONG WINAPI RecycleBin_ISFHelper_Release(ISFHelper *iface)
{
    RecycleBin *This = impl_from_ISFHelper(iface);

    return IShellFolder2_Release(&This->IShellFolder2_iface);
}

static HRESULT WINAPI RecycleBin_GetUniqueName(ISFHelper *iface,LPWSTR lpName,
                                               UINT  uLen)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI RecycleBin_AddFolder(ISFHelper * iface, HWND hwnd,
                                           LPCWSTR pwszName,
                                           LPITEMIDLIST * ppidlOut)
{
    /*Adding folders doesn't make sense in the recycle bin*/
    return E_NOTIMPL;
}

static HRESULT erase_items(HWND parent,const LPCITEMIDLIST * apidl, UINT cidl, BOOL confirm)
{
    UINT i=0;
    HRESULT ret = S_OK;
    LPITEMIDLIST recyclebin;

    if(confirm)
    {
        WCHAR arg[MAX_PATH];
        WCHAR message[100];
        WCHAR caption[50];
        switch(cidl)
        {
        case 0:
            return S_OK;
        case 1:
            {
                const WIN32_FIND_DATAW *data = get_trash_item_data( apidl[0] );
                lstrcpynW(arg,data->cFileName,MAX_PATH);
                LoadStringW(shell32_hInstance, IDS_RECYCLEBIN_ERASEITEM, message, ARRAY_SIZE(message));
                break;
            }
        default:
            {
                static const WCHAR format[]={'%','u','\0'};
                LoadStringW(shell32_hInstance, IDS_RECYCLEBIN_ERASEMULTIPLE, message, ARRAY_SIZE(message));
                sprintfW(arg,format,cidl);
                break;
            }

        }
        LoadStringW(shell32_hInstance, IDS_RECYCLEBIN_ERASE_CAPTION, caption, ARRAY_SIZE(caption));
        if(ShellMessageBoxW(shell32_hInstance,parent,message,caption,
                            MB_YESNO|MB_ICONEXCLAMATION,arg)!=IDYES)
            return ret;

    }
    SHGetFolderLocation(parent,CSIDL_BITBUCKET,0,0,&recyclebin);
    for (; i<cidl; i++)
    {
        if(SUCCEEDED(erase_trash_item(apidl[i])))
            SHChangeNotify(SHCNE_DELETE,SHCNF_IDLIST,
                           ILCombine(recyclebin,apidl[i]),0);
    }
    ILFree(recyclebin);
    return S_OK;
}

static HRESULT WINAPI RecycleBin_DeleteItems(ISFHelper * iface, UINT cidl,
                                             LPCITEMIDLIST * apidl)
{
    TRACE("(%p,%u,%p)\n",iface,cidl,apidl);
    return erase_items(GetActiveWindow(),apidl,cidl,TRUE);
}

static HRESULT WINAPI RecycleBin_CopyItems(ISFHelper * iface,
                                           IShellFolder * pSFFrom,
                                           UINT cidl, LPCITEMIDLIST * apidl)
{
    return E_NOTIMPL;
}

static const ISFHelperVtbl sfhelperVtbl =
{
    RecycleBin_ISFHelper_QueryInterface,
    RecycleBin_ISFHelper_AddRef,
    RecycleBin_ISFHelper_Release,
    RecycleBin_GetUniqueName,
    RecycleBin_AddFolder,
    RecycleBin_DeleteItems,
    RecycleBin_CopyItems
};

HRESULT WINAPI SHQueryRecycleBinA(LPCSTR pszRootPath, LPSHQUERYRBINFO pSHQueryRBInfo)
{
    WCHAR wszRootPath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, pszRootPath, -1, wszRootPath, MAX_PATH);
    return SHQueryRecycleBinW(wszRootPath, pSHQueryRBInfo);
}

HRESULT WINAPI SHQueryRecycleBinW(LPCWSTR pszRootPath, LPSHQUERYRBINFO pSHQueryRBInfo)
{
    LPITEMIDLIST *apidl;
    INT cidl;
    INT i=0;
    HRESULT hr;

    TRACE("(%s, %p)\n", debugstr_w(pszRootPath), pSHQueryRBInfo);

    hr = enum_trash_items(&apidl, &cidl);
    if (FAILED(hr))
        return hr;
    pSHQueryRBInfo->i64NumItems = cidl;
    pSHQueryRBInfo->i64Size = 0;
    for (; i<cidl; i++)
    {
        const WIN32_FIND_DATAW *data = get_trash_item_data( apidl[i] );
        pSHQueryRBInfo->i64Size += ((DWORDLONG)data->nFileSizeHigh << 32) + data->nFileSizeLow;
        ILFree(apidl[i]);
    }
    SHFree(apidl);
    return S_OK;
}

HRESULT WINAPI SHEmptyRecycleBinA(HWND hwnd, LPCSTR pszRootPath, DWORD dwFlags)
{
    WCHAR wszRootPath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, pszRootPath, -1, wszRootPath, MAX_PATH);
    return SHEmptyRecycleBinW(hwnd, wszRootPath, dwFlags);
}

#define SHERB_NOCONFIRMATION 1
#define SHERB_NOPROGRESSUI   2
#define SHERB_NOSOUND        4

HRESULT WINAPI SHEmptyRecycleBinW(HWND hwnd, LPCWSTR pszRootPath, DWORD dwFlags)
{
    LPITEMIDLIST *apidl;
    INT cidl;
    INT i=0;
    HRESULT ret;

    TRACE("(%p, %s, 0x%08x)\n", hwnd, debugstr_w(pszRootPath) , dwFlags);

    ret = enum_trash_items(&apidl, &cidl);
    if (FAILED(ret))
        return ret;

    ret = erase_items(hwnd,(const LPCITEMIDLIST*)apidl,cidl,!(dwFlags & SHERB_NOCONFIRMATION));
    for (;i<cidl;i++)
        ILFree(apidl[i]);
    SHFree(apidl);
    return ret;
}

/*************************************************************************
 * SHUpdateRecycleBinIcon                                [SHELL32.@]
 *
 * Undocumented
 */
HRESULT WINAPI SHUpdateRecycleBinIcon(void)
{
    FIXME("stub\n");
    return S_OK;
}
