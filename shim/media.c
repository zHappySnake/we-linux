/*
 * Wallpaper Engine's video wallpapers, as played through Proton's Media Foundation.
 *
 * Wallpaper Engine plays video with the Media Foundation media engine, created through
 * CoCreateInstance(CLSID_MFMediaEngineClassFactory). We wrap the factory and the engine's
 * vtable.
 *
 * Colors: Wallpaper Engine creates the engine with VIDEO_OUTPUT_FORMAT = B8G8R8A8 but then
 * transfers frames into R8G8B8A8 textures. Windows converts to the target texture's format in
 * TransferVideoFrame; Proton copies the bytes as they are, swapping red and blue. We declare
 * the output format Wallpaper Engine actually renders into, which is correct either way.
 */
#define INITGUID
#define COBJMACROS
#include "shim.h"
#include <d3d11.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <stdio.h>

/* What Wallpaper Engine renders video frames into (seen in TransferVideoFrame). */
#define WE_VIDEO_TEXTURE_FORMAT DXGI_FORMAT_R8G8B8A8_UNORM

static UINT32 output_format;

static HRESULT (STDMETHODCALLTYPE *real_create_instance)(IMFMediaEngineClassFactory *, DWORD, IMFAttributes *, IMFMediaEngine **);
static HRESULT (STDMETHODCALLTYPE *real_set_playback_rate)(IMFMediaEngine *, double);
static HRESULT (STDMETHODCALLTYPE *real_set_default_playback_rate)(IMFMediaEngine *, double);
static HRESULT (STDMETHODCALLTYPE *real_transfer_video_frame)(IMFMediaEngine *, IUnknown *, const MFVideoNormalizedRect *,
                                                              const RECT *, const MFARGB *);

static void patch_slot(void **slot, void *fn, void **original)
{
    if (*slot == fn) return;
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    *original = *slot;
    *slot = fn;
    VirtualProtect(slot, sizeof(*slot), old, &old);
}

