/* orc_hints.dll — підказки з orc_hints.txt рядками в зоні чату Warcraft 3 (1.26a), прямо в кадрі гри,
 * плюс зум і нахил камери на PgUp/PgDn. Таймер стартує сам, коли починається матч.
 * Завантажує WFE з папки Libraries (LOAD_ADD_LIBRARIES = yes у WFEConfig.ini). */
#include "orc.h"
#include <stdlib.h>

#define HINTS_FILE L"orc_hints.txt"  /* поруч із DLL або на рівень вище */
#define MAX_MSG 5           /* рядків чату на екрані */
#define MSG_MS 20000        /* скільки висить кожен рядок */
#define TIP_SEC 60          /* порада «Чи знали ви?» — раз на хвилину */
#define RESTART_MS 20000    /* поза матчем довше — наступний матч з 00:00 (меню й пауза не скидають) */
#define CHAT_X 120          /* лівий край рядків, пікселі при висоті 1080: правіше іконок героїв */
#define CHAT_BOTTOM 0.70f   /* низ останнього рядка, частка висоти: над консоллю */
#define LINE_H 28           /* висота рядка при 1080 */
#define TIME_RGB RGB(255, 204, 0)
#define TEXT_RGB RGB(255, 255, 255)
#define ZOOM_STEP 150.0f    /* крок зуму камери на PgUp/PgDn; стандартна дальність 1650 */
#define ZOOM_MIN 1000.0f
#define ZOOM_MAX 3500.0f    /* далі гра обрізає землю туманом */
#define TILT_STEP 4.0f      /* нахил на Ctrl+PgUp/PgDn, градуси; стандартно 304 (більше — пологіше) */
#define TILT_MIN 276.0f     /* майже згори */
#define TILT_MAX 344.0f

volatile int g_in_d3d8;
static HMODULE g_self;
static HWND g_wnd;
static WNDPROC g_old_proc;
static int g_unicode;

/* Підказки: час у секундах (-1 — порада без часу) і текст. */
static WCHAR g_buf[16384];
static struct hint { int sec; const WCHAR *text; } g_hints[256];
static int g_nhints, g_ntips;
static int g_bag[256], g_nbag;  /* поради в перемішаному порядку, без повторів підряд */

/* Таймер матчу: рахує лише поки йде матч. */
static DWORD g_last_tick, g_out_since;
static DWORD g_ms;
static int g_running, g_next, g_next_tip;

static struct msg { WCHAR text[160]; int prefix; DWORD shown; } g_msgs[MAX_MSG];
static int g_nmsgs, g_dirty;

static HDC g_dc;
static HBITMAP g_bmp;
static unsigned *g_bits;
static HFONT g_font;
static int g_aw, g_ah, g_bb_w, g_bb_h;
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

/* game.dll 1.26a; адреси взяті з плагіна Camera.mix (SLAVA_DROM). NULL — не та версія. */
static BYTE *game(void)
{
    BYTE *g = (BYTE *)GetModuleHandleA("game.dll");
    IMAGE_NT_HEADERS *nt;
    if (!g) return NULL;
    nt = (IMAGE_NT_HEADERS *)(g + ((IMAGE_DOS_HEADER *)g)->e_lfanew);
    return nt->OptionalHeader.SizeOfImage > 0xAD15F0 ? g : NULL;
}

static int in_match(BYTE *g)
{
#ifdef ORC_DEBUG
    if (!GetModuleHandleA("game.dll")) return 1;  /* тестовий хост без гри */
#endif
    return g && *(int *)(g + 0xAB5738) == 4 && *(int *)(g + 0xAB573C) == 4;
}

static int chat_open(BYTE *g) { return g && g[0xAD15F0] == 1; }

/* Зум і нахил: дальність і кут камери (для основного рівня зуму коліщатком) — float-и в game.dll,
 * гра підхоплює їх, коли камера зсувається (PgUp+PgDn як системні клавіші).
 * Останні значення зберігаються в %APPDATA%\orc_hints.ini і ставляться на старті кожного матчу. */
static WCHAR g_ini[MAX_PATH];

static float *cam(BYTE *g, int tilt)
{
    float *f;
    if (!g) return NULL;
    f = (float *)(g + (tilt ? 0x936438 : 0x93645C));
    return (tilt ? *f > 260 && *f < 360 : *f > 500 && *f < 10000) ? f : NULL;  /* не схоже на камеру: не та версія */
}

static void cam_write(float *f, int tilt, float v)
{
    float lo = tilt ? TILT_MIN : ZOOM_MIN, hi = tilt ? TILT_MAX : ZOOM_MAX;
    DWORD old;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    VirtualProtect(f, sizeof *f, PAGE_READWRITE, &old);
    *f = v;
    VirtualProtect(f, sizeof *f, old, &old);
}

