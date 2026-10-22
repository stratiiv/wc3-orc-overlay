"""Оверлей-підказки для Warcraft 3 (Орда проти Нічного ельфа). Запуск — подвійний клік.

Home — старт/пауза, PgUp/PgDn — ±10 с, End — скинути, Ctrl+End — вийти,
Alt+1..8 — контра на юніта ельфа (8 с).
"""
import ctypes
import mmap
import struct
import sys
import time
import tkinter as tk
import winsound
from ctypes import wintypes

SPEED = 1.0  # ігрових секунд за реальну; підкрути, якщо таймер розходиться з годинником гри
WIDTH, HEIGHT, ALPHA = 600, 100, 0.8
FLASH_MS, COUNTER_SEC = 1500, 8

SCHEDULE = [  # (сек, тема, що робити, деталь)
    (0, "Старт", "4 пеони на золото", "1 пеон будує Altar of Storms"),
    (25, "Будівництво", "Пеон будує Barracks", "Решта — на золото"),
    (55, "Будівництво", "Пеон будує Burrow", "Стеж за їжею"),
    (100, "Герой", "Вівтар готовий: найняти героя", "Одразу вивчити вміння"),
    (130, "Магазин", "Купити 2 Healing Salve", ""),
    (150, "Армія", "Казарма: 5 грантів у черзі", ""),
    (240, "Розвідка", "Пеон до бази ельфа", "Що в нього: лучниці, дріади, химери?"),
    (270, "Економіка", "Друга Burrow, крипи героєм", "Пеонів зупинити на 16–20"),
    (360, "Апгрейд", "Stronghold і War Mill", ""),
    (420, "Армія", "Друга казарма", "Золото не більше 300"),
    (480, "Магазин", "Salve + Scroll of Town Portal", ""),
    (540, "Розвідка", "Чи є гніздо химер?", "Є химери → Alt+6"),
    (600, "Атака", "Атакувати другу базу AI", ""),
    (660, "Тех", "Fortress → Tauren Totem → Tauren", "Вивчити Pulverize"),
]
CYCLE_START, CYCLE_STEP, SCOUT_EVERY = 720, 45, 180
SCOUT = ("Розвідка", "Розвідати базу й армію ельфа", "Нові юніти → Alt+цифра")
MACRO = [
    ("Макро", "Золото > 300 → юніти в чергу", ""),
    ("Макро", "Їжа до ліміту ≤ 5 → Burrow", ""),
    ("Магазин", "Герой біля магазину → 2 Healing Salve", ""),
    ("Крипи", "Герой без діла → крипи", ""),
]
COUNTERS = {
    0x31: ("Лучниці", "Гранти в лоб, тролі за ними"),
    0x32: ("Мисливиці", "Гранти + таурени", "Не ганятися поодинці"),
    0x33: ("Дріади", "Гранти й таурени", "Не кастувати Bloodlust"),
    0x34: ("Ведмеді", "Таурени з Pulverize", "Фокус по одному"),
    0x35: ("Велетні", "Бий армію позаду велетнів", "Велетнів ігнорувати"),
    0x36: ("Химери", "Wind Rider + Envenomed Spears", "І тролі"),
    0x37: ("Гіппогрифи / драконята", "Troll Headhunter, виверни"),
    0x38: ("Демонхантер", "Hex від Shadow Hunter", "Уся армія — правим кліком по ньому"),
}

VK_CONTROL, VK_MENU, VK_PRIOR, VK_NEXT, VK_END, VK_HOME = 0x11, 0x12, 0x21, 0x22, 0x23, 0x24
GWL_EXSTYLE = -20
WS_EX_TRANSPARENT, WS_EX_TOOLWINDOW, WS_EX_LAYERED, WS_EX_NOACTIVATE = 0x20, 0x80, 0x80000, 0x08000000
WS_EX_APPWINDOW = 0x40000
SWP_FLAGS = 0x0001 | 0x0002 | 0x0010  # NOSIZE | NOMOVE | NOACTIVATE
HWND_TOPMOST = wintypes.HWND(-1)

# Спільна пам'ять для orc_hints.dll, яка малює ту саму смужку всередині гри (і в fullscreen).
# Розмітка — як struct shared в orc.c.
MAP_NAME, MAP_SIZE, TEXT_OFF = "Local\\OrcOverlay", 8192, 16
DLL_ALIVE_MS = 1500  # DLL відмітилась нещодавно — гра малює смужку сама, своє вікно ховаємо


class SharedText:
    def __init__(self):
        self.m = mmap.mmap(-1, MAP_SIZE, tagname=MAP_NAME)
        self.seq, self.last = 0, None
        self.tick = ctypes.windll.kernel32.GetTickCount

    def publish(self, lines, flags):
        now = self.tick()
        struct.pack_into("<I", self.m, 12, now)  # пульс Python
        if (lines, flags) == self.last:
            return
        self.last = (lines, flags)
        data = "\n".join(lines).encode("utf-16-le")[: MAP_SIZE - TEXT_OFF - 2] + b"\0\0"
        self.seq += 1  # непарне: DLL не читає, поки пишемо
        struct.pack_into("<I", self.m, 0, self.seq)
        self.m[TEXT_OFF:TEXT_OFF + len(data)] = data
        struct.pack_into("<I", self.m, 4, flags)
        self.seq += 1
        struct.pack_into("<I", self.m, 0, self.seq)

    def dll_alive(self):
        beat = struct.unpack_from("<I", self.m, 8)[0]
        return beat != 0 and (self.tick() - beat) & 0xFFFFFFFF < DLL_ALIVE_MS