static const char *guid_str(REFGUID g)
{
    static char ring[4][40];
    static LONG next;
    char *out = ring[InterlockedIncrement(&next) & 3];
    snprintf(out, 40, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g->Data1, g->Data2, g->Data3,
             g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    return out;
}

static const char *attr_name(REFGUID key)
{
    if (IsEqualGUID(key, &MF_MEDIA_ENGINE_DXGI_MANAGER)) return "DXGI_MANAGER";
    if (IsEqualGUID(key, &MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT)) return "VIDEO_OUTPUT_FORMAT";
    if (IsEqualGUID(key, &MF_MEDIA_ENGINE_CALLBACK)) return "CALLBACK";
    if (IsEqualGUID(key, &MF_MEDIA_ENGINE_PLAYBACK_HWND)) return "PLAYBACK_HWND";
    if (IsEqualGUID(key, &MF_MEDIA_ENGINE_AUDIO_CATEGORY)) return "AUDIO_CATEGORY";
    if (IsEqualGUID(key, &MF_MEDIA_ENGINE_CONTENT_PROTECTION_FLAGS)) return "CONTENT_PROTECTION_FLAGS";
    return guid_str(key);
}

static void log_attributes(IMFAttributes *attr)
{
    UINT32 count = 0;
    if (!attr || FAILED(IMFAttributes_GetCount(attr, &count))) return;
    for (UINT32 i = 0; i < count; i++) {
        GUID key;
        PROPVARIANT value;
        PropVariantInit(&value);
        if (FAILED(IMFAttributes_GetItemByIndex(attr, i, &key, &value))) continue;
        switch (value.vt) {
        case VT_UI4: shim_log("  %s = %lu", attr_name(&key), value.ulVal); break;
        case VT_UI8: shim_log("  %s = %llu", attr_name(&key), value.uhVal.QuadPart); break;
        case VT_CLSID: shim_log("  %s = %s", attr_name(&key), guid_str(value.puuid)); break;
        case VT_UNKNOWN: shim_log("  %s = <object %p>", attr_name(&key), (void *)value.punkVal); break;
        default: shim_log("  %s = <vt %u>", attr_name(&key), value.vt); break;
        }
        PropVariantClear(&value);
    }
}

/* ---- media engine ------------------------------------------------------- */

static HRESULT STDMETHODCALLTYPE hook_set_playback_rate(IMFMediaEngine *engine, double rate)
{
    HRESULT hr = real_set_playback_rate(engine, rate);
    shim_log("media engine %p: SetPlaybackRate(%.3f) -> 0x%08lx", (void *)engine, rate, hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_set_default_playback_rate(IMFMediaEngine *engine, double rate)
{
    HRESULT hr = real_set_default_playback_rate(engine, rate);
    shim_log("media engine %p: SetDefaultPlaybackRate(%.3f) -> 0x%08lx", (void *)engine, rate, hr);
    return hr;
}

static LONG transfer_count;

static HRESULT STDMETHODCALLTYPE hook_transfer_video_frame(IMFMediaEngine *engine, IUnknown *surface,
                                                           const MFVideoNormalizedRect *src, const RECT *dst,
                                                           const MFARGB *border)
{
    HRESULT hr = real_transfer_video_frame(engine, surface, src, dst, border);
    LONG n = InterlockedIncrement(&transfer_count);
    if (n <= 3 || n % 1800 == 0) {
        ID3D11Texture2D *tex;
        D3D11_TEXTURE2D_DESC desc = { 0 };
        if (SUCCEEDED(IUnknown_QueryInterface(surface, &IID_ID3D11Texture2D, (void **)&tex))) {
            ID3D11Texture2D_GetDesc(tex, &desc);
            ID3D11Texture2D_Release(tex);
        }
        shim_log("media engine %p: TransferVideoFrame #%ld into %ux%u DXGI format %u, bind 0x%x -> 0x%08lx",
                 (void *)engine, n, desc.Width, desc.Height, desc.Format, desc.BindFlags, hr);
        if (desc.Format && output_format && desc.Format != output_format)
            shim_log("  warning: frames are decoded as DXGI format %u but drawn into format %u; colors may be off",
                     output_format, desc.Format);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_create_instance(IMFMediaEngineClassFactory *factory, DWORD flags,
                                                      IMFAttributes *attr, IMFMediaEngine **engine)
{
    shim_log("media engine CreateInstance(flags 0x%lx)", flags);
    log_attributes(attr);

    /* Work on a copy so Wallpaper Engine's own attribute store stays as it set it. */
    IMFAttributes *patched = NULL;
    UINT32 count = 0, format = 0;
    if (attr && SUCCEEDED(IMFAttributes_GetUINT32(attr, &MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, &format)) &&
        format == DXGI_FORMAT_B8G8R8A8_UNORM && SUCCEEDED(IMFAttributes_GetCount(attr, &count)) &&
        SUCCEEDED(MFCreateAttributes(&patched, count)) && SUCCEEDED(IMFAttributes_CopyAllItems(attr, patched)) &&
        SUCCEEDED(IMFAttributes_SetUINT32(patched, &MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, WE_VIDEO_TEXTURE_FORMAT))) {
        shim_log("  output format %u -> %u to match the textures frames are drawn into", format, WE_VIDEO_TEXTURE_FORMAT);
        format = WE_VIDEO_TEXTURE_FORMAT;
    }
    HRESULT hr = real_create_instance(factory, flags, patched ? patched : attr, engine);
    if (patched) IMFAttributes_Release(patched);
    if (SUCCEEDED(hr)) output_format = format;
    shim_log("  -> 0x%08lx engine %p", hr, SUCCEEDED(hr) ? (void *)*engine : NULL);
    if (SUCCEEDED(hr)) {
        IMFMediaEngineVtbl *vtbl = (*engine)->lpVtbl;
        patch_slot((void **)&vtbl->SetPlaybackRate, hook_set_playback_rate, (void **)&real_set_playback_rate);
        patch_slot((void **)&vtbl->SetDefaultPlaybackRate, hook_set_default_playback_rate,
                   (void **)&real_set_default_playback_rate);
        patch_slot((void **)&vtbl->TransferVideoFrame, hook_transfer_video_frame, (void **)&real_transfer_video_frame);
    }
    return hr;
}

/* ---- entry point -------------------------------------------------------- */

static HRESULT WINAPI hook_CoCreateInstance(REFCLSID clsid, IUnknown *outer, DWORD ctx, REFIID iid, void **out)
{
    HRESULT hr = CoCreateInstance(clsid, outer, ctx, iid, out);
    if (SUCCEEDED(hr) && IsEqualCLSID(clsid, &CLSID_MFMediaEngineClassFactory)) {
        IMFMediaEngineClassFactory *factory;
        if (SUCCEEDED(IUnknown_QueryInterface((IUnknown *)*out, &IID_IMFMediaEngineClassFactory, (void **)&factory))) {
            patch_slot((void **)&factory->lpVtbl->CreateInstance, hook_create_instance, (void **)&real_create_instance);
            IMFMediaEngineClassFactory_Release(factory);
        }
    }
    return hr;
}

const struct hook media_hooks[] = {
    { "ole32.dll", "CoCreateInstance", hook_CoCreateInstance },
};
const size_t media_hook_count = sizeof(media_hooks) / sizeof(media_hooks[0]);
