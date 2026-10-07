/*
 * dxgi.dll (SKJ Wine) - adds DXGI desktop duplication to the dxgi the prefix already has.
 *
 * Windows recorders get the screen with IDXGIOutput1::DuplicateOutput. Neither DXVK nor Wine
 * implements it, so recorders (SteelSeries GG Moments, ...) find nothing to record. This DLL sits
 * in front of the real dxgi (DXVK's, installed next to it as dxvk_dxgi.dll): everything is passed
 * straight through, except that the two DuplicateOutput methods of its outputs are replaced by
 * ours. The pictures come from bin/skj-screencast, a small Linux helper that gets the screen from
 * the desktop (xdg-desktop-portal + PipeWire) and puts the frames in shared memory - the layout
 * is described there. Without the helper running, DuplicateOutput fails exactly as before.
 *
 * Copyright 2026 SKJ Wine. LGPL-2.1-or-later.
 */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <stdio.h>
#include <d3d11.h>
#include <dxgi1_6.h>

#define HEADER 64
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

struct frame_header
{
    char magic[4];              /* "SKJS" */
    UINT version, width, height;
    volatile LONG seq;          /* odd while the helper is writing */
    volatile LONG active;
    UINT64 time;
};

struct duplication
{
    IDXGIOutputDuplication IDXGIOutputDuplication_iface;
    LONG ref;
    IDXGIOutput *output;
    ID3D11Device *device;
    UINT width, height;
    WCHAR dir[MAX_PATH];
    HANDLE file, mapping;
    const struct frame_header *view;
    LONG last_seq;
    ULONGLONG last_request;
    ID3D11Texture2D *texture;   /* the frame the program currently holds */
};

static struct duplication *impl(IDXGIOutputDuplication *iface)
{
    return CONTAINING_RECORD(iface, struct duplication, IDXGIOutputDuplication_iface);
}

/* the helper's folder; FALSE if the helper isn't running. skj-screencast links it into the prefix as
 * C:\ProgramData\skj-wine\screen (Wine doesn't pass XDG_RUNTIME_DIR on to Windows programs). */
static BOOL helper_dir(WCHAR *dir)
{
    WCHAR base[MAX_PATH], path[MAX_PATH + 32], *p;

    if (GetEnvironmentVariableW(L"SKJ_SCREENCAST_DIR", base, MAX_PATH - 64))    /* a Linux path, for testing */
    {
        swprintf(dir, MAX_PATH, L"\\\\?\\unix%ls", base);
        for (p = dir + 8; *p; p++) if (*p == '/') *p = '\\';
    }
    else lstrcpyW(dir, L"C:\\ProgramData\\skj-wine\\screen");
    swprintf(path, ARRAY_SIZE(path), L"%ls\\ready", dir);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

/* tell the helper that somebody wants frames; it stops capturing a few seconds after the last request */
static void request_frames(struct duplication *This)
{
    WCHAR path[MAX_PATH + 32];
    char text[32];
    HANDLE file;
    DWORD written;

    if (GetTickCount64() - This->last_request < 1000) return;
    This->last_request = GetTickCount64();
    swprintf(path, ARRAY_SIZE(path), L"%ls\\request", This->dir);
    file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
            CREATE_ALWAYS, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, text, sprintf(text, "%u %u\n", This->width, This->height), &written, NULL);
    CloseHandle(file);
}

static BOOL map_frames(struct duplication *This)
{
    WCHAR path[MAX_PATH + 32];
    LARGE_INTEGER size;

    if (This->view) return TRUE;
    swprintf(path, ARRAY_SIZE(path), L"%ls\\frame-%ux%u", This->dir, This->width, This->height);
    This->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
            OPEN_EXISTING, 0, NULL);
    if (This->file == INVALID_HANDLE_VALUE) return FALSE;
    if (GetFileSizeEx(This->file, &size) && size.QuadPart == HEADER + (LONGLONG)This->width * This->height * 4 &&
        (This->mapping = CreateFileMappingW(This->file, NULL, PAGE_READONLY, 0, 0, NULL)))
    {
        if ((This->view = MapViewOfFile(This->mapping, FILE_MAP_READ, 0, 0, 0)))
        {
            if (!memcmp(This->view->magic, "SKJS", 4) && This->view->version == 1) return TRUE;
            UnmapViewOfFile(This->view);
            This->view = NULL;
        }
        CloseHandle(This->mapping);
    }
    CloseHandle(This->file);
    return FALSE;
}

