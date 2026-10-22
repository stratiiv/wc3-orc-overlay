/* Те саме для Direct3D 9: коли в WFE увімкнено DirectX 9 (d3d8to9). */
#define CINTERFACE
#include "orc.h"
#include <d3d9.h>

typedef HRESULT(WINAPI *present_fn)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT(WINAPI *reset_fn)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
static present_fn real_present;
static reset_fn real_reset;

static IDirect3DDevice9 *g_dev;
static IDirect3DTexture9 *g_tex;
static IDirect3DStateBlock9 *g_sb;
static int g_tw, g_th;
static unsigned g_ver;

static void release_all(void)
{
    if (g_tex) IDirect3DTexture9_Release(g_tex);
    if (g_sb) IDirect3DStateBlock9_Release(g_sb);
    g_tex = NULL;
    g_sb = NULL;
}

static void draw(IDirect3DDevice9 *dev)
{
    IDirect3DSurface9 *bb;
    D3DSURFACE_DESC d;
    D3DLOCKED_RECT lr;
    const struct ovl_frame *f;
    struct vtx v[13 * 6];
    int y, n;

    if (dev != g_dev) { g_tex = NULL; g_sb = NULL; g_dev = dev; }
    if (IDirect3DDevice9_TestCooperativeLevel(dev) != D3D_OK) return;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    IDirect3DSurface9_GetDesc(bb, &d);
    IDirect3DSurface9_Release(bb);
    if (!(f = ovl_frame(d.Width, d.Height))) return;

    if (!g_tex || g_tw < f->w || g_th < f->h) {
        if (g_tex) IDirect3DTexture9_Release(g_tex);
        g_tex = NULL;
        g_tw = pow2(f->w);
        g_th = pow2(f->h);
        if (FAILED(IDirect3DDevice9_CreateTexture(dev, g_tw, g_th, 1, 0, D3DFMT_A8R8G8B8,
                                                  D3DPOOL_MANAGED, &g_tex, NULL))) { g_tex = NULL; return; }
        g_ver = 0;
    }
    if (g_ver != f->version) {
        if (FAILED(IDirect3DTexture9_LockRect(g_tex, 0, &lr, NULL, 0))) return;
        for (y = 0; y < f->h; y++)
            memcpy((char *)lr.pBits + y * lr.Pitch, f->px + y * f->w, f->w * 4);
        IDirect3DTexture9_UnlockRect(g_tex, 0);
        g_ver = f->version;
    }
    if (!g_sb && FAILED(IDirect3DDevice9_CreateStateBlock(dev, D3DSBT_ALL, &g_sb))) { g_sb = NULL; return; }
    IDirect3DStateBlock9_Capture(g_sb);

    IDirect3DDevice9_SetVertexShader(dev, NULL);
    IDirect3DDevice9_SetPixelShader(dev, NULL);
    IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SCISSORTESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_COLORWRITEENABLE, 0xF);
    IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)g_tex);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_TEXCOORDINDEX, 0);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

    n = ovl_vertices(f, g_tw, g_th, v);
    if (SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, n / 3, v, sizeof(v[0]));
        IDirect3DDevice9_EndScene(dev);
    }
    IDirect3DStateBlock9_Apply(g_sb);
}

static HRESULT WINAPI hk_present(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND w, const RGNDATA *rgn)
{
    if (!g_in_d3d8) draw(dev);
    return real_present(dev, src, dst, w, rgn);
}

static HRESULT WINAPI hk_reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
    if (dev == g_dev) release_all();
    return real_reset(dev, pp);
}

int hook_d3d9(HWND wnd)
{
    HMODULE m = LoadLibraryW(L"d3d9.dll");
    IDirect3D9 *(WINAPI *create)(UINT);
    IDirect3D9 *d3d;
    IDirect3DDevice9 *dev;
    D3DPRESENT_PARAMETERS pp = {0};
    HRESULT hr;
    int ok;

    if (!m || !(create = (void *)GetProcAddress(m, "Direct3DCreate9"))) return 0;
    if (!(d3d = create(D3D_SDK_VERSION))) return 0;
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = wnd;
    hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) { IDirect3D9_Release(d3d); return 0; }
    ok = orc_hotpatch((void *)dev->lpVtbl->Reset, (void *)hk_reset, (void **)&real_reset) &&
         orc_hotpatch((void *)dev->lpVtbl->Present, (void *)hk_present, (void **)&real_present);
    LOG("d3d9 hook: %d", ok);
    IDirect3DDevice9_Release(dev);
    IDirect3D9_Release(d3d);
    return ok;
}
