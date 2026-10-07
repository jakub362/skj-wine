/* duptest.exe: does DXGI desktop duplication (what screen recorders use) work in this prefix?
 *   duptest.exe [out.bmp] [seconds]
 * Takes frames for a few seconds (default 5), prints how many arrived and saves the last one. */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#define CHECK(what) do { hr = (what); if (FAILED(hr)) { printf("FAILED %08lx: %s\n", hr, #what); return 1; } } while (0)

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "duptest.bmp";
    ULONGLONG end = GetTickCount64() + (argc > 2 ? atoi(argv[2]) : 5) * 1000;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    IDXGIOutputDuplication *dup;
    D3D11_TEXTURE2D_DESC tex_desc;
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Texture2D *texture, *staging = NULL;
    ID3D11DeviceContext *context;
    DXGI_OUTDUPL_FRAME_INFO info;
    DXGI_OUTPUT_DESC out_desc;
    DXGI_OUTDUPL_DESC desc;
    IDXGIResource *resource;
    IDXGIFactory1 *factory;
    IDXGIAdapter *adapter;
    IDXGIOutput1 *output1;
    IDXGIOutput *output;
    ID3D11Device *device;
    unsigned frames = 0, timeouts = 0, y;
    HRESULT hr;

    CHECK(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory));
    CHECK(IDXGIFactory1_EnumAdapters(factory, 0, &adapter));
    CHECK(IDXGIAdapter_EnumOutputs(adapter, 0, &output));
    IDXGIOutput_GetDesc(output, &out_desc);
    printf("output %ls: %ldx%ld\n", out_desc.DeviceName, out_desc.DesktopCoordinates.right - out_desc.DesktopCoordinates.left,
           out_desc.DesktopCoordinates.bottom - out_desc.DesktopCoordinates.top);
    CHECK(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, &level, 1, D3D11_SDK_VERSION, &device, NULL, &context));
    CHECK(IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput1, (void **)&output1));
    CHECK(IDXGIOutput1_DuplicateOutput(output1, (IUnknown *)device, &dup));
    IDXGIOutputDuplication_GetDesc(dup, &desc);
    printf("duplication: %ux%u, format %u\n", desc.ModeDesc.Width, desc.ModeDesc.Height, desc.ModeDesc.Format);

    while (GetTickCount64() < end)
    {
        hr = IDXGIOutputDuplication_AcquireNextFrame(dup, 500, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) { timeouts++; continue; }
        CHECK(hr);
        CHECK(IDXGIResource_QueryInterface(resource, &IID_ID3D11Texture2D, (void **)&texture));
        if (!staging)
        {
            ID3D11Texture2D_GetDesc(texture, &tex_desc);
            tex_desc.Usage = D3D11_USAGE_STAGING;
            tex_desc.BindFlags = 0;
            tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            CHECK(ID3D11Device_CreateTexture2D(device, &tex_desc, NULL, &staging));
        }
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
        ID3D11Texture2D_Release(texture);
        IDXGIResource_Release(resource);
        CHECK(IDXGIOutputDuplication_ReleaseFrame(dup));
        frames++;
    }
    printf("%u frames, %u waits without a new frame\n", frames, timeouts);
    if (!frames) return 2;

    CHECK(ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map));
    {
        BITMAPFILEHEADER file = {0};
        BITMAPINFOHEADER bmp = {0};
        FILE *out = fopen(path, "wb");
        if (!out) { printf("cannot write %s\n", path); return 1; }
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(bmp);
        file.bfSize = file.bfOffBits + tex_desc.Width * tex_desc.Height * 4;
        bmp.biSize = sizeof(bmp);
        bmp.biWidth = tex_desc.Width;
        bmp.biHeight = -(LONG)tex_desc.Height;
        bmp.biPlanes = 1;
        bmp.biBitCount = 32;
        fwrite(&file, sizeof(file), 1, out);
        fwrite(&bmp, sizeof(bmp), 1, out);
        for (y = 0; y < tex_desc.Height; y++) fwrite((BYTE *)map.pData + y * map.RowPitch, 4, tex_desc.Width, out);
        fclose(out);
    }
    printf("last frame saved to %s\n", path);
    return 0;
}