static void cam_refresh(void)
{
    PostMessageW(g_wnd, WM_SYSKEYDOWN, VK_PRIOR, 0);
    PostMessageW(g_wnd, WM_SYSKEYDOWN, VK_NEXT, 0);
}

static void cam_load(BYTE *g)
{
    int tilt, v, any = 0;
    float *f;
    for (tilt = 0; tilt < 2; tilt++)
        if ((v = GetPrivateProfileIntW(L"camera", tilt ? L"tilt" : L"zoom", 0, g_ini)) && (f = cam(g, tilt))) {
            cam_write(f, tilt, (float)v);
            any = 1;
        }
    if (any) cam_refresh();
}

static int camera(WPARAM key, int tilt)
{
    BYTE *g = game();
    float *f;
    WCHAR num[16];
    if (!g || !in_match(g) || chat_open(g) || !(f = cam(g, tilt))) return 0;
    cam_write(f, tilt, *f + (key == VK_PRIOR ? 1 : -1) * (tilt ? TILT_STEP : ZOOM_STEP));
    wsprintfW(num, L"%d", (int)*f);
    WritePrivateProfileStringW(L"camera", tilt ? L"tilt" : L"zoom", num, g_ini);
    cam_refresh();
    return 1;
}

/* Рядки файлу: "m:ss текст" — підказка на цей час, "? текст" — порада, решта (# ...) ігнорується. */
static void parse_hints(WCHAR *s)
{
    WCHAR *line, *next;
    for (line = s; line && *line && g_nhints < (int)ARRAYSIZE(g_hints); line = next) {
        int sec = 0;
        WCHAR *e, *t = line;
        next = wcschr(line, L'\n');
        if (next) *next++ = 0;
        for (e = line + lstrlenW(line); e > line && (e[-1] == L'\r' || e[-1] == L' '); ) *--e = 0;
        if (line[0] == L'?') {
            g_hints[g_nhints++] = (struct hint){-1, line + 1 + (line[1] == L' ')};
            g_ntips++;
        } else {
            while (*t >= L'0' && *t <= L'9') sec = sec * 10 + *t++ - L'0';
            if (t > line && t[0] == L':' && t[1] >= L'0' && t[1] <= L'5' && t[2] >= L'0' && t[2] <= L'9' && t[3] == L' ')
                g_hints[g_nhints++] = (struct hint){sec * 60 + (t[1] - L'0') * 10 + t[2] - L'0', t + 4};
        }
    }
}

static int by_time(const void *a, const void *b)
{
    return ((const struct hint *)a)->sec - ((const struct hint *)b)->sec;
}

static void load_hints(void)
{
    WCHAR dir[MAX_PATH], path[MAX_PATH];
    static char raw[sizeof g_buf];
    int up;
    GetModuleFileNameW(g_self, dir, MAX_PATH);
    for (up = 0; up < 2; up++) {
        WCHAR *slash = wcsrchr(dir, L'\\');
        HANDLE f;
        DWORD n = 0;
        if (!slash) return;
        *slash = 0;
        wsprintfW(path, L"%s\\%s", dir, HINTS_FILE);
        f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        ReadFile(f, raw, sizeof raw - 1, &n, NULL);
        CloseHandle(f);
        n = MultiByteToWideChar(CP_UTF8, 0, raw, (int)n, g_buf, ARRAYSIZE(g_buf) - 1);
        g_buf[n] = 0;
        parse_hints(g_buf[0] == 0xFEFF ? g_buf + 1 : g_buf);
        qsort(g_hints, g_nhints, sizeof g_hints[0], by_time);  /* поради (-1) спереду, далі за часом */
        LOG("hints: %d (%d tips) from %ls", g_nhints, g_ntips, path);
        return;
    }
}

static void say(const WCHAR *prefix, const WCHAR *text)
{
    struct msg *m;
    if (g_nmsgs == MAX_MSG) memmove(g_msgs, g_msgs + 1, sizeof g_msgs[0] * --g_nmsgs);
    m = &g_msgs[g_nmsgs++];
    wsprintfW(m->text, L"%s %.140s", prefix, text);
    m->prefix = lstrlenW(prefix);
    m->shown = GetTickCount();
    g_dirty = 1;
}

static const WCHAR *next_tip(void)
{
    int i, j, k;
    if (!g_ntips) return NULL;
    if (!g_nbag) {
        for (i = 0; i < g_nhints; i++)
            if (g_hints[i].sec < 0) g_bag[g_nbag++] = i;
        for (i = g_nbag - 1; i > 0; i--) { j = rand() % (i + 1); k = g_bag[i]; g_bag[i] = g_bag[j]; g_bag[j] = k; }
    }
    return g_hints[g_bag[--g_nbag]].text;
}

