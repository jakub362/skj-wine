/*
 * mfaacenc.dll - AAC encoder Media Foundation transform (CLSID_AACMFTEncoder) for Wine.
 *
 * Windows ships this encoder ("Microsoft AAC Audio Encoder MFT"); Wine and Proton don't, so
 * recorders that mux AAC audio (SteelSeries GG Moments, anything using FFmpeg's aac_mf) fail
 * with REGDB_E_CLASSNOTREG. Input: 16-bit PCM, 1 or 2 channels. Output: raw AAC-LC frames,
 * 1024 samples each. The encoding itself is done by fdk-aac (the patent-free "stripped" build).
 *
 * Copyright 2026 SKJ Wine. LGPL-2.1-or-later for this file; see fdk-aac's NOTICE for the codec.
 */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include "aacenc_lib.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

DEFINE_GUID(CLSID_AACEncoder, 0x93af0c51, 0x2275, 0x45d2, 0xa3, 0x5b, 0xf2, 0xba, 0x21, 0xca, 0xed, 0x00);

#define FRAME 1024              /* samples per channel in one AAC-LC frame */
static const UINT rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000};
static const UINT byte_rates[] = {12000, 16000, 20000, 24000};

static HINSTANCE instance;

/* set SKJ_MFAACENC_LOG to a file name to see what the program asks of the encoder */
static void trace(const char *format, ...)
{
    static char path[MAX_PATH];
    static LONG state;
    va_list args;
    FILE *file;

    if (!state) state = GetEnvironmentVariableA("SKJ_MFAACENC_LOG", path, sizeof(path)) ? 1 : -1;
    if (state < 0 || !(file = fopen(path, "a"))) return;
    va_start(args, format);
    fprintf(file, "%04lx: ", GetCurrentThreadId());
    vfprintf(file, format, args);
    fputc('\n', file);
    va_end(args);
    fclose(file);
}

struct encoder
{
    IMFTransform IMFTransform_iface;
    LONG ref;
    CRITICAL_SECTION cs;
    IMFMediaType *input_type, *output_type;
    HANDLE_AACENCODER enc;
    UINT channels, rate, byte_rate;
    INT16 *pcm;                 /* samples waiting to be encoded */
    UINT pcm_frames, pcm_capacity;
    LONGLONG next_time;         /* time stamp of the first waiting sample */
    BOOL have_time, draining;
};

static struct encoder *impl(IMFTransform *iface)
{
    return CONTAINING_RECORD(iface, struct encoder, IMFTransform_iface);
}

static BOOL valid_rate(UINT rate)
{
    UINT i;
    for (i = 0; i < ARRAY_SIZE(rates); i++) if (rates[i] == rate) return TRUE;
    return FALSE;
}

/* MF_MT_USER_DATA of an AAC type: the tail of HEAACWAVEINFO followed by the AudioSpecificConfig */
static void user_data(UINT rate, UINT channels, BYTE data[14])
{
    UINT index = 0;
    while (index < ARRAY_SIZE(rates) && rates[index] != rate) index++;
    memset(data, 0, 14);
    data[2] = 0x29;                                     /* wAudioProfileLevelIndication: AAC-LC L2 */
    data[12] = (2 << 3) | (index >> 1);                 /* object type 2 (AAC-LC), frequency index */
    data[13] = ((index & 1) << 7) | (channels << 3);    /* channel configuration */
}

static HRESULT make_output_type(UINT rate, UINT channels, UINT byte_rate, IMFMediaType **out)
{
    IMFMediaType *type;
    BYTE data[14];
    HRESULT hr;

    if (FAILED(hr = MFCreateMediaType(&type))) return hr;
    user_data(rate, channels, data);
    IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, byte_rate);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
    IMFMediaType_SetUINT32(type, &MF_MT_AAC_PAYLOAD_TYPE, 0);
    IMFMediaType_SetUINT32(type, &MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_PREFER_WAVEFORMATEX, 1);
    IMFMediaType_SetBlob(type, &MF_MT_USER_DATA, data, sizeof(data));
    *out = type;
    return S_OK;
}

