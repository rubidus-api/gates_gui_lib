/* gates_gui_lib - the Windows image decoder: Windows Imaging Component turns
 * PNG, JPEG, BMP, GIF, ICO and TIFF into RGBA8 for the tree's image store
 * (plan-0020). WIC is reached through COM (the app initialised it); the GUIDs
 * are defined here so the link line needs no extra library. */
#define COBJMACROS
#include "gates_win32_internal.h"

#include <wincodec.h>

static const GUID gates_clsid_wic_factory = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID gates_iid_wic_factory = { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
static const GUID gates_wic_rgba32 = { 0xf5c7ad2d, 0x6a8d, 0x43dd, { 0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9 } };

static gates_err_t from_hr(HRESULT hr) {
    if (hr == E_OUTOFMEMORY) return PROVEN_ERR_NOMEM;
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
        return PROVEN_ERR_NOT_FOUND;
    }
    return PROVEN_ERR_INVALID_ARG; /* not an image WIC can read */
}

static gates_err_t decode(void *ctx, gates_str_t src, bool is_file, gates_allocator_t alloc, gates_u8 **out_rgba,
                          gates_i32 *out_w, gates_i32 *out_h) {
    (void)ctx;
    *out_rgba = nullptr;
    IWICImagingFactory *f = nullptr;
    IWICBitmapDecoder *dec = nullptr;
    IWICStream *stream = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *conv = nullptr;
    wchar_t *wpath = nullptr;
    HRESULT hr = CoCreateInstance(&gates_clsid_wic_factory, nullptr, CLSCTX_INPROC_SERVER, &gates_iid_wic_factory,
                                  (void **)&f);
    if (SUCCEEDED(hr) && is_file) {
        int n = MultiByteToWideChar(CP_UTF8, 0, (const char *)src.ptr, (int)src.size, nullptr, 0);
        wpath = n > 0 ? (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(n + 1) * sizeof(wchar_t)) : nullptr;
        if (wpath == nullptr) {
            hr = E_INVALIDARG;
        } else {
            MultiByteToWideChar(CP_UTF8, 0, (const char *)src.ptr, (int)src.size, wpath, n);
            wpath[n] = L'\0';
            hr = IWICImagingFactory_CreateDecoderFromFilename(f, wpath, nullptr, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &dec);
        }
    } else if (SUCCEEDED(hr)) {
        hr = IWICImagingFactory_CreateStream(f, &stream);
        if (SUCCEEDED(hr)) hr = IWICStream_InitializeFromMemory(stream, (BYTE *)src.ptr, (DWORD)src.size);
        if (SUCCEEDED(hr)) {
            hr = IWICImagingFactory_CreateDecoderFromStream(f, (IStream *)stream, nullptr,
                                                            WICDecodeMetadataCacheOnDemand, &dec);
        }
    }
    if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(dec, 0, &frame);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateFormatConverter(f, &conv);
    if (SUCCEEDED(hr)) {
        hr = IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)frame, &gates_wic_rgba32,
                                            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = IWICFormatConverter_GetSize(conv, &w, &h);
    if (SUCCEEDED(hr) && (w == 0 || h == 0 || w > 16384 || h > 16384)) hr = E_INVALIDARG;
    gates_err_t err = GATES_OK;
    if (SUCCEEDED(hr)) {
        proven_result_mem_mut_t r = alloc.alloc_fn(alloc.ctx, (gates_usize_t)w * h * 4u, 1);
        if (!proven_is_ok(r.err)) {
            err = r.err;
        } else {
            *out_rgba = (gates_u8 *)r.value.ptr;
            hr = IWICFormatConverter_CopyPixels(conv, nullptr, w * 4u, w * h * 4u, *out_rgba);
            if (FAILED(hr)) {
                alloc.free_fn(alloc.ctx, *out_rgba);
                *out_rgba = nullptr;
            }
        }
    }
    if (conv != nullptr) IWICFormatConverter_Release(conv);
    if (frame != nullptr) IWICBitmapFrameDecode_Release(frame);
    if (dec != nullptr) IWICBitmapDecoder_Release(dec);
    if (stream != nullptr) IWICStream_Release(stream);
    if (f != nullptr) IWICImagingFactory_Release(f);
    if (wpath != nullptr) HeapFree(GetProcessHeap(), 0, wpath);
    if (!gates_is_ok(err)) return err;
    if (FAILED(hr)) return from_hr(hr);
    *out_w = (gates_i32)w;
    *out_h = (gates_i32)h;
    return GATES_OK;
}

void gates_win32_install_image_decoder(gates_window_t *win) {
    gates_image_decoder_t d = { .ctx = win, .decode = decode };
    gates_tree_set_image_decoder(win->tree, &d);
}