static void restart(void)
{
    g_ms = 0;
    g_next = 0;
    g_next_tip = TIP_SEC / 2;
    g_nmsgs = 0;
    g_dirty = 1;
}

/* Раз на кадр: час матчу і нові рядки чату. */
static void tick(void)
{
    DWORD now = GetTickCount(), dt = now - g_last_tick;
    int sec, i;
    WCHAR stamp[16];
    g_last_tick = now;
    if (!in_match(game())) {
        if (g_running) { g_running = 0; g_out_since = now; }
        return;
    }
    if (!g_running) {
        g_running = 1;
        if (now - g_out_since > RESTART_MS) restart();
        cam_load(game());
        dt = 0;
    }
    g_ms += dt < 1000 ? dt : 0;  /* ponytail: пауза в одиночній грі лічиться, якщо гра лишається «в матчі»; Home — знов з 00:00 */
    sec = (int)(g_ms / 1000);
    for (; g_next < g_nhints; g_next++) {
        if (g_hints[g_next].sec < 0) continue;
        if (g_hints[g_next].sec > sec) break;
        wsprintfW(stamp, L"[%02d:%02d]", g_hints[g_next].sec / 60, g_hints[g_next].sec % 60);
        say(stamp, g_hints[g_next].text);
        MessageBeep(MB_ICONASTERISK);
    }
    if (sec >= g_next_tip) {
        const WCHAR *tip = next_tip();
        if (tip) say(L"[Чи знали ви?]", tip);
        g_next_tip = sec + TIP_SEC;
    }
    for (i = 0; i < g_nmsgs && now - g_msgs[i].shown > MSG_MS; i++) {}
    if (i) {
        memmove(g_msgs, g_msgs + i, sizeof g_msgs[0] * (g_nmsgs - i));
        g_nmsgs -= i;
        g_dirty = 1;
    }
}

/* Рядок з темним контуром: спершу маска контуру (текст, зсунутий навсібіч), потім сам текст;
 * колір — за x: префікс жовтий, решта біла. */