static HRESULT make_input_type(UINT rate, UINT channels, IMFMediaType **out)
{
    IMFMediaType *type;
    HRESULT hr;

    if (FAILED(hr = MFCreateMediaType(&type))) return hr;
    IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2);
    IMFMediaType_SetUINT32(type, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
    *out = type;
    return S_OK;
}

static void close_codec(struct encoder *This)
{
    if (This->enc) aacEncClose(&This->enc);
    This->enc = NULL;
    This->pcm_frames = 0;
    This->have_time = This->draining = FALSE;
}

/* both types are known: start fdk-aac */
static HRESULT open_codec(struct encoder *This)
{
    close_codec(This);
    if (!This->input_type || !This->output_type) return S_OK;
    if (aacEncOpen(&This->enc, 0, This->channels) != AACENC_OK) return E_FAIL;
    if (aacEncoder_SetParam(This->enc, AACENC_AOT, 2) != AACENC_OK ||
        aacEncoder_SetParam(This->enc, AACENC_SAMPLERATE, This->rate) != AACENC_OK ||
        aacEncoder_SetParam(This->enc, AACENC_CHANNELMODE, This->channels == 1 ? MODE_1 : MODE_2) != AACENC_OK ||
        aacEncoder_SetParam(This->enc, AACENC_CHANNELORDER, 1) != AACENC_OK ||
        aacEncoder_SetParam(This->enc, AACENC_BITRATE, This->byte_rate * 8) != AACENC_OK ||
        aacEncoder_SetParam(This->enc, AACENC_TRANSMUX, TT_MP4_RAW) != AACENC_OK ||
        aacEncEncode(This->enc, NULL, NULL, NULL, NULL) != AACENC_OK)
    {
        close_codec(This);
        return MF_E_INVALIDMEDIATYPE;
    }
    return S_OK;
}