static HRESULT WINAPI duplication_QueryInterface(IDXGIOutputDuplication *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDXGIObject) ||
        IsEqualGUID(iid, &IID_IDXGIOutputDuplication))
    {
        IDXGIOutputDuplication_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI duplication_AddRef(IDXGIOutputDuplication *iface)
{
    return InterlockedIncrement(&impl(iface)->ref);
}

static ULONG WINAPI duplication_Release(IDXGIOutputDuplication *iface)
{
    struct duplication *This = impl(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    if (!ref)
    {
        if (This->texture) ID3D11Texture2D_Release(This->texture);
        if (This->view)
        {
            UnmapViewOfFile(This->view);
            CloseHandle(This->mapping);
            CloseHandle(This->file);
        }
        ID3D11Device_Release(This->device);
        IDXGIOutput_Release(This->output);
        free(This);
    }
    return ref;
}

static HRESULT WINAPI duplication_SetPrivateData(IDXGIOutputDuplication *iface, REFGUID guid, UINT size, const void *data)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI duplication_SetPrivateDataInterface(IDXGIOutputDuplication *iface, REFGUID guid, const IUnknown *object)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI duplication_GetPrivateData(IDXGIOutputDuplication *iface, REFGUID guid, UINT *size, void *data)
{
    return DXGI_ERROR_NOT_FOUND;
}

static HRESULT WINAPI duplication_GetParent(IDXGIOutputDuplication *iface, REFIID iid, void **parent)
{
    return IDXGIOutput_QueryInterface(impl(iface)->output, iid, parent);
}

static void WINAPI duplication_GetDesc(IDXGIOutputDuplication *iface, DXGI_OUTDUPL_DESC *desc)
{
    struct duplication *This = impl(iface);

    memset(desc, 0, sizeof(*desc));
    desc->ModeDesc.Width = This->width;
    desc->ModeDesc.Height = This->height;
    desc->ModeDesc.RefreshRate.Numerator = 60;
    desc->ModeDesc.RefreshRate.Denominator = 1;
    desc->ModeDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc->Rotation = DXGI_MODE_ROTATION_IDENTITY;
    desc->DesktopImageInSystemMemory = FALSE;
}

/* a new texture per frame: creating one is safe from any thread, using the program's device context is not */
static HRESULT make_texture(struct duplication *This, ID3D11Texture2D **texture)
{
    D3D11_TEXTURE2D_DESC desc = {0};
    D3D11_SUBRESOURCE_DATA data = {0};

    desc.Width = This->width;
    desc.Height = This->height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    data.pSysMem = (const BYTE *)This->view + HEADER;
    data.SysMemPitch = This->width * 4;
    return ID3D11Device_CreateTexture2D(This->device, &desc, &data, texture);
}

static HRESULT WINAPI duplication_AcquireNextFrame(IDXGIOutputDuplication *iface, UINT timeout,
        DXGI_OUTDUPL_FRAME_INFO *info, IDXGIResource **resource)
{
    struct duplication *This = impl(iface);
    ULONGLONG end = GetTickCount64() + timeout;
    ID3D11Texture2D *texture;
    HRESULT hr;
    LONG seq;

    if (!info || !resource) return E_INVALIDARG;
    *resource = NULL;
    memset(info, 0, sizeof(*info));
    if (This->texture) return DXGI_ERROR_INVALID_CALL;      /* ReleaseFrame first */

    for (;;)
    {
        request_frames(This);
        if (map_frames(This) && This->view->active && !((seq = This->view->seq) & 1) && seq != This->last_seq)
        {
            if (FAILED(hr = make_texture(This, &texture))) return hr;
            MemoryBarrier();
            if (This->view->seq == seq)     /* the helper didn't write into it meanwhile */
            {
                This->last_seq = seq;
                This->texture = texture;
                break;
            }
            ID3D11Texture2D_Release(texture);
            continue;
        }
        if (timeout != INFINITE && GetTickCount64() >= end) return DXGI_ERROR_WAIT_TIMEOUT;
        Sleep(1);
    }
    QueryPerformanceCounter(&info->LastPresentTime);
    info->AccumulatedFrames = 1;        /* the pointer is already drawn into the picture */
    return ID3D11Texture2D_QueryInterface(This->texture, &IID_IDXGIResource, (void **)resource);
}

static HRESULT WINAPI duplication_GetFrameDirtyRects(IDXGIOutputDuplication *iface, UINT size, RECT *rects, UINT *required)
{
    if (required) *required = 0;
    return S_OK;
}

static HRESULT WINAPI duplication_GetFrameMoveRects(IDXGIOutputDuplication *iface, UINT size,
        DXGI_OUTDUPL_MOVE_RECT *rects, UINT *required)
{
    if (required) *required = 0;
    return S_OK;
}

static HRESULT WINAPI duplication_GetFramePointerShape(IDXGIOutputDuplication *iface, UINT size, void *buffer,
        UINT *required, DXGI_OUTDUPL_POINTER_SHAPE_INFO *shape)
{
    if (required) *required = 0;
    if (shape) memset(shape, 0, sizeof(*shape));
    return S_OK;
}

static HRESULT WINAPI duplication_MapDesktopSurface(IDXGIOutputDuplication *iface, DXGI_MAPPED_RECT *rect)
{
    return DXGI_ERROR_UNSUPPORTED;      /* what Windows answers when the picture isn't in system memory */
}

static HRESULT WINAPI duplication_UnMapDesktopSurface(IDXGIOutputDuplication *iface)
{
    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT WINAPI duplication_ReleaseFrame(IDXGIOutputDuplication *iface)
{
    struct duplication *This = impl(iface);

    if (!This->texture) return DXGI_ERROR_INVALID_CALL;
    ID3D11Texture2D_Release(This->texture);
    This->texture = NULL;
    return S_OK;
}

static const IDXGIOutputDuplicationVtbl duplication_vtbl =
{
    duplication_QueryInterface, duplication_AddRef, duplication_Release,
    duplication_SetPrivateData, duplication_SetPrivateDataInterface, duplication_GetPrivateData, duplication_GetParent,
    duplication_GetDesc, duplication_AcquireNextFrame, duplication_GetFrameDirtyRects, duplication_GetFrameMoveRects,
    duplication_GetFramePointerShape, duplication_MapDesktopSurface, duplication_UnMapDesktopSurface,
    duplication_ReleaseFrame,
};

/* what the real dxgi answered before we took the methods over */
static HRESULT (WINAPI *real_DuplicateOutput)(IDXGIOutput5 *, IUnknown *, IDXGIOutputDuplication **);
static HRESULT (WINAPI *real_DuplicateOutput1)(IDXGIOutput5 *, IUnknown *, UINT, UINT, const DXGI_FORMAT *,
        IDXGIOutputDuplication **);

static HRESULT duplicate(IDXGIOutput5 *output, IUnknown *device, IDXGIOutputDuplication **out)
{
    struct duplication *This;
    DXGI_OUTPUT_DESC desc;
    HRESULT hr;

    if (!device || !out) return E_INVALIDARG;
    *out = NULL;
    if (!(This = calloc(1, sizeof(*This)))) return E_OUTOFMEMORY;
    if (!helper_dir(This->dir) || FAILED(IDXGIOutput5_GetDesc(output, &desc)) ||
        FAILED(IUnknown_QueryInterface(device, &IID_ID3D11Device, (void **)&This->device)))
    {
        free(This);
        return DXGI_ERROR_UNSUPPORTED;
    }
    This->IDXGIOutputDuplication_iface.lpVtbl = (IDXGIOutputDuplicationVtbl *)&duplication_vtbl;
    This->ref = 1;
    This->width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    This->height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
    hr = IDXGIOutput5_QueryInterface(output, &IID_IDXGIOutput, (void **)&This->output);
    if (FAILED(hr) || !This->width || !This->height)
    {
        if (This->output) IDXGIOutput_Release(This->output);
        ID3D11Device_Release(This->device);
        free(This);
        return DXGI_ERROR_UNSUPPORTED;
    }
    request_frames(This);
    *out = &This->IDXGIOutputDuplication_iface;
    return S_OK;
}

static HRESULT WINAPI skj_DuplicateOutput(IDXGIOutput5 *iface, IUnknown *device, IDXGIOutputDuplication **out)
{
    HRESULT hr = duplicate(iface, device, out);
    if (hr == DXGI_ERROR_UNSUPPORTED && real_DuplicateOutput) hr = real_DuplicateOutput(iface, device, out);
    return hr;
}

static HRESULT WINAPI skj_DuplicateOutput1(IDXGIOutput5 *iface, IUnknown *device, UINT flags, UINT count,
        const DXGI_FORMAT *formats, IDXGIOutputDuplication **out)
{
    HRESULT hr = duplicate(iface, device, out);     /* we always hand out BGRA, the format every caller lists */
    if (hr == DXGI_ERROR_UNSUPPORTED && real_DuplicateOutput1)
        hr = real_DuplicateOutput1(iface, device, flags, count, formats, out);
    return hr;
}

/* All outputs of one dxgi share one method table: replace the two DuplicateOutput entries in it. */
static void hook_outputs(IUnknown *factory_unk)
{
    static LONG done;
    IDXGIFactory *factory;
    IDXGIAdapter *adapter;
    IDXGIOutput *output;
    IDXGIOutput5 *output5;
    IDXGIOutput1 *output1;
    DWORD old;
    UINT i;

    if (done || FAILED(IUnknown_QueryInterface(factory_unk, &IID_IDXGIFactory, (void **)&factory))) return;
    for (i = 0; !done && SUCCEEDED(IDXGIFactory_EnumAdapters(factory, i, &adapter)); i++)
    {
        if (SUCCEEDED(IDXGIAdapter_EnumOutputs(adapter, 0, &output)))
        {
            if (SUCCEEDED(IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput5, (void **)&output5)))
            {
                IDXGIOutput5Vtbl *vtbl = output5->lpVtbl;
                if (!InterlockedExchange(&done, 1) && VirtualProtect(vtbl, sizeof(*vtbl), PAGE_READWRITE, &old))
                {
                    real_DuplicateOutput = vtbl->DuplicateOutput;
                    real_DuplicateOutput1 = vtbl->DuplicateOutput1;
                    vtbl->DuplicateOutput = skj_DuplicateOutput;
                    vtbl->DuplicateOutput1 = skj_DuplicateOutput1;
                    VirtualProtect(vtbl, sizeof(*vtbl), old, &old);
                }
                IDXGIOutput5_Release(output5);
            }
            else if (SUCCEEDED(IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput1, (void **)&output1)))
            {
                IDXGIOutput1Vtbl *vtbl = output1->lpVtbl;
                if (!InterlockedExchange(&done, 1) && VirtualProtect(vtbl, sizeof(*vtbl), PAGE_READWRITE, &old))
                {
                    real_DuplicateOutput = (void *)vtbl->DuplicateOutput;
                    vtbl->DuplicateOutput = (void *)skj_DuplicateOutput;
                    VirtualProtect(vtbl, sizeof(*vtbl), old, &old);
                }
                IDXGIOutput1_Release(output1);
            }
            IDXGIOutput_Release(output);
        }
        IDXGIAdapter_Release(adapter);
    }
    IDXGIFactory_Release(factory);
}

/* the real dxgi: DXVK's, installed under another name next to this file */
static FARPROC real(const char *name)
{
    static HMODULE module;

    if (!module && !(module = LoadLibraryW(L"dxvk_dxgi.dll"))) return NULL;
    return GetProcAddress(module, name);
}

HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID iid, void **factory)
{
    HRESULT (WINAPI *func)(UINT, REFIID, void **) = (void *)real("CreateDXGIFactory2");
    HRESULT hr;

    if (!func) return E_FAIL;
    if (SUCCEEDED(hr = func(flags, iid, factory)) && factory && *factory) hook_outputs(*factory);
    return hr;
}

