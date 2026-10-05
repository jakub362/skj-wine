/* metertest.exe [seconds]: print the default render endpoint's peak meter and a loopback capture's own peak, 10x per second */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <stdio.h>
#include <math.h>

/* mingw only forward declares the interface */
typedef struct Meter Meter;
struct MeterVtbl {
    HRESULT (WINAPI *QueryInterface)(Meter *, REFIID, void **); ULONG (WINAPI *AddRef)(Meter *); ULONG (WINAPI *Release)(Meter *);
    HRESULT (WINAPI *GetPeakValue)(Meter *, float *); HRESULT (WINAPI *GetMeteringChannelCount)(Meter *, UINT *);
    HRESULT (WINAPI *GetChannelsPeakValues)(Meter *, UINT32, float *); HRESULT (WINAPI *QueryHardwareSupport)(Meter *, DWORD *);
};
struct Meter { const struct MeterVtbl *lpVtbl; };
DEFINE_GUID(IID_IAudioMeterInformation_, 0xc02216f6, 0x8c67, 0x4b5b, 0x9d, 0x00, 0xd0, 0x08, 0xe7, 0x3e, 0x00, 0x64);

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 5, i;
    IMMDeviceEnumerator *e; IMMDevice *dev; Meter *meter = NULL; IAudioClient *client; IAudioCaptureClient *cap;
    WAVEFORMATEX *fmt; HRESULT hr; UINT ch = 0;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&e);
    if (FAILED(hr)) { printf("enumerator %08lx\n", hr); return 1; }
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(e, eRender, eConsole, &dev);
    if (FAILED(hr)) { printf("endpoint %08lx\n", hr); return 1; }
    hr = IMMDevice_Activate(dev, &IID_IAudioMeterInformation_, CLSCTX_ALL, NULL, (void **)&meter);
    printf("Activate(IAudioMeterInformation) = %08lx\n", hr);
    if (meter) { hr = meter->lpVtbl->GetMeteringChannelCount(meter, &ch); printf("channels = %u (%08lx)\n", ch, hr); }

    hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    IAudioClient_GetMixFormat(client, &fmt);
    printf("mix format: tag %#x, %u ch, %lu Hz, %u bits\n", fmt->wFormatTag, fmt->nChannels, fmt->nSamplesPerSec, fmt->wBitsPerSample);
    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, NULL);
    printf("loopback Initialize = %08lx\n", hr);
    IAudioClient_GetService(client, &IID_IAudioCaptureClient, (void **)&cap);
    IAudioClient_Start(client);

    for (i = 0; i < secs * 10; i++)
    {
        float peaks[8] = {0}, own = 0, peak = -1; UINT32 frames, total = 0, n; DWORD flags; BYTE *data;
        Sleep(100);
        while (IAudioCaptureClient_GetBuffer(cap, &data, &frames, &flags, NULL, NULL) == S_OK)
        {
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && fmt->wBitsPerSample == 32)
                for (n = 0; n < frames * fmt->nChannels; n++) { float v = fabsf(((float *)data)[n]); if (v > own) own = v; }
            total += frames;
            IAudioCaptureClient_ReleaseBuffer(cap, frames);
        }
        hr = S_OK;
        if (meter) { meter->lpVtbl->GetPeakValue(meter, &peak); hr = meter->lpVtbl->GetChannelsPeakValues(meter, ch, peaks); }
        printf("%4.1fs  loopback frames=%-5u own peak=%.4f | meter peak=%.4f L=%.4f R=%.4f (%08lx)\n", i / 10.0, total, own, peak, peaks[0], peaks[1], hr);
        fflush(stdout);
    }
    return 0;
}