static HRESULT WINAPI encoder_QueryInterface(IMFTransform *iface, REFIID iid, void **out)
{
    trace("QueryInterface %08lx-%04x", iid->Data1, iid->Data2);
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IMFTransform))
    {
        IMFTransform_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI encoder_AddRef(IMFTransform *iface)
{
    return InterlockedIncrement(&impl(iface)->ref);
}

static ULONG WINAPI encoder_Release(IMFTransform *iface)
{
    struct encoder *This = impl(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    if (!ref)
    {
        close_codec(This);
        if (This->input_type) IMFMediaType_Release(This->input_type);
        if (This->output_type) IMFMediaType_Release(This->output_type);
        DeleteCriticalSection(&This->cs);
        free(This->pcm);
        free(This);
    }
    return ref;
}

static HRESULT WINAPI encoder_GetStreamLimits(IMFTransform *iface, DWORD *in_min, DWORD *in_max, DWORD *out_min, DWORD *out_max)
{
    trace("GetStreamLimits");
    *in_min = *in_max = *out_min = *out_max = 1;
    return S_OK;
}

static HRESULT WINAPI encoder_GetStreamCount(IMFTransform *iface, DWORD *inputs, DWORD *outputs)
{
    trace("GetStreamCount");
    *inputs = *outputs = 1;
    return S_OK;
}

static HRESULT WINAPI encoder_GetStreamIDs(IMFTransform *iface, DWORD in_size, DWORD *in, DWORD out_size, DWORD *out)
{
    trace("GetStreamIDs");
    return E_NOTIMPL;
}

static HRESULT WINAPI encoder_GetInputStreamInfo(IMFTransform *iface, DWORD id, MFT_INPUT_STREAM_INFO *info)
{
    struct encoder *This = impl(iface);

    trace("GetInputStreamInfo");
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    memset(info, 0, sizeof(*info));
    info->cbSize = (This->channels ? This->channels : 2) * 2;
    return S_OK;
}

static HRESULT WINAPI encoder_GetOutputStreamInfo(IMFTransform *iface, DWORD id, MFT_OUTPUT_STREAM_INFO *info)
{
    struct encoder *This = impl(iface);

    trace("GetOutputStreamInfo");
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    memset(info, 0, sizeof(*info));
    info->dwFlags = MFT_OUTPUT_STREAM_WHOLE_SAMPLES | MFT_OUTPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER;
    info->cbSize = 768 * (This->channels ? This->channels : 2);     /* largest AAC frame: 6144 bits per channel */
    return S_OK;
}

static HRESULT WINAPI encoder_GetAttributes(IMFTransform *iface, IMFAttributes **attributes)
{
    trace("GetAttributes");
    return E_NOTIMPL;
}

static HRESULT WINAPI encoder_GetInputStreamAttributes(IMFTransform *iface, DWORD id, IMFAttributes **attributes)
{
    trace("GetInputStreamAttributes");
    return E_NOTIMPL;
}

static HRESULT WINAPI encoder_GetOutputStreamAttributes(IMFTransform *iface, DWORD id, IMFAttributes **attributes)
{
    trace("GetOutputStreamAttributes");
    return E_NOTIMPL;
}

static HRESULT WINAPI encoder_DeleteInputStream(IMFTransform *iface, DWORD id)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI encoder_AddInputStreams(IMFTransform *iface, DWORD count, DWORD *ids)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI encoder_GetInputAvailableType(IMFTransform *iface, DWORD id, DWORD index, IMFMediaType **type)
{
    struct encoder *This = impl(iface);

    trace("GetInputAvailableType %lu", index);
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    if (index) return MF_E_NO_MORE_TYPES;
    return make_input_type(This->rate ? This->rate : 48000, This->channels ? This->channels : 2, type);
}

static HRESULT WINAPI encoder_GetOutputAvailableType(IMFTransform *iface, DWORD id, DWORD index, IMFMediaType **type)
{
    struct encoder *This = impl(iface);

    trace("GetOutputAvailableType %lu", index);
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    if (index >= ARRAY_SIZE(byte_rates)) return MF_E_NO_MORE_TYPES;
    return make_output_type(This->rate ? This->rate : 48000, This->channels ? This->channels : 2, byte_rates[index], type);
}

static HRESULT audio_format(IMFMediaType *type, const GUID *wanted, UINT *rate, UINT *channels)
{
    GUID major, subtype;
    UINT32 bits = 16;

    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major)) || !IsEqualGUID(&major, &MFMediaType_Audio) ||
        FAILED(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype)) || !IsEqualGUID(&subtype, wanted) ||
        FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate)) || !valid_rate(*rate) ||
        FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, channels)) || !*channels || *channels > 2)
        return MF_E_INVALIDMEDIATYPE;
    IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    return bits == 16 ? S_OK : MF_E_INVALIDMEDIATYPE;
}

static HRESULT WINAPI encoder_SetInputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)
{
    struct encoder *This = impl(iface);
    UINT rate, channels;
    HRESULT hr = S_OK;

    trace("SetInputType %p flags %lx", type, flags);
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    EnterCriticalSection(&This->cs);
    if (!type)
    {
        if (This->input_type) IMFMediaType_Release(This->input_type);
        This->input_type = NULL;
        close_codec(This);
    }
    else if (FAILED(hr = audio_format(type, &MFAudioFormat_PCM, &rate, &channels))) ;
    else if (This->output_type && (rate != This->rate || channels != This->channels)) hr = MF_E_INVALIDMEDIATYPE;
    else if (!(flags & MFT_SET_TYPE_TEST_ONLY))
    {
        if (This->input_type) IMFMediaType_Release(This->input_type);
        if (SUCCEEDED(hr = make_input_type(rate, channels, &This->input_type)))
        {
            This->rate = rate;
            This->channels = channels;
            hr = open_codec(This);
        }
    }
    LeaveCriticalSection(&This->cs);
    trace("%s -> %08lx", __func__, hr);
    return hr;
}

static HRESULT WINAPI encoder_SetOutputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)
{
    struct encoder *This = impl(iface);
    UINT rate, channels;
    UINT32 byte_rate = 20000;
    HRESULT hr = S_OK;

    trace("SetOutputType %p flags %lx", type, flags);
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    EnterCriticalSection(&This->cs);
    if (!type)
    {
        if (This->output_type) IMFMediaType_Release(This->output_type);
        This->output_type = NULL;
        close_codec(This);
    }
    else if (FAILED(hr = audio_format(type, &MFAudioFormat_AAC, &rate, &channels))) ;
    else if (This->input_type && (rate != This->rate || channels != This->channels)) hr = MF_E_INVALIDMEDIATYPE;
    else if (!(flags & MFT_SET_TYPE_TEST_ONLY))
    {
        IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &byte_rate);
        if (byte_rate < 4000 || byte_rate > 64000) byte_rate = 20000;
        if (This->output_type) IMFMediaType_Release(This->output_type);
        if (SUCCEEDED(hr = make_output_type(rate, channels, byte_rate, &This->output_type)))
        {
            /* Windows' encoder completes the caller's type: programs (GG's recorder) read the
             * AudioSpecificConfig back from the very object they passed in */
            BYTE data[14];
            UINT32 value;
            user_data(rate, channels, data);
            IMFMediaType_SetBlob(type, &MF_MT_USER_DATA, data, sizeof(data));
            if (FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AAC_PAYLOAD_TYPE, &value)))
                IMFMediaType_SetUINT32(type, &MF_MT_AAC_PAYLOAD_TYPE, 0);
            if (FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, &value)))
                IMFMediaType_SetUINT32(type, &MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
            This->rate = rate;
            This->channels = channels;
            This->byte_rate = byte_rate;
            hr = open_codec(This);
        }
    }
    LeaveCriticalSection(&This->cs);
    trace("%s -> %08lx", __func__, hr);
    return hr;
}

static HRESULT current_type(struct encoder *This, IMFMediaType *current, IMFMediaType **out)
{
    HRESULT hr = MF_E_TRANSFORM_TYPE_NOT_SET;

    EnterCriticalSection(&This->cs);
    if (current && SUCCEEDED(hr = MFCreateMediaType(out)))
        hr = IMFMediaType_CopyAllItems(current, (IMFAttributes *)*out);
    LeaveCriticalSection(&This->cs);
    trace("%s -> %08lx", __func__, hr);
    return hr;
}

static HRESULT WINAPI encoder_GetInputCurrentType(IMFTransform *iface, DWORD id, IMFMediaType **type)
{
    trace("GetInputCurrentType");
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    return current_type(impl(iface), impl(iface)->input_type, type);
}

static HRESULT WINAPI encoder_GetOutputCurrentType(IMFTransform *iface, DWORD id, IMFMediaType **type)
{
    trace("GetOutputCurrentType");
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    return current_type(impl(iface), impl(iface)->output_type, type);
}

static HRESULT WINAPI encoder_GetInputStatus(IMFTransform *iface, DWORD id, DWORD *flags)
{
    struct encoder *This = impl(iface);

    trace("GetInputStatus");
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    *flags = This->enc && This->pcm_frames < FRAME ? MFT_INPUT_STATUS_ACCEPT_DATA : 0;
    return S_OK;
}

static HRESULT WINAPI encoder_GetOutputStatus(IMFTransform *iface, DWORD *flags)
{
    struct encoder *This = impl(iface);

    trace("GetOutputStatus");
    *flags = This->enc && This->pcm_frames >= FRAME ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
    return S_OK;
}

static HRESULT WINAPI encoder_SetOutputBounds(IMFTransform *iface, LONGLONG lower, LONGLONG upper)
{
    trace("SetOutputBounds");
    return E_NOTIMPL;
}

static HRESULT WINAPI encoder_ProcessEvent(IMFTransform *iface, DWORD id, IMFMediaEvent *event)
{
    trace("ProcessEvent");
    return E_NOTIMPL;
}

static HRESULT WINAPI encoder_ProcessMessage(IMFTransform *iface, MFT_MESSAGE_TYPE message, ULONG_PTR param)
{
    struct encoder *This = impl(iface);

    trace("ProcessMessage %x", message);
    EnterCriticalSection(&This->cs);
    if (message == MFT_MESSAGE_COMMAND_FLUSH)
    {
        This->pcm_frames = 0;
        This->have_time = This->draining = FALSE;
    }
    else if (message == MFT_MESSAGE_COMMAND_DRAIN) This->draining = TRUE;
    LeaveCriticalSection(&This->cs);
    return S_OK;
}

static HRESULT WINAPI encoder_ProcessInput(IMFTransform *iface, DWORD id, IMFSample *sample, DWORD flags)
{
    struct encoder *This = impl(iface);
    IMFMediaBuffer *buffer;
    LONGLONG time;
    DWORD len, frames;
    BYTE *data;
    HRESULT hr;

    trace("ProcessInput %p, %u waiting", sample, This->pcm_frames);
    if (id) return MF_E_INVALIDSTREAMNUMBER;
    if (!sample) return E_POINTER;
    EnterCriticalSection(&This->cs);
    if (!This->enc) hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else if (This->pcm_frames >= FRAME) hr = MF_E_NOTACCEPTING;     /* take the waiting frame out first */
    else if (SUCCEEDED(hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer)))
    {
        if (SUCCEEDED(hr = IMFMediaBuffer_Lock(buffer, &data, NULL, &len)))
        {
            frames = len / (This->channels * 2);
            if (This->pcm_frames + frames > This->pcm_capacity)
            {
                UINT capacity = This->pcm_frames + frames + FRAME;
                INT16 *pcm = realloc(This->pcm, capacity * This->channels * sizeof(INT16));
                if (pcm) { This->pcm = pcm; This->pcm_capacity = capacity; }
                else hr = E_OUTOFMEMORY;
            }
            if (SUCCEEDED(hr))
            {
                if (!This->have_time && SUCCEEDED(IMFSample_GetSampleTime(sample, &time)))
                {
                    This->next_time = time - (LONGLONG)This->pcm_frames * 10000000 / This->rate;
                    This->have_time = TRUE;
                }
                memcpy(This->pcm + This->pcm_frames * This->channels, data, frames * This->channels * 2);
                This->pcm_frames += frames;
                This->draining = FALSE;
            }
            IMFMediaBuffer_Unlock(buffer);
        }
        IMFMediaBuffer_Release(buffer);
    }
    LeaveCriticalSection(&This->cs);
    trace("%s -> %08lx", __func__, hr);
    return hr;
}

