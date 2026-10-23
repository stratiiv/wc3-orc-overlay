/* orc_hints.dll — малює підказки orc_overlay.pyw прямо в кадрі Warcraft 3,
 * а також літери гарячих клавіш (як у Reforged) на сітці команд унизу праворуч.
 * Текст і геометрію сітки бере зі спільної пам'яті "Local\OrcOverlay", яку пише Python-оверлей.
 * Завантажується WFE (папка Libraries) або будь-яким інжектором. */
#include "orc.h"
#include <shellapi.h>

#define MAP_NAME L"Local\\OrcOverlay"
#define MAP_SIZE 8192
#define BASE_W 600          /* розмір смужки при висоті екрана 1080, далі масштабується */
#define BASE_H 106
#define SMALL_H 36          /* згорнута смужка: лише рядок з таймером */
#define BTN 26              /* кнопки «згорнути» і «закрити» в правому верхньому куті */
#define TOP_PERCENT 11      /* відступ зверху, % висоти: щоб не закривати годинник WC3 */
#define BG_ALPHA 204        /* непрозорість фону, 0..255 (~80%) */
#define PY_TIMEOUT_MS 3000  /* Python мовчить довше — ховаємо смужку */
#define LETTER_SCALE 0.40f  /* розмір літери відносно клітинки сітки */
#define LETTER_RGB RGB(255, 204, 0)  /* колір підсвітки гарячих клавіш WC3 */
#define ZOOM_STEP 150.0f    /* крок зуму камери на PgUp/PgDn; стандартна дальність 1650 */
#define ZOOM_MIN 1000.0f
#define ZOOM_MAX 3500.0f    /* далі гра обрізає землю туманом */

static const WCHAR GRID_KEYS[] = L"QWERASDFZXCV";

struct shared {
    volatile LONG seq;        /* непарне — Python саме пише */
    volatile LONG flags;      /* біт 0 — підсвітка рамки, біт 1 — контра, біт 2 — літери сітки */
    volatile DWORD dll_beat;  /* GetTickCount від DLL: Python ховає своє вікно */
    volatile DWORD py_beat;   /* GetTickCount від Python */
    float grid[5];            /* лівий край, низ, крок x, крок y, розмір клітинки — в одиницях UI 0.8 x 0.6 */
    volatile DWORD clicks;    /* кліки по кнопках: молодші 16 біт — згорнути, старші — закрити */
    WCHAR text[(MAP_SIZE - 40) / 2];  /* 4 рядки через \n */
};

volatile int g_in_d3d8;
static HMODULE g_self;
static struct shared *g_shm;
static DWORD g_next_open;
static LONG g_seq_drawn = -1;
static int g_bb_w, g_bb_h, g_flags;
static float g_grid[5];
static WCHAR g_text[512];

static HDC g_dc;
static HBITMAP g_bmp;
static unsigned *g_bits;
static HFONT g_big, g_small, g_tiny, g_letter;
static int g_sw, g_sh, g_lsz;  /* смужка і літера, для яких створено шрифти */
static int g_margin;           /* відступ тексту від країв смужки */
static struct ovl_frame g_frame;

static HWND g_wnd;
static WNDPROC g_old_proc;
static int g_unicode;
static RECT g_btn[2];          /* згорнути, закрити — у пікселях backbuffer */

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

static void resize(int sw, int sh, int lsz, float s)
{
    int aw = sw > 12 * lsz ? sw : 12 * lsz, ah = sh + lsz;
    BITMAPINFO bi = {{sizeof(bi.bmiHeader), aw, -ah, 1, 32, BI_RGB}};
    if (g_bmp) DeleteObject(g_bmp);
    if (g_big) { DeleteObject(g_big); DeleteObject(g_small); DeleteObject(g_tiny); DeleteObject(g_letter); }
    if (!g_dc) g_dc = CreateCompatibleDC(NULL);
    g_bmp = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, (void **)&g_bits, NULL, 0);
    SelectObject(g_dc, g_bmp);
    g_big = font((int)(20 * s), 1);
    g_small = font((int)(15 * s), 0);
    g_tiny = font((int)(13 * s), 0);
    g_letter = font(lsz * 8 / 10, 1);  /* з запасом на контур, щоб W влазила в клітинку */
    g_sw = sw; g_sh = sh; g_lsz = lsz;
    g_frame.w = aw;
    g_frame.h = ah;
    g_frame.px = g_bits;
}

