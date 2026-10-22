#pragma once
#include <windows.h>

/* Готова картинка смужки (BGRA, з альфою) і де її малювати в кадрі. */
struct ovl_frame {
    int x, y, w, h;
    const unsigned *px;   /* w*h пікселів, рядок за рядком */
    unsigned version;     /* змінюється, коли картинку перемальовано */
};

/* Викликати раз на кадр з розміром backbuffer; NULL — нічого не малювати. */
const struct ovl_frame *ovl_frame(int bb_w, int bb_h);

/* Перехопити функцію (гаряче латання); 1 — вдалося. Оригінал — через *orig. */
int orc_hotpatch(void *fn, void *hook, void **orig);

int hook_d3d8(HWND wnd);
int hook_d3d9(HWND wnd);

/* 1, поки йде d3d8 Present: щоб не малювати двічі, якщо d3d8 сам іде через d3d9. */
extern volatile int g_in_d3d8;

#ifdef ORC_DEBUG  /* тестова збірка: журнал у %TEMP%\orc_hints.log */
#include <stdio.h>
#define LOG(...) do { char p_[MAX_PATH]; FILE *f_; GetTempPathA(MAX_PATH, p_); lstrcatA(p_, "orc_hints.log"); \
    if ((f_ = fopen(p_, "a"))) { fprintf(f_, __VA_ARGS__); fputc('\n', f_); fclose(f_); } } while (0)
#else
#define LOG(...) ((void)0)
#endif

static inline int pow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }
