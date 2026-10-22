/* orc_hints.dll — малює підказки orc_overlay.pyw прямо в кадрі Warcraft 3.
 * Текст бере зі спільної пам'яті "Local\OrcOverlay", яку пише Python-оверлей.
 * Завантажується WFE (папка Libraries) або будь-яким інжектором. */
#include "orc.h"
#include <shellapi.h>

#define MAP_NAME L"Local\\OrcOverlay"
#define MAP_SIZE 8192
#define BASE_W 600          /* розмір смужки при висоті екрана 1080, далі масштабується */
#define BASE_H 106
#define TOP_PERCENT 11      /* відступ зверху, % висоти: щоб не закривати годинник WC3 */
#define BG_ALPHA 204        /* непрозорість фону, 0..255 (~80%) */
#define PY_TIMEOUT_MS 3000  /* Python мовчить довше — ховаємо смужку */

struct shared {
    volatile LONG seq;        /* непарне — Python саме пише */
    volatile LONG flags;      /* біт 0 — підсвітка рамки, біт 1 — режим контри */
    volatile DWORD dll_beat;  /* GetTickCount від DLL: Python ховає своє вікно */
    volatile DWORD py_beat;   /* GetTickCount від Python */
    WCHAR text[(MAP_SIZE - 16) / 2];  /* 4 рядки через \n */
};

volatile int g_in_d3d8;
static HMODULE g_self;
static struct shared *g_shm;
static DWORD g_next_open;
static LONG g_seq_drawn = -1;
static int g_bb_h, g_flags;
static WCHAR g_text[512];

static HDC g_dc;
static HBITMAP g_bmp;
static unsigned *g_bits;
static HFONT g_big, g_small, g_tiny;
static struct ovl_frame g_frame;

/* Гаряче латання Microsoft: перед функцією 5 байтів-заповнювачів, на початку "mov edi, edi".
 * Пишемо "jmp hook" у заповнювач і "jmp $-5" поверх mov edi, edi; оригінал далі з fn+2.
 * Якщо хтось (WFE) уже так перехопив — стаємо перед ним у ланцюжок. */
int orc_hotpatch(void *fn, void *hook, void **orig)
{
    unsigned char *f = fn, *pad = f - 5;
    DWORD old;
    int chained = f[0] == 0xEB && f[1] == 0xF9 && pad[0] == 0xE9;
    if (!chained && !(f[0] == 0x8B && f[1] == 0xFF)) return 0;
    if (chained && pad + 5 + *(LONG *)(pad + 1) == (unsigned char *)hook) return 1;
    if (!VirtualProtect(pad, 7, PAGE_EXECUTE_READWRITE, &old)) return 0;
    *orig = chained ? (void *)(pad + 5 + *(LONG *)(pad + 1)) : (void *)(f + 2);
    *(LONG *)(pad + 1) = (LONG)((unsigned char *)hook - (pad + 5));
    pad[0] = 0xE9;
    *(volatile WORD *)f = 0xF9EB;
    VirtualProtect(pad, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), pad, 7);
    return 1;
}

static HFONT font(int px, int bold)
{
    return CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                       DEFAULT_PITCH, L"Segoe UI");
}

static void resize(int w, int h, float s)
{
    BITMAPINFO bi = {{sizeof(bi.bmiHeader), w, -h, 1, 32, BI_RGB}};
    if (g_bmp) DeleteObject(g_bmp);
    if (g_big) { DeleteObject(g_big); DeleteObject(g_small); DeleteObject(g_tiny); }
    if (!g_dc) g_dc = CreateCompatibleDC(NULL);
    g_bmp = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, (void **)&g_bits, NULL, 0);
    SelectObject(g_dc, g_bmp);
    g_big = font((int)(20 * s), 1);
    g_small = font((int)(15 * s), 0);
    g_tiny = font((int)(13 * s), 0);
    g_frame.w = w;
    g_frame.h = h;
    g_frame.px = g_bits;
}

