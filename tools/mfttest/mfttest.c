/* mfttest.exe: can this Wine create the Media Foundation encoders that recorders need?
 * Prints the result of CoCreateInstance for the AAC and H.264 encoder MFTs and the MP4 sink. */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <stdio.h>

DEFINE_GUID(CLSID_AACEnc,  0x93af0c51, 0x2275, 0x45d2, 0xa3, 0x5b, 0xf2, 0xba, 0x21, 0xca, 0xed, 0x00);
DEFINE_GUID(CLSID_H264Enc, 0x6ca50344, 0x051a, 0x4ded, 0x97, 0x79, 0xa4, 0x33, 0x05, 0x16, 0x5e, 0x35);
DEFINE_GUID(CLSID_AACDec,  0x32d186a7, 0x218f, 0x4c75, 0x88, 0x76, 0xdd, 0x77, 0x27, 0x3a, 0x89, 0x99);
DEFINE_GUID(IID_IMFTransform_, 0xbf94c121, 0x5b05, 0x4e6f, 0x80, 0x00, 0xba, 0x59, 0x89, 0x61, 0x41, 0x4d);

static void try(const char *name, const GUID *clsid)
{
    IUnknown *obj = NULL;
    HRESULT hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform_, (void **)&obj);
    printf("%-22s %s (%08lx)\n", name, SUCCEEDED(hr) ? "available" : "MISSING", hr);
    if (obj) IUnknown_Release(obj);
}

int main(void)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    try("AAC encoder MFT", &CLSID_AACEnc);
    try("H.264 encoder MFT", &CLSID_H264Enc);
    try("AAC decoder MFT", &CLSID_AACDec);
    return 0;
}