def hint_at(t):
    """(початок, тема, дія, деталь, початок наступної) для ігрового часу t."""
    if t < CYCLE_START:
        i = max(i for i, h in enumerate(SCHEDULE) if h[0] <= t)
        nxt = SCHEDULE[i + 1][0] if i + 1 < len(SCHEDULE) else CYCLE_START
        return (*SCHEDULE[i], nxt)
    k = int(t - CYCLE_START) // CYCLE_STEP
    start = CYCLE_START + k * CYCLE_STEP
    per = SCOUT_EVERY // CYCLE_STEP  # кожен per-ий слот — розвідка (720 кратне 180)
    h = SCOUT if k % per == 0 else MACRO[(k - k // per - 1) % len(MACRO)]
    return (start, *h, start + CYCLE_STEP)


def fmt(t):
    t = int(t)
    return f"{t // 60:02d}:{t % 60:02d}"


class Overlay:
    def __init__(self):
        self.user32 = ctypes.windll.user32
        self.acc, self.run_from = 0.0, None
        self.last_start, self.counter, self.counter_until = None, None, 0.0
        self.keys = {}
        self.flash_until, self.hidden = 0.0, False
        self.shared = SharedText()

        r = self.root = tk.Tk()
        r.overrideredirect(True)
        r.attributes("-topmost", True)
        r.attributes("-alpha", ALPHA)
        r.geometry(f"{WIDTH}x{HEIGHT}+{(r.winfo_screenwidth() - WIDTH) // 2}+0")
        r.config(bg="#1b1b1b")
        inner = tk.Frame(r, bg="#1b1b1b")
        inner.pack(fill="both", expand=True, padx=4, pady=4)
        mk = lambda size, fg, bold: tk.Label(
            inner, bg="#1b1b1b", fg=fg, font=("Segoe UI", size, "bold" if bold else "normal"),
            anchor="center", wraplength=WIDTH - 20)
        self.l_head = mk(14, "#ffffff", True)
        self.l_action = mk(14, "#ffd34d", True)
        self.l_detail = mk(10, "#d0d0d0", False)
        self.l_next = mk(9, "#8a8a8a", False)
        for lbl in (self.l_head, self.l_action, self.l_detail, self.l_next):
            lbl.pack(fill="x")

        r.update_idletasks()
        self.hwnd = wintypes.HWND(self.user32.GetParent(r.winfo_id()))
        ex = self.user32.GetWindowLongW(self.hwnd, GWL_EXSTYLE)
        ex = (ex | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW
        self.user32.SetWindowLongW(self.hwnd, GWL_EXSTYLE, ex)

        self.tick()
        self.keep_on_top()

    def game_time(self):
        run = (time.monotonic() - self.run_from) * SPEED if self.run_from is not None else 0.0
        return self.acc + run

    def pressed(self, vk):
        down = bool(self.user32.GetAsyncKeyState(vk) & 0x8000)
        was, self.keys[vk] = self.keys.get(vk, False), down
        return down and not was

    def handle_keys(self):
        edges = {vk for vk in (VK_HOME, VK_PRIOR, VK_NEXT, VK_END, *COUNTERS) if self.pressed(vk)}
        held = lambda vk: self.user32.GetAsyncKeyState(vk) & 0x8000
        if VK_END in edges and held(VK_CONTROL):
            self.root.destroy()
            return False
        if VK_HOME in edges:
            if self.run_from is None:
                self.run_from = time.monotonic()
            else:
                self.acc, self.run_from = self.game_time(), None
        if VK_PRIOR in edges:
            self.acc += 10
        if VK_NEXT in edges:
            self.acc -= min(10, self.game_time())
        if VK_END in edges:
            self.acc, self.run_from, self.last_start = 0.0, None, None
        if held(VK_MENU):
            for vk in edges & COUNTERS.keys():
                self.counter, self.counter_until = COUNTERS[vk], time.monotonic() + COUNTER_SEC
                self.flash()
        return True

    def flash(self):
        self.flash_until = time.monotonic() + FLASH_MS / 1000
        self.root.config(bg="#ffcc00")
        self.root.after(FLASH_MS, lambda: self.root.config(bg="#1b1b1b"))

    def tick(self):
        if not self.handle_keys():
            return
        t = self.game_time()
        start, topic, action, detail, nxt = hint_at(t)
        if start != self.last_start:
            self.last_start = start
            if self.run_from is not None:
                winsound.PlaySound("SystemAsterisk", winsound.SND_ALIAS | winsound.SND_ASYNC)
                self.flash()
        paused = "" if self.run_from is not None else ("  (Home — старт)" if t == 0 else "  (пауза)")
        now = time.monotonic()
        left = self.counter_until - now
        if left > 0:
            name, text, *rest = self.counter
            head, action, detail = f"{fmt(t)}{paused}   КОНТРА: {name}", text, " · ".join([*rest, f"ще {int(left) + 1} с"])
        else:
            head = f"{fmt(t)}{paused}   {topic}"
        lines = (head, action, detail, f"Далі {fmt(nxt)}: {hint_at(nxt)[2]}")
        self.l_head.config(text=lines[0], fg="#ff7070" if left > 0 else "#ffffff")
        self.l_action.config(text=lines[1])
        self.l_detail.config(text=lines[2])
        self.l_next.config(text=lines[3])
        self.shared.publish(lines, (self.flash_until > now) | (left > 0) << 1)
        self.set_hidden(self.shared.dll_alive())
        self.root.after(30, self.tick)

    def set_hidden(self, hidden):
        # DLL малює в грі — своє вікно прибираємо, щоб не було дубля в borderless-режимі
        if hidden != self.hidden:
            self.hidden = hidden
            self.root.withdraw() if hidden else self.root.deiconify()

    def keep_on_top(self):
        # гра у windowed fullscreen іноді перекриває topmost — повторюємо щосекунди
        if not self.hidden:
            self.user32.SetWindowPos(self.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_FLAGS)
        self.root.after(1000, self.keep_on_top)


def self_check():
    assert hint_at(0)[2] == "4 пеони на золото"
    assert hint_at(24.9)[0] == 0 and hint_at(25)[0] == 25
    assert hint_at(700)[4] == 720
    assert hint_at(720)[1:3] == SCOUT[:2] and hint_at(900)[1:3] == SCOUT[:2]
    cycle = [hint_at(720 + 45 * k)[2] for k in range(1, 8) if k % 4]
    assert cycle[:4] == [m[1] for m in MACRO] and cycle[4] == MACRO[0][1]
    print("ok")


if __name__ == "__main__":
    if "--test" in sys.argv:
        self_check()
    else:
        # одна копія: DLL теж запускає оверлей, якщо він ще не працює
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.CreateMutexW(None, False, "Local\\OrcOverlayMutex")
        if ctypes.get_last_error() == 183:  # ERROR_ALREADY_EXISTS
            sys.exit()
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(1)  # чіткий текст на ноутах зі scaling
        except Exception:
            pass
        Overlay().root.mainloop()