static void line(const WCHAR **p, HFONT f, COLORREF c, int *y, int lh)
{
    const WCHAR *end = wcschr(*p, L'\n');
    int n = end ? (int)(end - *p) : lstrlenW(*p);
    RECT r = {8, *y, g_frame.w - 8, *y + lh};
    SelectObject(g_dc, f);
    SetTextColor(g_dc, c);
    DrawTextW(g_dc, *p, n, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    *p += end ? n + 1 : n;
    *y += lh;
}

static void redraw(int bb_h)
{
    float s = bb_h / 1080.0f;
    int w, h, b, i;
    const WCHAR *p = g_text;
    RECT all;
    if (s < 0.85f) s = 0.85f;  /* дрібніше вже не читається */
    w = (int)(BASE_W * s);
    h = (int)(BASE_H * s);
    if (w != g_frame.w || h != g_frame.h) resize(w, h, s);
    if (!g_bits) return;

    SetRect(&all, 0, 0, w, h);
    FillRect(g_dc, &all, GetStockObject(BLACK_BRUSH));
    SetBkMode(g_dc, TRANSPARENT);
    {
        int y = (int)(4 * s);
        line(&p, g_big, (g_flags & 2) ? RGB(255, 112, 112) : RGB(255, 255, 255), &y, (int)(28 * s));
        line(&p, g_big, RGB(255, 211, 77), &y, (int)(28 * s));
        line(&p, g_small, RGB(208, 208, 208), &y, (int)(23 * s));
        line(&p, g_tiny, RGB(150, 150, 150), &y, (int)(21 * s));
    }
    GdiFlush();
    for (i = 0; i < w * h; i++)
        g_bits[i] = (g_bits[i] & 0xFFFFFF) | ((unsigned)BG_ALPHA << 24);
    if (g_flags & 1) {  /* жовта рамка на нову підказку */
        b = (int)(4 * s) > 2 ? (int)(4 * s) : 2;
        for (i = 0; i < w * h; i++) {
            int x = i % w, y = i / w;
            if (x < b || y < b || x >= w - b || y >= h - b) g_bits[i] = 0xFFFFCC00;
        }
    }
    g_frame.version++;
}

/* Копія тексту без розірваних рядків: seqlock, Python робить seq непарним на час запису. */
static int read_text(LONG *seq)
{
    LONG s1 = g_shm->seq;
    if (s1 & 1) return 0;
    MemoryBarrier();
    lstrcpynW(g_text, (const WCHAR *)g_shm->text, ARRAYSIZE(g_text));
    g_flags = g_shm->flags;
    MemoryBarrier();
    if (g_shm->seq != s1) return 0;
    *seq = s1;
    return 1;
}

const struct ovl_frame *ovl_frame(int bb_w, int bb_h)
{
    DWORD now = GetTickCount();
    LONG seq;
    if (!g_shm && (LONG)(now - g_next_open) >= 0) {
        HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_NAME);
        g_next_open = now + 2000;
        LOG("open mapping: %p (err %lu)", h, h ? 0 : GetLastError());
        if (h) {
            g_shm = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, MAP_SIZE);
            CloseHandle(h);
        }
    }
    if (!g_shm) return NULL;
    if (now - g_shm->py_beat > PY_TIMEOUT_MS) {  /* оверлей закрили */
        LOG("python silent: now %lu beat %lu", now, g_shm->py_beat);
        UnmapViewOfFile((void *)g_shm);
        g_shm = NULL;
        g_seq_drawn = -1;
        return NULL;
    }
    g_shm->dll_beat = now;
    if ((g_shm->seq != g_seq_drawn || bb_h != g_bb_h) && read_text(&seq)) {
        g_seq_drawn = seq;
        g_bb_h = bb_h;
        redraw(bb_h);
        LOG("redraw seq %ld bb %dx%d -> %dx%d v%u", seq, bb_w, bb_h, g_frame.w, g_frame.h, g_frame.version);
    }
    if (!g_frame.version) return NULL;
    g_frame.x = (bb_w - g_frame.w) / 2;
    g_frame.y = bb_h * TOP_PERCENT / 100;
    return &g_frame;
}

/* Запускає orc_overlay.pyw, якщо він ще не працює: шукає поруч із DLL і на рівень вище. */
static void launch_python(void)
{
    WCHAR dir[MAX_PATH], path[MAX_PATH];
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, MAP_NAME);
    int up;
    if (h) { CloseHandle(h); return; }
    GetModuleFileNameW(g_self, dir, MAX_PATH);
    for (up = 0; up < 2; up++) {
        WCHAR *slash = wcsrchr(dir, L'\\');
        if (!slash) return;
        *slash = 0;
        wsprintfW(path, L"%s\\orc_overlay.pyw", dir);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            ShellExecuteW(NULL, L"open", path, NULL, dir, SW_SHOWNORMAL);
            return;
        }
    }
}

static DWORD WINAPI init(LPVOID unused)
{
    WNDCLASSW wc = {0};
    HWND wnd;
    int ok8 = 0, ok9 = 0, i;
    (void)unused;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    launch_python();

    /* Тимчасовий пристрій D3D лише для того, щоб знайти vtable і підмінити Present/Reset. */
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = g_self;
    wc.lpszClassName = L"OrcHintsDummy";
    RegisterClassW(&wc);
    wnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                          NULL, NULL, g_self, NULL);
    for (i = 0; i < 60 && !(ok8 && ok9); i++) {
        if (!ok8) ok8 = hook_d3d8(wnd);
        if (!ok9) ok9 = hook_d3d9(wnd);
        LOG("hook try %d: d3d8=%d d3d9=%d", i, ok8, ok9);
        if (!(ok8 && ok9)) Sleep(1000);
    }
    DestroyWindow(wnd);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        CloseHandle(CreateThread(NULL, 0, init, NULL, 0, NULL));
    }
    return TRUE;
}