static HRESULT WINAPI encoder_ProcessOutput(IMFTransform *iface, DWORD flags, DWORD count,
        MFT_OUTPUT_DATA_BUFFER *samples, DWORD *status)
{
    struct encoder *This = impl(iface);
    AACENC_BufDesc in_desc = {0}, out_desc = {0};
    AACENC_InArgs in_args = {0};
    AACENC_OutArgs out_args = {0};
    int in_id = IN_AUDIO_DATA, out_id = OUT_BITSTREAM_DATA, in_size, in_el = 2, out_size, out_el = 1;
    IMFMediaBuffer *buffer;
    BYTE *data, frame[768 * 2];
    void *in_ptr, *out_ptr = frame;
    DWORD max_len;
    HRESULT hr = S_OK;

    trace("ProcessOutput count %lu sample %p, %u waiting", count, count ? samples[0].pSample : NULL, This->pcm_frames);
    if (count != 1) return E_INVALIDARG;
    *status = samples[0].dwStatus = 0;

    EnterCriticalSection(&This->cs);
    if (!This->enc) hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else for (;;)
    {
        if (This->pcm_frames < FRAME)
        {
            if (!This->draining || !This->pcm_frames)
            {
                This->draining = FALSE;
                hr = MF_E_TRANSFORM_NEED_MORE_INPUT;
                break;
            }
            /* end of stream: fill the last frame up with silence */
            memset(This->pcm + This->pcm_frames * This->channels, 0, (FRAME - This->pcm_frames) * This->channels * 2);
            This->pcm_frames = FRAME;
        }
        in_ptr = This->pcm;
        in_size = FRAME * This->channels * 2;
        out_size = sizeof(frame);
        in_desc.numBufs = out_desc.numBufs = 1;
        in_desc.bufs = &in_ptr; in_desc.bufferIdentifiers = &in_id; in_desc.bufSizes = &in_size; in_desc.bufElSizes = &in_el;
        out_desc.bufs = &out_ptr; out_desc.bufferIdentifiers = &out_id; out_desc.bufSizes = &out_size; out_desc.bufElSizes = &out_el;
        in_args.numInSamples = FRAME * This->channels;
        if (aacEncEncode(This->enc, &in_desc, &out_desc, &in_args, &out_args) != AACENC_OK)
        {
            hr = E_FAIL;
            break;
        }
        This->pcm_frames -= FRAME;
        memmove(This->pcm, This->pcm + FRAME * This->channels, This->pcm_frames * This->channels * 2);
        if (!out_args.numOutBytes)      /* the codec is still filling its look-ahead */
        {
            This->next_time += (LONGLONG)FRAME * 10000000 / This->rate;
            continue;
        }
        /* the caller normally brings the sample; programs that ask "anything ready?" with none get one of ours */
        if (!samples[0].pSample && SUCCEEDED(hr = MFCreateSample(&samples[0].pSample)))
        {
            if (SUCCEEDED(hr = MFCreateMemoryBuffer(sizeof(frame), &buffer)))
            {
                hr = IMFSample_AddBuffer(samples[0].pSample, buffer);
                IMFMediaBuffer_Release(buffer);
            }
            if (FAILED(hr)) { IMFSample_Release(samples[0].pSample); samples[0].pSample = NULL; }
        }
        if (SUCCEEDED(hr) && SUCCEEDED(hr = IMFSample_ConvertToContiguousBuffer(samples[0].pSample, &buffer)))
        {
            if (SUCCEEDED(hr = IMFMediaBuffer_Lock(buffer, &data, &max_len, NULL)))
            {
                if (max_len < (DWORD)out_args.numOutBytes) hr = MF_E_BUFFERTOOSMALL;
                else memcpy(data, frame, out_args.numOutBytes);
                IMFMediaBuffer_Unlock(buffer);
            }
            if (SUCCEEDED(hr)) hr = IMFMediaBuffer_SetCurrentLength(buffer, out_args.numOutBytes);
            IMFMediaBuffer_Release(buffer);
        }
        if (SUCCEEDED(hr))
        {
            IMFSample_SetSampleTime(samples[0].pSample, This->next_time);
            IMFSample_SetSampleDuration(samples[0].pSample, (LONGLONG)FRAME * 10000000 / This->rate);
            IMFSample_SetUINT32(samples[0].pSample, &MFSampleExtension_CleanPoint, 1);
        }
        This->next_time += (LONGLONG)FRAME * 10000000 / This->rate;
        break;
    }
    LeaveCriticalSection(&This->cs);
    trace("%s -> %08lx", __func__, hr);
    return hr;
}

