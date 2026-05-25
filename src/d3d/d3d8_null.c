/*
 * Null D3D8 backend for native Linux bring-up.
 *
 * This preserves the Xbox D3D8 ABI so recompiled code can link and run
 * non-rendering paths while a real Vulkan/DXVK-backed renderer is built.
 */
#include "d3d8_xbox.h"
#include <stdlib.h>
#include <string.h>

typedef struct NullResource {
    union {
        IDirect3DVertexBuffer8 vb;
        IDirect3DIndexBuffer8 ib;
        IDirect3DTexture8 tex;
        IDirect3DSurface8 surf;
    } iface;
    LONG ref_count;
    BYTE *data;
    UINT size;
    UINT width;
    UINT height;
    UINT pitch;
    D3DFORMAT format;
} NullResource;

static IDirect3D8 g_d3d8;
static IDirect3DDevice8 g_device;
static LONG g_d3d8_ref = 1;
static LONG g_device_ref = 1;
static D3DVIEWPORT8 g_viewport = { 0, 0, 640, 480, 0.0f, 1.0f };

static const IDirect3D8Vtbl g_d3d8_vtbl;
static const IDirect3DDevice8Vtbl g_device_vtbl;
static const IDirect3DVertexBuffer8Vtbl g_vb_vtbl;
static const IDirect3DIndexBuffer8Vtbl g_ib_vtbl;
static const IDirect3DTexture8Vtbl g_tex_vtbl;
static const IDirect3DSurface8Vtbl g_surf_vtbl;

static UINT format_bpp(D3DFORMAT fmt)
{
    switch (fmt) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_LIN_A8R8G8B8:
    case D3DFMT_LIN_X8R8G8B8:
        return 4;
    case D3DFMT_R5G6B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_LIN_R5G6B5:
    case D3DFMT_LIN_A1R5G5B5:
    case D3DFMT_LIN_A4R4G4B4:
        return 2;
    case D3DFMT_A8:
    case D3DFMT_L8:
    case D3DFMT_P8:
        return 1;
    default:
        return 4;
    }
}