static void draw_line(int y0, int lh, const struct msg *m)
{
    static unsigned char outline[4096 * 64];
    int o = lh / 14 > 1 ? lh / 14 : 1, x, y, dx, dy, split, len = lstrlenW(m->text);
    RECT row = {0, y0, g_aw, y0 + lh};
    SIZE sz;
    unsigned tc = ((TIME_RGB & 0xFF) << 16) | (TIME_RGB & 0xFF00) | ((TIME_RGB >> 16) & 0xFF);
    unsigned xc = ((TEXT_RGB & 0xFF) << 16) | (TEXT_RGB & 0xFF00) | ((TEXT_RGB >> 16) & 0xFF);
    if (g_aw > 4096 || lh > 64) return;
    GetTextExtentPoint32W(g_dc, m->text, m->prefix, &sz);
    split = o + sz.cx;
    for (dy = -o; dy <= o; dy++)
        for (dx = -o; dx <= o; dx++) {
            RECT r = {o + dx, y0 + dy, g_aw - o + dx, y0 + lh + dy};
            DrawTextW(g_dc, m->text, len, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    GdiFlush();
    for (y = 0; y < lh; y++)
        for (x = 0; x < g_aw; x++) outline[y * g_aw + x] = (unsigned char)(g_bits[(y0 + y) * g_aw + x] >> 16);
    FillRect(g_dc, &row, GetStockObject(BLACK_BRUSH));
    {
        RECT r = {o, y0, g_aw - o, y0 + lh};
        DrawTextW(g_dc, m->text, len, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    GdiFlush();
    for (y = 0; y < lh; y++)
        for (x = 0; x < g_aw; x++) {
            unsigned *p = &g_bits[(y0 + y) * g_aw + x], col = x < split ? tc : xc;
            unsigned a1 = (*p >> 16) & 0xFF, a2 = outline[y * g_aw + x], a = a1 > a2 ? a1 : a2;
            unsigned k = a ? a1 * 255 / a : 0;  /* частка тексту, решта — чорний контур */
            *p = (a << 24) | ((((col >> 16) & 0xFF) * k / 255) << 16) |
                 ((((col >> 8) & 0xFF) * k / 255) << 8) | ((col & 0xFF) * k / 255);
        }
}

static void redraw(int bb_w, int bb_h)
{
    float s = bb_h / 1080.0f;
    int lh, aw, ah, i;
    if (s < 0.85f) s = 0.85f;  /* дрібніше вже не читається */
    lh = (int)(LINE_H * s);
    aw = (int)(bb_w * 0.62f);
    ah = MAX_MSG * lh;
    if (aw != g_aw || ah != g_ah) {
        BITMAPINFO bi = {{sizeof(bi.bmiHeader), aw, -ah, 1, 32, BI_RGB}};
        if (g_bmp) DeleteObject(g_bmp);
        if (g_font) DeleteObject(g_font);
        if (!g_dc) g_dc = CreateCompatibleDC(NULL);
        g_bmp = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, (void **)&g_bits, NULL, 0);
        SelectObject(g_dc, g_bmp);
        g_font = CreateFontW(-(int)(19 * s), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_aw = aw;
        g_ah = ah;
        g_frame.w = aw;
        g_frame.h = ah;
        g_frame.px = g_bits;
    }
    if (!g_bits) return;
    SetBkMode(g_dc, TRANSPARENT);
    SetTextColor(g_dc, RGB(255, 255, 255));
    SelectObject(g_dc, g_font);
    GdiFlush();
    for (i = 0; i < aw * ah; i++) g_bits[i] = 0;  /* порожнє — повністю прозоре */
    for (i = 0; i < g_nmsgs; i++)  /* найновіший — унизу */
        draw_line((MAX_MSG - g_nmsgs + i) * lh, lh, &g_msgs[i]);
    g_frame.q[0] = (struct ovl_quad){(float)(int)(CHAT_X * s), (float)(int)(bb_h * CHAT_BOTTOM - ah),
                                     (float)aw, (float)ah, 0, 0};
    g_frame.nq = 1;
    g_frame.version++;
}

int ovl_vertices(const struct ovl_frame *f, int tw, int th, struct vtx *out)
{
    int i, n = 0;
    for (i = 0; i < f->nq; i++) {
        const struct ovl_quad *q = &f->q[i];
        float x0 = q->x - 0.5f, y0 = q->y - 0.5f, x1 = x0 + q->w, y1 = y0 + q->h;
        float u0 = (float)q->u / tw, v0 = (float)q->v / th;
        float u1 = (q->u + q->w) / tw, v1 = (q->v + q->h) / th;
        struct vtx a = {x0, y0, 0, 1, u0, v0}, b = {x1, y0, 0, 1, u1, v0};
        struct vtx c = {x0, y1, 0, 1, u0, v1}, d = {x1, y1, 0, 1, u1, v1};
        out[n++] = a; out[n++] = b; out[n++] = c;
        out[n++] = c; out[n++] = b; out[n++] = d;
    }
    return n;
}

/* Клавіші ловимо у вікна гри: PgUp/PgDn — зум, Ctrl+PgUp/PgDn — нахил, Home — таймер знов з 00:00. */
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN && (wp == VK_PRIOR || wp == VK_NEXT) && camera(wp, GetKeyState(VK_CONTROL) < 0)) return 0;
    if (m == WM_KEYDOWN && wp == VK_HOME && in_match(game()) && !chat_open(game())) restart();
    return g_unicode ? CallWindowProcW(g_old_proc, h, m, wp, lp) : CallWindowProcA(g_old_proc, h, m, wp, lp);
}

static void subclass(HWND wnd)
{
    WNDPROC old;
    if (!wnd || wnd == g_wnd || !IsWindow(wnd)) return;
    g_unicode = IsWindowUnicode(wnd);
    old = (WNDPROC)(g_unicode ? SetWindowLongPtrW(wnd, GWLP_WNDPROC, (LONG_PTR)wndproc)
                              : SetWindowLongPtrA(wnd, GWLP_WNDPROC, (LONG_PTR)wndproc));
    LOG("subclass %p: old %p", (void *)wnd, (void *)old);
    if (old) { g_old_proc = old; g_wnd = wnd; }
}

const struct ovl_frame *ovl_frame(int bb_w, int bb_h, HWND wnd)
{
    subclass(wnd);
    tick();
    if (!g_running) return NULL;
    if (g_dirty || bb_w != g_bb_w || bb_h != g_bb_h) {
        g_dirty = 0;
        g_bb_w = bb_w;
        g_bb_h = bb_h;
        redraw(bb_w, bb_h);
    }
    return g_nmsgs ? &g_frame : NULL;
}

static DWORD WINAPI init(LPVOID unused)
{
    WNDCLASSW wc = {0};
    HWND wnd;
    int ok8 = 0, ok9 = 0, i;
    (void)unused;
    srand(GetTickCount());
    ExpandEnvironmentStringsW(L"%APPDATA%\\orc_hints.ini", g_ini, MAX_PATH);
    load_hints();

    /* Тимчасовий пристрій D3D лише для того, щоб знайти код Present/Reset і перехопити його. */
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
        g_out_since = GetTickCount() - RESTART_MS - 1;
        CloseHandle(CreateThread(NULL, 0, init, NULL, 0, NULL));
    }
    return TRUE;
}