static void line(const WCHAR **p, HFONT f, COLORREF c, int *y, int lh)
{
    const WCHAR *end = wcschr(*p, L'\n');
    int n = end ? (int)(end - *p) : lstrlenW(*p);
    RECT r = {g_margin, *y, g_sw - g_margin, *y + lh};
    SelectObject(g_dc, f);
    SetTextColor(g_dc, c);
    DrawTextW(g_dc, *p, n, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    *p += end ? n + 1 : n;
    *y += lh;
}

/* Літера з темним контуром: спершу маска контуру, потім маска самої літери. */
static void glyph(int x0, int y0, WCHAR ch)
{
    static unsigned char outline[128 * 128];
    int n = g_lsz, o = n / 14 > 1 ? n / 14 : 1, x, y, dx, dy;
    RECT cell = {x0, y0, x0 + n, y0 + n};
    unsigned col = ((LETTER_RGB & 0xFF) << 16) | (LETTER_RGB & 0xFF00) | ((LETTER_RGB >> 16) & 0xFF);
    if (n > 128) return;
    FillRect(g_dc, &cell, GetStockObject(BLACK_BRUSH));  /* прибрати, що «натекло» від сусідньої літери */
    SelectObject(g_dc, g_letter);
    SetTextColor(g_dc, RGB(255, 255, 255));
    for (dy = -o; dy <= o; dy++)
        for (dx = -o; dx <= o; dx++) {
            RECT r = {x0 + o + 1 + dx, y0 + dy, x0 + n + dx, y0 + n + dy};
            DrawTextW(g_dc, &ch, 1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
        }
    GdiFlush();
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++) outline[y * n + x] = (unsigned char)(g_bits[(y0 + y) * g_frame.w + x0 + x] >> 16);
    FillRect(g_dc, &cell, GetStockObject(BLACK_BRUSH));
    {
        RECT r = {x0 + o + 1, y0, x0 + n, y0 + n};
        DrawTextW(g_dc, &ch, 1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    }
    GdiFlush();
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++) {
            unsigned *p = &g_bits[(y0 + y) * g_frame.w + x0 + x];
            unsigned a1 = (*p >> 16) & 0xFF, a2 = outline[y * n + x], a = a1 > a2 ? a1 : a2;
            unsigned k = a ? a1 * 255 / a : 0;  /* частка літери, решта — чорний контур */
            *p = (a << 24) | ((((col >> 16) & 0xFF) * k / 255) << 16) |
                 ((((col >> 8) & 0xFF) * k / 255) << 8) | ((col & 0xFF) * k / 255);
        }
}

static void redraw(int bb_w, int bb_h)
{
    float s = bb_h / 1080.0f, cell = g_grid[4] / 0.6f * bb_h;
    int sw, sh, lsz, b, i, x, y, bs, pad;
    const WCHAR *p = g_text;
    RECT all, btn[2];
    if (s < 0.85f) s = 0.85f;  /* дрібніше вже не читається */
    sw = (int)(BASE_W * s);
    sh = (int)(((g_flags & 8) ? SMALL_H : BASE_H) * s);
    lsz = (int)(cell * LETTER_SCALE);
    if (lsz < 8) lsz = 8;
    if (lsz > 128) lsz = 128;
    if (sw != g_sw || sh != g_sh || lsz != g_lsz) resize(sw, sh, lsz, s);
    if (!g_bits) return;

    SetRect(&all, 0, 0, g_frame.w, g_frame.h);
    FillRect(g_dc, &all, GetStockObject(BLACK_BRUSH));
    SetBkMode(g_dc, TRANSPARENT);
    bs = (int)(BTN * s);
    pad = (int)(3 * s);
    SetRect(&btn[1], sw - pad - bs, pad, sw - pad, pad + bs);
    SetRect(&btn[0], sw - pad - 2 * bs, pad, sw - pad - bs, pad + bs);
    SelectObject(g_dc, g_big);
    SetTextColor(g_dc, RGB(187, 187, 187));
    DrawTextW(g_dc, (g_flags & 8) ? L"+" : L"\x2013", 1, &btn[0], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(g_dc, L"\x00D7", 1, &btn[1], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    y = (int)(4 * s);
    g_margin = 8 + pad + 2 * bs;  /* верхній рядок не залазить під кнопки, лишаючись по центру */
    line(&p, g_big, (g_flags & 2) ? RGB(255, 112, 112) : RGB(255, 255, 255), &y, (int)(28 * s));
    g_margin = 8;
    line(&p, g_big, RGB(255, 211, 77), &y, (int)(28 * s));
    line(&p, g_small, RGB(208, 208, 208), &y, (int)(23 * s));
    line(&p, g_tiny, RGB(150, 150, 150), &y, (int)(21 * s));
    GdiFlush();
    for (y = 0; y < sh; y++)
        for (x = 0; x < sw; x++) {
            unsigned *px = &g_bits[y * g_frame.w + x];
            *px = (*px & 0xFFFFFF) | ((unsigned)BG_ALPHA << 24);
        }
    if (g_flags & 1) {  /* жовта рамка на нову підказку */
        b = (int)(4 * s) > 2 ? (int)(4 * s) : 2;
        for (y = 0; y < sh; y++)
            for (x = 0; x < sw; x++)
                if (x < b || y < b || x >= sw - b || y >= sh - b) g_bits[y * g_frame.w + x] = 0xFFFFCC00;
    }

    g_frame.q[0] = (struct ovl_quad){(float)((bb_w - sw) / 2), (float)(bb_h * TOP_PERCENT / 100),
                                     (float)sw, (float)sh, 0, 0};
    g_frame.nq = 1;
    for (i = 0; i < 2; i++) {
        g_btn[i] = btn[i];
        OffsetRect(&g_btn[i], (int)g_frame.q[0].x, (int)g_frame.q[0].y);
    }
    if (g_flags & 4) {  /* літери сітки: клітинка (рядок r, стовпець c), рядок 0 — верхній */
        float inset = cell * 0.06f;
        for (i = 0; i < 12; i++) {
            int r = i / 4, c = i % 4;
            float ui_x = g_grid[0] + c * g_grid[2];
            float ui_top = g_grid[1] + (2 - r) * g_grid[3] + g_grid[4];
            glyph(i * lsz, sh, GRID_KEYS[i]);
            g_frame.q[g_frame.nq++] = (struct ovl_quad){ui_x / 0.8f * bb_w + inset,
                                                        (0.6f - ui_top) / 0.6f * bb_h + inset,
                                                        (float)lsz, (float)lsz, i * lsz, sh};
        }
    }
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

/* Копія тексту без розірваних рядків: seqlock, Python робить seq непарним на час запису. */
static int read_text(LONG *seq)
{
    LONG s1 = g_shm->seq;
    int i;
    if (s1 & 1) return 0;
    MemoryBarrier();
    lstrcpynW(g_text, (const WCHAR *)g_shm->text, ARRAYSIZE(g_text));
    g_flags = g_shm->flags;
    for (i = 0; i < 5; i++) g_grid[i] = g_shm->grid[i];
    MemoryBarrier();
    if (g_shm->seq != s1) return 0;
    *seq = s1;
    return 1;
}

/* Зум камери для 1.26a, як у плагіні Camera.mix (SLAVA_DROM): дальність камери — float у game.dll,
 * після запису гра підхоплює його, коли камера зсувається (PgUp+PgDn як системні клавіші). */
static int zoom(WPARAM key)
{
    BYTE *g = (BYTE *)GetModuleHandleA("game.dll");
    IMAGE_NT_HEADERS *nt;
    float *dist, v;
    DWORD old;
    if (!g) return 0;
    nt = (IMAGE_NT_HEADERS *)(g + ((IMAGE_DOS_HEADER *)g)->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage <= 0xAD15F0) return 0;  /* не та версія гри */
    if (*(int *)(g + 0xAB5738) != 4 || *(int *)(g + 0xAB573C) != 4 || g[0xAD15F0] == 1) return 0;  /* не в матчі або відкритий чат */
    dist = (float *)(g + 0x93645C);
    if (!(*dist > 500 && *dist < 10000)) return 0;  /* не схоже на дальність камери: не та версія */
    v = *dist + (key == VK_PRIOR ? ZOOM_STEP : -ZOOM_STEP);
    if (v < ZOOM_MIN) v = ZOOM_MIN;
    if (v > ZOOM_MAX) v = ZOOM_MAX;
    VirtualProtect(dist, sizeof *dist, PAGE_READWRITE, &old);
    *dist = v;
    VirtualProtect(dist, sizeof *dist, old, &old);
    PostMessageW(g_wnd, WM_SYSKEYDOWN, VK_PRIOR, 0);
    PostMessageW(g_wnd, WM_SYSKEYDOWN, VK_NEXT, 0);
    return 1;
}

/* Кліки по кнопках смужки ловимо у вікна гри (у fullscreen мишу бачить тільки воно) і не пускаємо в гру. */
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN && (wp == VK_PRIOR || wp == VK_NEXT) && zoom(wp)) return 0;
    if ((m == WM_LBUTTONDOWN || m == WM_LBUTTONUP || m == WM_LBUTTONDBLCLK) && g_shm && g_frame.version) {
        RECT rc;
        POINT pt;
        int i;
        GetClientRect(h, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            pt.x = (short)LOWORD(lp) * g_bb_w / rc.right;  /* клієнтська область може бути іншого розміру, ніж backbuffer */
            pt.y = (short)HIWORD(lp) * g_bb_h / rc.bottom;
            for (i = 0; i < 2; i++)
                if (PtInRect(&g_btn[i], pt)) {
                    if (m != WM_LBUTTONUP) g_shm->clicks += i ? 0x10000 : 1;
                    return 0;
                }
        }
    }
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
    DWORD now = GetTickCount();
    LONG seq;
    subclass(wnd);
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
    if ((g_shm->seq != g_seq_drawn || bb_w != g_bb_w || bb_h != g_bb_h) && read_text(&seq)) {
        g_seq_drawn = seq;
        g_bb_w = bb_w;
        g_bb_h = bb_h;
        redraw(bb_w, bb_h);
        LOG("redraw seq %ld bb %dx%d atlas %dx%d quads %d v%u", seq, bb_w, bb_h, g_frame.w, g_frame.h,
            g_frame.nq, g_frame.version);
    }
    return g_frame.version ? &g_frame : NULL;
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
        CloseHandle(CreateThread(NULL, 0, init, NULL, 0, NULL));
    }
    return TRUE;
}
