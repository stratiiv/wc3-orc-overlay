/* Малювання смужки через Direct3D 8 — рендер Warcraft 3 1.26 за замовчуванням. */
#define CINTERFACE
#include "orc.h"
#include <d3d8.h>

typedef HRESULT(WINAPI *present_fn)(IDirect3DDevice8 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT(WINAPI *reset_fn)(IDirect3DDevice8 *, D3DPRESENT_PARAMETERS *);
static present_fn real_present;
static reset_fn real_reset;

static IDirect3DDevice8 *g_dev;
static IDirect3DTexture8 *g_tex;
static int g_tw, g_th;
static unsigned g_ver;
static DWORD g_sb;

static void release_all(void)
{
    if (g_tex) IDirect3DTexture8_Release(g_tex);
    if (g_sb) IDirect3DDevice8_DeleteStateBlock(g_dev, g_sb);
    g_tex = NULL;
    g_sb = 0;
}

static void draw(IDirect3DDevice8 *dev)
{
    IDirect3DSurface8 *bb;
    D3DSURFACE_DESC d;
    D3DLOCKED_RECT lr;
    const struct ovl_frame *f;
    struct vtx v[13 * 6];
    int y, n;

    if (dev != g_dev) { g_tex = NULL; g_sb = 0; g_dev = dev; }  /* старий пристрій уже знищено */
    if (IDirect3DDevice8_TestCooperativeLevel(dev) != D3D_OK) return;
    if (FAILED(IDirect3DDevice8_GetBackBuffer(dev, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    IDirect3DSurface8_GetDesc(bb, &d);
    IDirect3DSurface8_Release(bb);
    if (!(f = ovl_frame(d.Width, d.Height))) return;

    if (!g_tex || g_tw < f->w || g_th < f->h) {
        if (g_tex) IDirect3DTexture8_Release(g_tex);
        g_tex = NULL;
        g_tw = pow2(f->w);
        g_th = pow2(f->h);
        if (FAILED(IDirect3DDevice8_CreateTexture(dev, g_tw, g_th, 1, 0, D3DFMT_A8R8G8B8,
                                                  D3DPOOL_MANAGED, &g_tex))) { g_tex = NULL; return; }
        g_ver = 0;
    }
    if (g_ver != f->version) {
        if (FAILED(IDirect3DTexture8_LockRect(g_tex, 0, &lr, NULL, 0))) return;
        for (y = 0; y < f->h; y++)
            memcpy((char *)lr.pBits + y * lr.Pitch, f->px + y * f->w, f->w * 4);
        IDirect3DTexture8_UnlockRect(g_tex, 0);
        g_ver = f->version;
    }
    if (!g_sb && FAILED(IDirect3DDevice8_CreateStateBlock(dev, D3DSBT_ALL, &g_sb))) { g_sb = 0; return; }
    IDirect3DDevice8_CaptureStateBlock(dev, g_sb);

    IDirect3DDevice8_SetVertexShader(dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
    IDirect3DDevice8_SetPixelShader(dev, 0);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_COLORWRITEENABLE, 0xF);
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)g_tex);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_TEXCOORDINDEX, 0);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    IDirect3DDevice8_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    n = ovl_vertices(f, g_tw, g_th, v);
    if (SUCCEEDED(IDirect3DDevice8_BeginScene(dev))) {
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, n / 3, v, sizeof(v[0]));
        IDirect3DDevice8_EndScene(dev);
    }
    IDirect3DDevice8_ApplyStateBlock(dev, g_sb);
}

static HRESULT WINAPI hk_present(IDirect3DDevice8 *dev, const RECT *src, const RECT *dst, HWND w, const RGNDATA *rgn)
{
    HRESULT hr;
    g_in_d3d8 = 1;
    draw(dev);
    hr = real_present(dev, src, dst, w, rgn);
    g_in_d3d8 = 0;
    return hr;
}

static HRESULT WINAPI hk_reset(IDirect3DDevice8 *dev, D3DPRESENT_PARAMETERS *pp)
{
    if (dev == g_dev) release_all();
    return real_reset(dev, pp);
}

int hook_d3d8(HWND wnd)
{
    HMODULE m = LoadLibraryW(L"d3d8.dll");
    IDirect3D8 *(WINAPI *create)(UINT);
    IDirect3D8 *d3d;
    IDirect3DDevice8 *dev;
    D3DDISPLAYMODE mode;
    D3DPRESENT_PARAMETERS pp = {0};
    HRESULT hr;
    int ok;

    if (!m || !(create = (void *)GetProcAddress(m, "Direct3DCreate8"))) return 0;
    if (!(d3d = create(D3D_SDK_VERSION))) return 0;
    IDirect3D8_GetAdapterDisplayMode(d3d, D3DADAPTER_DEFAULT, &mode);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = mode.Format;
    pp.hDeviceWindow = wnd;
    hr = IDirect3D8_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) { IDirect3D8_Release(d3d); return 0; }
    /* vtable у кожного пристрою своя, а код функцій у d3d8.dll спільний — латаємо код */
    ok = orc_hotpatch((void *)dev->lpVtbl->Reset, (void *)hk_reset, (void **)&real_reset) &&
         orc_hotpatch((void *)dev->lpVtbl->Present, (void *)hk_present, (void **)&real_present);
    LOG("d3d8 hook: %d", ok);
    IDirect3DDevice8_Release(dev);
    IDirect3D8_Release(d3d);
    return ok;
}