HRESULT WINAPI CreateDXGIFactory1(REFIID iid, void **factory)
{
    HRESULT (WINAPI *func)(REFIID, void **) = (void *)real("CreateDXGIFactory1");
    HRESULT hr;

    if (!func) return E_FAIL;
    if (SUCCEEDED(hr = func(iid, factory)) && factory && *factory) hook_outputs(*factory);
    return hr;
}

HRESULT WINAPI CreateDXGIFactory(REFIID iid, void **factory)
{
    HRESULT (WINAPI *func)(REFIID, void **) = (void *)real("CreateDXGIFactory");
    HRESULT hr;

    if (!func) return E_FAIL;
    if (SUCCEEDED(hr = func(iid, factory)) && factory && *factory) hook_outputs(*factory);
    return hr;
}

HRESULT WINAPI DXGIDeclareAdapterRemovalSupport(void)
{
    HRESULT (WINAPI *func)(void) = (void *)real("DXGIDeclareAdapterRemovalSupport");
    return func ? func() : S_OK;
}

HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID iid, void **out)
{
    HRESULT (WINAPI *func)(UINT, REFIID, void **) = (void *)real("DXGIGetDebugInterface1");
    return func ? func(flags, iid, out) : E_NOINTERFACE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(inst);
    return TRUE;
}