static NullResource *alloc_resource(size_t iface_size, const void *vtbl, UINT size)
{
    NullResource *r = (NullResource *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    (void)iface_size;
    *(const void **)&r->iface = vtbl;
    r->ref_count = 1;
    r->size = size ? size : 1;
    r->data = (BYTE *)calloc(1, r->size);
    if (!r->data) {
        free(r);
        return NULL;
    }
    return r;
}

static HRESULT __stdcall qi_void(void *self, const IID *riid, void **ppv)
{
    (void)riid;
    if (!ppv) return E_INVALIDARG;
    *ppv = self;
    return S_OK;
}

static ULONG __stdcall res_addref(void *self)
{
    return (ULONG)InterlockedIncrement(&((NullResource *)self)->ref_count);
}

static ULONG __stdcall res_release(void *self)
{
    NullResource *r = (NullResource *)self;
    LONG ref = InterlockedDecrement(&r->ref_count);
    if (ref <= 0) {
        free(r->data);
        free(r);
    }
    return (ULONG)ref;
}

static HRESULT __stdcall vb_get_device(IDirect3DVertexBuffer8 *self, IDirect3DDevice8 **dev) { (void)self; if (dev) *dev = &g_device; return S_OK; }
static DWORD __stdcall vb_set_priority(IDirect3DVertexBuffer8 *self, DWORD p) { (void)self; return p; }
static DWORD __stdcall vb_get_priority(IDirect3DVertexBuffer8 *self) { (void)self; return 0; }
static void __stdcall vb_preload(IDirect3DVertexBuffer8 *self) { (void)self; }
static DWORD __stdcall vb_get_type(IDirect3DVertexBuffer8 *self) { (void)self; return 3; }
static HRESULT __stdcall vb_lock(IDirect3DVertexBuffer8 *self, UINT off, UINT len, BYTE **out, DWORD flags)
{
    (void)flags;
    NullResource *r = (NullResource *)self;
    if (!out || off >= r->size) return E_INVALIDARG;
    if (len == 0 || off + len > r->size) len = r->size - off;
    (void)len;
    *out = r->data + off;
    return S_OK;
}
static HRESULT __stdcall vb_unlock(IDirect3DVertexBuffer8 *self) { (void)self; return S_OK; }
static HRESULT __stdcall vb_get_desc(IDirect3DVertexBuffer8 *self, void *desc) { (void)self; if (desc) memset(desc, 0, 32); return S_OK; }

static HRESULT __stdcall ib_get_device(IDirect3DIndexBuffer8 *self, IDirect3DDevice8 **dev) { (void)self; if (dev) *dev = &g_device; return S_OK; }
static DWORD __stdcall ib_set_priority(IDirect3DIndexBuffer8 *self, DWORD p) { (void)self; return p; }
static DWORD __stdcall ib_get_priority(IDirect3DIndexBuffer8 *self) { (void)self; return 0; }
static void __stdcall ib_preload(IDirect3DIndexBuffer8 *self) { (void)self; }
static DWORD __stdcall ib_get_type(IDirect3DIndexBuffer8 *self) { (void)self; return 4; }
static HRESULT __stdcall ib_lock(IDirect3DIndexBuffer8 *self, UINT off, UINT len, BYTE **out, DWORD flags) { return vb_lock((IDirect3DVertexBuffer8 *)self, off, len, out, flags); }
static HRESULT __stdcall ib_unlock(IDirect3DIndexBuffer8 *self) { (void)self; return S_OK; }
static HRESULT __stdcall ib_get_desc(IDirect3DIndexBuffer8 *self, void *desc) { (void)self; if (desc) memset(desc, 0, 32); return S_OK; }

static HRESULT __stdcall tex_get_device(IDirect3DTexture8 *self, IDirect3DDevice8 **dev) { (void)self; if (dev) *dev = &g_device; return S_OK; }
static DWORD __stdcall tex_set_priority(IDirect3DTexture8 *self, DWORD p) { (void)self; return p; }
static DWORD __stdcall tex_get_priority(IDirect3DTexture8 *self) { (void)self; return 0; }
static void __stdcall tex_preload(IDirect3DTexture8 *self) { (void)self; }
static DWORD __stdcall tex_get_type(IDirect3DTexture8 *self) { (void)self; return 5; }
static DWORD __stdcall tex_get_level_count(IDirect3DTexture8 *self) { (void)self; return 1; }
static HRESULT __stdcall tex_get_level_desc(IDirect3DTexture8 *self, UINT level, D3DSURFACE_DESC *desc)
{
    NullResource *r = (NullResource *)self;
    if (!desc || level != 0) return E_INVALIDARG;
    memset(desc, 0, sizeof(*desc));
    desc->Width = r->width;
    desc->Height = r->height;
    desc->Format = r->format;
    return S_OK;
}
static HRESULT __stdcall tex_get_surface_level(IDirect3DTexture8 *self, UINT level, IDirect3DSurface8 **out)
{
    (void)self; (void)level;
    if (!out) return E_INVALIDARG;
    NullResource *r = alloc_resource(sizeof(IDirect3DSurface8), &g_surf_vtbl, 1);
    if (!r) return E_OUTOFMEMORY;
    *out = &r->iface.surf;
    return S_OK;
}
static HRESULT __stdcall tex_lock_rect(IDirect3DTexture8 *self, UINT level, D3DLOCKED_RECT *lr, const RECT *rect, DWORD flags)
{
    (void)level; (void)rect; (void)flags;
    NullResource *r = (NullResource *)self;
    if (!lr) return E_INVALIDARG;
    lr->Pitch = (INT)r->pitch;
    lr->pBits = r->data;
    return S_OK;
}
static HRESULT __stdcall tex_unlock_rect(IDirect3DTexture8 *self, UINT level) { (void)self; (void)level; return S_OK; }

static HRESULT __stdcall surf_get_device(IDirect3DSurface8 *self, IDirect3DDevice8 **dev) { (void)self; if (dev) *dev = &g_device; return S_OK; }
static HRESULT __stdcall surf_get_desc(IDirect3DSurface8 *self, D3DSURFACE_DESC *desc)
{
    NullResource *r = (NullResource *)self;
    if (!desc) return E_INVALIDARG;
    memset(desc, 0, sizeof(*desc));
    desc->Width = r->width;
    desc->Height = r->height;
    desc->Format = r->format;
    return S_OK;
}
static HRESULT __stdcall surf_lock_rect(IDirect3DSurface8 *self, D3DLOCKED_RECT *lr, const RECT *rect, DWORD flags)
{
    (void)rect; (void)flags;
    NullResource *r = (NullResource *)self;
    if (!lr) return E_INVALIDARG;
    lr->Pitch = (INT)r->pitch;
    lr->pBits = r->data;
    return S_OK;
}
static HRESULT __stdcall surf_unlock_rect(IDirect3DSurface8 *self) { (void)self; return S_OK; }

static HRESULT __stdcall dev_qi(IDirect3DDevice8 *self, const IID *riid, void **ppv) { return qi_void(self, riid, ppv); }
static ULONG __stdcall dev_addref(IDirect3DDevice8 *self) { (void)self; return (ULONG)InterlockedIncrement(&g_device_ref); }
static ULONG __stdcall dev_release(IDirect3DDevice8 *self) { (void)self; return (ULONG)InterlockedDecrement(&g_device_ref); }
static HRESULT __stdcall dev_get_d3d(IDirect3DDevice8 *self, IDirect3D8 **out) { (void)self; if (out) *out = &g_d3d8; return S_OK; }
static HRESULT __stdcall ok0(IDirect3DDevice8 *self) { (void)self; return S_OK; }
static HRESULT __stdcall dev_get_caps(IDirect3DDevice8 *self, void *p) { (void)self; if (p) memset(p, 0, 256); return S_OK; }
static HRESULT __stdcall dev_get_display_mode(IDirect3DDevice8 *self, void *p) { (void)self; if (p) memset(p, 0, 32); return S_OK; }
static HRESULT __stdcall dev_get_creation_params(IDirect3DDevice8 *self, void *p) { (void)self; if (p) memset(p, 0, 32); return S_OK; }
static HRESULT __stdcall dev_reset(IDirect3DDevice8 *self, D3DPRESENT_PARAMETERS *pp) { (void)self; (void)pp; return S_OK; }
static HRESULT __stdcall dev_present(IDirect3DDevice8 *self, const RECT *src, const RECT *dst, HWND wnd, void *dirty) { (void)self; (void)src; (void)dst; (void)wnd; (void)dirty; return S_OK; }
static HRESULT __stdcall dev_get_backbuffer(IDirect3DDevice8 *self, INT i, DWORD type, IDirect3DSurface8 **out) { (void)self; (void)i; (void)type; return tex_get_surface_level(NULL, 0, out); }
static HRESULT __stdcall dev_clear(IDirect3DDevice8 *self, DWORD c, const D3DRECT *r, DWORD f, D3DCOLOR col, float z, DWORD s) { (void)self; (void)c; (void)r; (void)f; (void)col; (void)z; (void)s; return S_OK; }
static HRESULT __stdcall dev_set_transform(IDirect3DDevice8 *self, D3DTRANSFORMSTATETYPE st, const D3DMATRIX *m) { (void)self; (void)st; (void)m; return S_OK; }
static HRESULT __stdcall dev_get_transform(IDirect3DDevice8 *self, D3DTRANSFORMSTATETYPE st, D3DMATRIX *m) { (void)self; (void)st; if (m) memset(m, 0, sizeof(*m)); return S_OK; }
static HRESULT __stdcall dev_set_rs(IDirect3DDevice8 *self, D3DRENDERSTATETYPE st, DWORD v) { (void)self; (void)st; (void)v; return S_OK; }
static HRESULT __stdcall dev_get_rs(IDirect3DDevice8 *self, D3DRENDERSTATETYPE st, DWORD *v) { (void)self; (void)st; if (v) *v = 0; return S_OK; }
static HRESULT __stdcall dev_set_tss(IDirect3DDevice8 *self, DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD v) { (void)self; (void)s; (void)t; (void)v; return S_OK; }
static HRESULT __stdcall dev_get_tss(IDirect3DDevice8 *self, DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD *v) { (void)self; (void)s; (void)t; if (v) *v = 0; return S_OK; }
static HRESULT __stdcall dev_set_texture(IDirect3DDevice8 *self, DWORD s, IDirect3DBaseTexture8 *t) { (void)self; (void)s; (void)t; return S_OK; }
static HRESULT __stdcall dev_get_texture(IDirect3DDevice8 *self, DWORD s, IDirect3DBaseTexture8 **t) { (void)self; (void)s; if (t) *t = NULL; return S_OK; }
static HRESULT __stdcall dev_set_stream(IDirect3DDevice8 *self, UINT sn, IDirect3DVertexBuffer8 *vb, UINT stride) { (void)self; (void)sn; (void)vb; (void)stride; return S_OK; }
static HRESULT __stdcall dev_get_stream(IDirect3DDevice8 *self, UINT sn, IDirect3DVertexBuffer8 **vb, UINT *stride) { (void)self; (void)sn; if (vb) *vb = NULL; if (stride) *stride = 0; return S_OK; }
static HRESULT __stdcall dev_set_indices(IDirect3DDevice8 *self, IDirect3DIndexBuffer8 *ib, UINT base) { (void)self; (void)ib; (void)base; return S_OK; }
static HRESULT __stdcall dev_get_indices(IDirect3DDevice8 *self, IDirect3DIndexBuffer8 **ib, UINT *base) { (void)self; if (ib) *ib = NULL; if (base) *base = 0; return S_OK; }
static HRESULT __stdcall dev_draw(IDirect3DDevice8 *self, D3DPRIMITIVETYPE t, UINT a, UINT b) { (void)self; (void)t; (void)a; (void)b; return S_OK; }
static HRESULT __stdcall dev_drawi(IDirect3DDevice8 *self, D3DPRIMITIVETYPE t, UINT a, UINT b, UINT c, UINT d) { (void)self; (void)t; (void)a; (void)b; (void)c; (void)d; return S_OK; }
static HRESULT __stdcall dev_drawup(IDirect3DDevice8 *self, D3DPRIMITIVETYPE t, UINT pc, const void *v, UINT stride) { (void)self; (void)t; (void)pc; (void)v; (void)stride; return S_OK; }
static HRESULT __stdcall dev_drawiup(IDirect3DDevice8 *self, D3DPRIMITIVETYPE t, UINT min, UINT nv, UINT pc, const void *idx, D3DFORMAT f, const void *v, UINT stride) { (void)self; (void)t; (void)min; (void)nv; (void)pc; (void)idx; (void)f; (void)v; (void)stride; return S_OK; }

static HRESULT __stdcall dev_create_texture(IDirect3DDevice8 *self, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8 **out)
{
    (void)self; (void)levels; (void)usage; (void)pool;
    if (!out) return E_INVALIDARG;
    UINT pitch = w * format_bpp(fmt);
    NullResource *r = alloc_resource(sizeof(IDirect3DTexture8), &g_tex_vtbl, pitch * (h ? h : 1));
    if (!r) return E_OUTOFMEMORY;
    r->width = w; r->height = h; r->pitch = pitch; r->format = fmt;
    *out = &r->iface.tex;
    return S_OK;
}
static HRESULT __stdcall dev_create_vb(IDirect3DDevice8 *self, UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8 **out)
{
    (void)self; (void)usage; (void)fvf; (void)pool;
    if (!out) return E_INVALIDARG;
    NullResource *r = alloc_resource(sizeof(IDirect3DVertexBuffer8), &g_vb_vtbl, len);
    if (!r) return E_OUTOFMEMORY;
    *out = &r->iface.vb;
    return S_OK;
}
static HRESULT __stdcall dev_create_ib(IDirect3DDevice8 *self, UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer8 **out)
{
    (void)self; (void)usage; (void)fmt; (void)pool;
    if (!out) return E_INVALIDARG;
    NullResource *r = alloc_resource(sizeof(IDirect3DIndexBuffer8), &g_ib_vtbl, len);
    if (!r) return E_OUTOFMEMORY;
    *out = &r->iface.ib;
    return S_OK;
}
static HRESULT __stdcall dev_create_surface(IDirect3DDevice8 *self, UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, BOOL lockable, IDirect3DSurface8 **out)
{
    (void)self; (void)ms; (void)lockable;
    if (!out) return E_INVALIDARG;
    UINT pitch = w * format_bpp(fmt);
    NullResource *r = alloc_resource(sizeof(IDirect3DSurface8), &g_surf_vtbl, pitch * (h ? h : 1));
    if (!r) return E_OUTOFMEMORY;
    r->width = w; r->height = h; r->pitch = pitch; r->format = fmt;
    *out = &r->iface.surf;
    return S_OK;
}
static HRESULT __stdcall dev_create_depth_surface(IDirect3DDevice8 *self, UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, IDirect3DSurface8 **out)
{
    return dev_create_surface(self, w, h, fmt, ms, TRUE, out);
}
static HRESULT __stdcall dev_set_rt(IDirect3DDevice8 *self, IDirect3DSurface8 *rt, IDirect3DSurface8 *ds) { (void)self; (void)rt; (void)ds; return S_OK; }
static HRESULT __stdcall dev_get_rt(IDirect3DDevice8 *self, IDirect3DSurface8 **out) { (void)self; return dev_create_surface(self, 640, 480, D3DFMT_A8R8G8B8, 0, TRUE, out); }
static HRESULT __stdcall dev_get_ds(IDirect3DDevice8 *self, IDirect3DSurface8 **out) { (void)self; if (out) *out = NULL; return S_OK; }
static HRESULT __stdcall dev_set_viewport(IDirect3DDevice8 *self, const D3DVIEWPORT8 *vp) { (void)self; if (vp) g_viewport = *vp; return S_OK; }
static HRESULT __stdcall dev_get_viewport(IDirect3DDevice8 *self, D3DVIEWPORT8 *vp) { (void)self; if (vp) *vp = g_viewport; return S_OK; }
static HRESULT __stdcall dev_set_material(IDirect3DDevice8 *self, const D3DMATERIAL8 *m) { (void)self; (void)m; return S_OK; }
static HRESULT __stdcall dev_get_material(IDirect3DDevice8 *self, D3DMATERIAL8 *m) { (void)self; if (m) memset(m, 0, sizeof(*m)); return S_OK; }
static HRESULT __stdcall dev_set_light(IDirect3DDevice8 *self, DWORD i, const D3DLIGHT8 *l) { (void)self; (void)i; (void)l; return S_OK; }
static HRESULT __stdcall dev_get_light(IDirect3DDevice8 *self, DWORD i, D3DLIGHT8 *l) { (void)self; (void)i; if (l) memset(l, 0, sizeof(*l)); return S_OK; }
static HRESULT __stdcall dev_light_enable(IDirect3DDevice8 *self, DWORD i, BOOL e) { (void)self; (void)i; (void)e; return S_OK; }
static HRESULT __stdcall dev_set_shader(IDirect3DDevice8 *self, DWORD h) { (void)self; (void)h; return S_OK; }
static HRESULT __stdcall dev_get_shader(IDirect3DDevice8 *self, DWORD *h) { (void)self; if (h) *h = 0; return S_OK; }
static HRESULT __stdcall dev_set_const(IDirect3DDevice8 *self, INT r, const void *d, DWORD c) { (void)self; (void)r; (void)d; (void)c; return S_OK; }
static void __stdcall dev_set_gamma(IDirect3DDevice8 *self, DWORD flags, const D3DGAMMARAMP *r) { (void)self; (void)flags; (void)r; }
static void __stdcall dev_get_gamma(IDirect3DDevice8 *self, D3DGAMMARAMP *r) { (void)self; if (r) memset(r, 0, sizeof(*r)); }
static HRESULT __stdcall dev_set_palette(IDirect3DDevice8 *self, DWORD n, const void *e) { (void)self; (void)n; (void)e; return S_OK; }
static HRESULT __stdcall dev_begin_push(IDirect3DDevice8 *self, DWORD count, DWORD **out) { (void)self; if (out) *out = calloc(count ? count : 1, sizeof(DWORD)); return out && *out ? S_OK : E_OUTOFMEMORY; }
static HRESULT __stdcall dev_end_push(IDirect3DDevice8 *self, DWORD *p) { (void)self; free(p); return S_OK; }
static HRESULT __stdcall dev_swap(IDirect3DDevice8 *self, DWORD flags) { (void)self; (void)flags; return S_OK; }

static HRESULT __stdcall d3d8_qi(IDirect3D8 *self, const IID *riid, void **ppv) { return qi_void(self, riid, ppv); }
static ULONG __stdcall d3d8_addref(IDirect3D8 *self) { (void)self; return (ULONG)InterlockedIncrement(&g_d3d8_ref); }
static ULONG __stdcall d3d8_release(IDirect3D8 *self) { (void)self; return (ULONG)InterlockedDecrement(&g_d3d8_ref); }
static HRESULT __stdcall d3d8_create_device(IDirect3D8 *self, UINT adapter, DWORD type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **out)
{
    (void)self; (void)adapter; (void)type; (void)window; (void)flags;
    if (pp) {
        g_viewport.Width = pp->BackBufferWidth ? pp->BackBufferWidth : 640;
        g_viewport.Height = pp->BackBufferHeight ? pp->BackBufferHeight : 480;
    }
    g_device.lpVtbl = &g_device_vtbl;
    g_device_ref = 1;
    if (out) *out = &g_device;
    return S_OK;
}

static const IDirect3DVertexBuffer8Vtbl g_vb_vtbl = {
    (void *)qi_void, (void *)res_addref, (void *)res_release,
    vb_get_device, vb_set_priority, vb_get_priority, vb_preload, vb_get_type,
    vb_lock, vb_unlock, vb_get_desc
};
static const IDirect3DIndexBuffer8Vtbl g_ib_vtbl = {
    (void *)qi_void, (void *)res_addref, (void *)res_release,
    ib_get_device, ib_set_priority, ib_get_priority, ib_preload, ib_get_type,
    ib_lock, ib_unlock, ib_get_desc
};
static const IDirect3DTexture8Vtbl g_tex_vtbl = {
    (void *)qi_void, (void *)res_addref, (void *)res_release,
    tex_get_device, tex_set_priority, tex_get_priority, tex_preload, tex_get_type,
    tex_get_level_count, tex_get_level_desc, tex_get_surface_level, tex_lock_rect, tex_unlock_rect
};
static const IDirect3DSurface8Vtbl g_surf_vtbl = {
    (void *)qi_void, (void *)res_addref, (void *)res_release,
    surf_get_device, surf_get_desc, surf_lock_rect, surf_unlock_rect
};
static const IDirect3DDevice8Vtbl g_device_vtbl = {
    dev_qi, dev_addref, dev_release,
    dev_get_d3d, dev_get_caps, dev_get_display_mode, dev_get_creation_params,
    dev_reset, dev_present, dev_get_backbuffer,
    ok0, ok0, dev_clear,
    dev_set_transform, dev_get_transform,
    dev_set_rs, dev_get_rs,
    dev_set_tss, dev_get_tss,
    dev_set_texture, dev_get_texture,
    dev_set_stream, dev_get_stream, dev_set_indices, dev_get_indices,
    dev_draw, dev_drawi, dev_drawup, dev_drawiup,
    dev_create_texture, dev_create_vb, dev_create_ib, dev_create_surface, dev_create_depth_surface,
    dev_set_rt, dev_get_rt, dev_get_ds,
    dev_set_viewport, dev_get_viewport,
    dev_set_material, dev_get_material, dev_set_light, dev_get_light, dev_light_enable,
    dev_set_shader, dev_get_shader, dev_set_const, dev_set_shader, dev_get_shader, dev_set_const,
    dev_set_gamma, dev_get_gamma,
    dev_set_palette,
    dev_begin_push, dev_end_push,
    dev_swap
};
static const IDirect3D8Vtbl g_d3d8_vtbl = {
    d3d8_qi, d3d8_addref, d3d8_release, d3d8_create_device
};

IDirect3D8 *xbox_Direct3DCreate8(UINT SDKVersion)
{
    (void)SDKVersion;
    g_d3d8.lpVtbl = &g_d3d8_vtbl;
    return &g_d3d8;
}

IDirect3DDevice8 *xbox_GetD3DDevice(void)
{
    g_device.lpVtbl = &g_device_vtbl;
    return &g_device;
}

void d3d8_PresentFrame(void)
{
}
