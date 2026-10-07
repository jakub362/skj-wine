/* mfttest.exe: can this Wine create the Media Foundation encoders that recorders need?
 * Prints the result of CoCreateInstance for the AAC and H.264 encoder MFTs and the AAC decoder.
 *   mfttest.exe            just the list
 *   mfttest.exe out.aac    also encode 2 s of a 440 Hz tone with the AAC encoder into out.aac
 *                          (ADTS, plays anywhere) */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <math.h>
#include <stdio.h>

DEFINE_GUID(CLSID_AACEnc,  0x93af0c51, 0x2275, 0x45d2, 0xa3, 0x5b, 0xf2, 0xba, 0x21, 0xca, 0xed, 0x00);
DEFINE_GUID(CLSID_H264Enc, 0x6ca50344, 0x051a, 0x4ded, 0x97, 0x79, 0xa4, 0x33, 0x05, 0x16, 0x5e, 0x35);
DEFINE_GUID(CLSID_AACDec,  0x32d186a7, 0x218f, 0x4c75, 0x88, 0x76, 0xdd, 0x77, 0x27, 0x3a, 0x89, 0x99);

static void try(const char *name, const GUID *clsid)
{
    IUnknown *obj = NULL;
    HRESULT hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform, (void **)&obj);
    printf("%-22s %s (%08lx)\n", name, SUCCEEDED(hr) ? "available" : "MISSING", hr);
    if (obj) IUnknown_Release(obj);
}

#define CHECK(what) do { hr = (what); if (FAILED(hr)) { printf("FAILED %08lx: %s\n", hr, #what); return 1; } } while (0)

static IMFSample *new_sample(DWORD size)
{
    IMFMediaBuffer *buffer;
    IMFSample *sample;
    MFCreateSample(&sample);
    MFCreateMemoryBuffer(size, &buffer);
    IMFSample_AddBuffer(sample, buffer);
    IMFMediaBuffer_Release(buffer);
    return sample;
}

/* take every finished frame out of the encoder, write it with an ADTS header */
static int pull(IMFTransform *enc, FILE *out, DWORD size, unsigned *frames, unsigned *bytes)
{
    for (;;)
    {
        MFT_OUTPUT_DATA_BUFFER data = {0};
        IMFMediaBuffer *buffer;
        DWORD status, len, full;
        BYTE *ptr, adts[7];
        HRESULT hr;

        data.pSample = new_sample(size);
        hr = IMFTransform_ProcessOutput(enc, 0, 1, &data, &status);
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) { IMFSample_Release(data.pSample); return 0; }
        CHECK(hr);
        IMFSample_ConvertToContiguousBuffer(data.pSample, &buffer);
        IMFMediaBuffer_Lock(buffer, &ptr, NULL, &len);
        full = len + 7;                       /* AAC-LC, 48000 Hz (index 3), 2 channels */
        adts[0] = 0xff; adts[1] = 0xf1; adts[2] = (1 << 6) | (3 << 2); adts[3] = (2 << 6) | (full >> 11);
        adts[4] = full >> 3; adts[5] = (full << 5) | 0x1f; adts[6] = 0xfc;
        fwrite(adts, 1, 7, out); fwrite(ptr, 1, len, out);
        IMFMediaBuffer_Unlock(buffer); IMFMediaBuffer_Release(buffer); IMFSample_Release(data.pSample);
        ++*frames; *bytes += len;
    }
}

static int encode(const char *path)
{
    MFT_OUTPUT_STREAM_INFO info;
    IMFMediaType *type;
    IMFTransform *enc;
    unsigned frames = 0, bytes = 0, pos = 0, i, n;
    UINT32 size = 0;
    BYTE blob[64];
    FILE *out;
    HRESULT hr;

    CHECK(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    CHECK(CoCreateInstance(&CLSID_AACEnc, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform, (void **)&enc));
    CHECK(MFCreateMediaType(&type));
    IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, 2);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 20000);
    CHECK(IMFTransform_SetOutputType(enc, 0, type, 0));
    IMFMediaType_Release(type);
    CHECK(IMFTransform_GetInputAvailableType(enc, 0, 0, &type));
    CHECK(IMFTransform_SetInputType(enc, 0, type, 0));
    IMFMediaType_Release(type);
    CHECK(IMFTransform_GetOutputCurrentType(enc, 0, &type));
    CHECK(IMFMediaType_GetBlob(type, &MF_MT_USER_DATA, blob, sizeof(blob), &size));
    printf("user data (%u bytes):", size);
    for (i = 0; i < size; i++) printf(" %02x", blob[i]);
    printf("\n");
    CHECK(IMFTransform_GetOutputStreamInfo(enc, 0, &info));
    CHECK(IMFTransform_ProcessMessage(enc, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
    if (!(out = fopen(path, "wb"))) { printf("cannot write %s\n", path); return 1; }

    while (pos < 96000)                       /* 2 s in pieces of 10 ms, like a capture client delivers */
    {
        IMFSample *sample = new_sample(480 * 4);
        IMFMediaBuffer *buffer;
        BYTE *ptr;
        short *pcm;

        IMFSample_GetBufferByIndex(sample, 0, &buffer);
        IMFMediaBuffer_Lock(buffer, &ptr, NULL, NULL);
        pcm = (short *)ptr;
        for (n = 0; n < 480; n++, pos++) pcm[2 * n] = pcm[2 * n + 1] = (short)(8000 * sin(pos * 2 * 3.14159265358979 * 440 / 48000));
        IMFMediaBuffer_Unlock(buffer);
        IMFMediaBuffer_SetCurrentLength(buffer, 480 * 4);
        IMFMediaBuffer_Release(buffer);
        IMFSample_SetSampleTime(sample, (LONGLONG)(pos - 480) * 10000000 / 48000);
        hr = IMFTransform_ProcessInput(enc, 0, sample, 0);
        if (hr == MF_E_NOTACCEPTING) { pos -= 480; }
        else CHECK(hr);
        IMFSample_Release(sample);
        if (pull(enc, out, info.cbSize, &frames, &bytes)) return 1;
    }
    CHECK(IMFTransform_ProcessMessage(enc, MFT_MESSAGE_COMMAND_DRAIN, 0));
    if (pull(enc, out, info.cbSize, &frames, &bytes)) return 1;
    fclose(out);
    printf("encoded %u frames, %u bytes (%u bit/s) -> %s\n", frames, bytes, (unsigned)((unsigned long long)bytes * 8 * 48000 / (frames * 1024)), path);
    return 0;
}

int main(int argc, char **argv)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    try("AAC encoder MFT", &CLSID_AACEnc);
    try("H.264 encoder MFT", &CLSID_H264Enc);
    try("AAC decoder MFT", &CLSID_AACDec);
    return argc > 1 ? encode(argv[1]) : 0;
}