static const IMFTransformVtbl encoder_vtbl =
{
    encoder_QueryInterface, encoder_AddRef, encoder_Release,
    encoder_GetStreamLimits, encoder_GetStreamCount, encoder_GetStreamIDs,
    encoder_GetInputStreamInfo, encoder_GetOutputStreamInfo,
    encoder_GetAttributes, encoder_GetInputStreamAttributes, encoder_GetOutputStreamAttributes,
    encoder_DeleteInputStream, encoder_AddInputStreams,
    encoder_GetInputAvailableType, encoder_GetOutputAvailableType,
    encoder_SetInputType, encoder_SetOutputType,
    encoder_GetInputCurrentType, encoder_GetOutputCurrentType,
    encoder_GetInputStatus, encoder_GetOutputStatus, encoder_SetOutputBounds,
    encoder_ProcessEvent, encoder_ProcessMessage, encoder_ProcessInput, encoder_ProcessOutput,
};

static HRESULT WINAPI factory_QueryInterface(IClassFactory *iface, REFIID iid, void **out)
{
    *out = IsEqualGUID(iid, &IID_IClassFactory) || IsEqualGUID(iid, &IID_IUnknown) ? iface : NULL;
    return *out ? S_OK : E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IClassFactory *iface) { return 2; }
static ULONG WINAPI factory_Release(IClassFactory *iface) { return 1; }

static HRESULT WINAPI factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    struct encoder *This;
    HRESULT hr;

    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(This = calloc(1, sizeof(*This)))) return E_OUTOFMEMORY;
    This->IMFTransform_iface.lpVtbl = (IMFTransformVtbl *)&encoder_vtbl;
    This->ref = 1;
    InitializeCriticalSection(&This->cs);
    hr = IMFTransform_QueryInterface(&This->IMFTransform_iface, iid, out);
    IMFTransform_Release(&This->IMFTransform_iface);
    return hr;
}

static HRESULT WINAPI factory_LockServer(IClassFactory *iface, BOOL lock) { return S_OK; }

static const IClassFactoryVtbl factory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release, factory_CreateInstance, factory_LockServer,
};
static IClassFactory factory = {(IClassFactoryVtbl *)&factory_vtbl};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    if (IsEqualGUID(clsid, &CLSID_AACEncoder)) return IClassFactory_QueryInterface(&factory, iid, out);
    *out = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

static const WCHAR class_key[] = L"Software\\Classes\\CLSID\\{93AF0C51-2275-45D2-A35B-F2BA21CAED00}";

HRESULT WINAPI DllRegisterServer(void)
{
    MFT_REGISTER_TYPE_INFO inputs[] = {{MFMediaType_Audio, MFAudioFormat_PCM}};
    MFT_REGISTER_TYPE_INFO outputs[] = {{MFMediaType_Audio, MFAudioFormat_AAC}};
    WCHAR path[MAX_PATH], key[128];
    HKEY hkey;

    if (!GetModuleFileNameW(instance, path, ARRAY_SIZE(path))) return E_FAIL;
    wsprintfW(key, L"%s\\InprocServer32", class_key);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL)) return E_ACCESSDENIED;
    RegSetValueExW(hkey, NULL, 0, REG_SZ, (BYTE *)path, (lstrlenW(path) + 1) * sizeof(WCHAR));
    RegSetValueExW(hkey, L"ThreadingModel", 0, REG_SZ, (BYTE *)L"Both", sizeof(L"Both"));
    RegCloseKey(hkey);
    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, class_key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL))
    {
        RegSetValueExW(hkey, NULL, 0, REG_SZ, (BYTE *)L"AAC Audio Encoder MFT (SKJ Wine)", sizeof(L"AAC Audio Encoder MFT (SKJ Wine)"));
        RegCloseKey(hkey);
    }
    return MFTRegister(CLSID_AACEncoder, MFT_CATEGORY_AUDIO_ENCODER, (WCHAR *)L"AAC Audio Encoder MFT",
            MFT_ENUM_FLAG_SYNCMFT, ARRAY_SIZE(inputs), inputs, ARRAY_SIZE(outputs), outputs, NULL);
}

HRESULT WINAPI DllUnregisterServer(void)
{
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, class_key);
    return MFTUnregister(CLSID_AACEncoder);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        instance = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
