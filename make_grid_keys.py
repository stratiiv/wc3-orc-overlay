"""Builds a Reforged-style grid CustomKeys.txt for Warcraft III 1.26 from the game's own data.

Every command-card button gets the hotkey of its position:  Q W E R / A S D F / Z X C V.
Positions come from Units\\*Func.txt inside the game's MPQ archives; tooltips and the rest
come from CustomKeysSample.txt that ships with the game (so the text stays in the game's language).

    python make_grid_keys.py "C:\\Program Files (x86)\\Warcraft 3" [out.txt]

Then in the game: Options -> Gameplay -> Custom Keyboard Shortcuts.
"""
import re
import struct
import sys
import zlib
import bz2
from pathlib import Path

GRID = ("QWER", "ASDF", "ZXCV")
ARCHIVES = ("War3Patch.mpq", "War3xlocal.mpq", "War3x.mpq", "war3.mpq")  # first one wins
RACES = ("Human", "Orc", "NightElf", "Undead", "Neutral", "Common", "Item", "Campaign")
POS_TO_KEY = {"Buttonpos": "Hotkey", "Researchbuttonpos": "Researchhotkey", "Unbuttonpos": "Unhotkey"}


# --- minimal MPQ v1 reader (hash/block tables, zlib/bzip2 sectors, encryption) ---

def _crypt_table():
    seed, table = 0x00100001, [0] * 0x500
    for i in range(0x100):
        for j in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            hi = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            table[i + j * 0x100] = hi | (seed & 0xFFFF)
    return table


CRYPT = _crypt_table()


def mpq_hash(name, kind):
    s1, s2 = 0x7FED7FED, 0xEEEEEEEE
    for ch in name.upper().encode("latin-1"):
        v = CRYPT[(kind << 8) + ch]
        s1 = (v ^ (s1 + s2)) & 0xFFFFFFFF
        s2 = (ch + s1 + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
    return s1


def decrypt(data, key):
    n = len(data) // 4
    words = struct.unpack(f"<{n}I", data[: n * 4])
    out, s2 = [], 0xEEEEEEEE
    for w in words:
        s2 = (s2 + CRYPT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        v = w ^ ((key + s2) & 0xFFFFFFFF)
        key = ((((~key) << 21) + 0x11111111) | (key >> 11)) & 0xFFFFFFFF
        s2 = (v + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
        out.append(v)
    return struct.pack(f"<{n}I", *out) + data[n * 4:]


class MPQ:
    def __init__(self, path):
        self.f = open(path, "rb")
        data = self.f.read(0x100000)
        self.base = next(o for o in range(0, len(data), 512) if data[o:o + 4] == b"MPQ\x1a")
        (_, _, _, _, shift, hpos, bpos, hcount, bcount) = struct.unpack_from("<4sIIHHIIII", data, self.base)
        self.sector = 512 << shift
        self.hashes = list(struct.iter_unpack("<IIHHI", decrypt(self._read(hpos, hcount * 16), mpq_hash("(hash table)", 3))))
        self.blocks = list(struct.iter_unpack("<IIII", decrypt(self._read(bpos, bcount * 16), mpq_hash("(block table)", 3))))

    def _read(self, pos, size):
        self.f.seek(self.base + pos)
        return self.f.read(size)

    def _find(self, name):
        a, b, n = mpq_hash(name, 1), mpq_hash(name, 2), len(self.hashes)
        i, found = mpq_hash(name, 0) % n, []
        for _ in range(n):
            h1, h2, locale, _, block = self.hashes[i]
            if block == 0xFFFFFFFF:
                break
            if (h1, h2) == (a, b) and block < len(self.blocks):
                found.append((locale != 0, block))  # neutral locale first
            i = (i + 1) % n
        return min(found)[1] if found else None

    @staticmethod
    def _inflate(chunk):
        method, body = chunk[0], chunk[1:]
        if method == 0x02:
            return zlib.decompress(body)
        if method == 0x10:
            return bz2.decompress(body)
        raise ValueError(f"unsupported MPQ compression 0x{method:02x}")

    def read(self, name):
        block = self._find(name)
        if block is None:
            return None
        pos, csize, fsize, flags = self.blocks[block]
        if not flags & 0x80000000:
            return None
        if flags & 0x100:
            raise ValueError("imploded MPQ files are not supported")
        raw = self._read(pos, csize)
        key = None
        if flags & 0x10000:
            key = mpq_hash(name.replace("/", "\\").split("\\")[-1], 3)
            if flags & 0x20000:
                key = ((key + pos) ^ fsize) & 0xFFFFFFFF
        if flags & 0x01000000:  # single unit
            data = decrypt(raw, key) if key is not None else raw
            return self._inflate(data) if flags & 0x200 and csize < fsize else data
        count = (fsize + self.sector - 1) // self.sector
        if flags & 0x200:
            table = raw[: (count + 1) * 4]
            if key is not None:
                table = decrypt(table, (key - 1) & 0xFFFFFFFF)
            offs = struct.unpack(f"<{count + 1}I", table)
        else:
            offs = [min(i * self.sector, csize) for i in range(count + 1)]
        out = bytearray()
        for i in range(count):
            chunk = raw[offs[i]:offs[i + 1]]
            if key is not None:
                chunk = decrypt(chunk, (key + i) & 0xFFFFFFFF)
            want = min(self.sector, fsize - i * self.sector)
            out += self._inflate(chunk) if flags & 0x200 and len(chunk) < want else chunk
        return bytes(out)


# --- grid hotkeys ---

def parse_ini(text):
    """{section: {key: value}} for Blizzard's ini-like txt files."""
    sections, cur = {}, None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            cur = sections.setdefault(line[1:-1], {})
        elif cur is not None and "=" in line and not line.startswith("//"):
            k, v = line.split("=", 1)
            cur[k.strip()] = v.strip()
    return sections


def button_positions(game_dir):
    archives = [MPQ(Path(game_dir) / a) for a in ARCHIVES if (Path(game_dir) / a).exists()]
    names = [f"Units\\{r}{kind}Func.txt" for r in RACES for kind in ("Ability", "Unit", "Upgrade")]
    names.append("Units\\CommandFunc.txt")
    pos = {}
    for name in names:
        data = next((d for d in (m.read(name) for m in archives) if d), None)
        for sec, keys in parse_ini(data.decode("utf-8", "replace") if data else "").items():
            for k, v in keys.items():
                if k.lower() in (p.lower() for p in POS_TO_KEY):
                    pos.setdefault(sec, {})[next(p for p in POS_TO_KEY if p.lower() == k.lower())] = v
    return pos


def grid_key(pos):
    try:
        x, y = (int(v) for v in pos.split(",")[:2])
        return GRID[y][x]
    except (ValueError, IndexError):
        return None


def regrid(sample_text, positions):
    """Rewrites Hotkey/Researchhotkey/Unhotkey lines of CustomKeysSample.txt by button position."""
    out, sec, changed = [], None, 0
    for line in sample_text.splitlines():
        m = re.match(r"\[(.+)\]\s*$", line)
        if m:
            sec = m.group(1)
        key, _, val = line.partition("=")
        pos_key = next((p for p, h in POS_TO_KEY.items() if h == key.strip()), None)
        p = positions.get(sec, {})
        pos = p.get(pos_key) or (p.get("Buttonpos") if pos_key == "Unbuttonpos" else None)
        new = grid_key(pos) if pos and pos_key else None
        levels = val.strip().split(",")  # upgrades list one key per level: "M,M,M"
        if new and all(len(v) == 1 for v in levels):  # leave special keys (512 = Esc) alone
            line, changed = f"{key}={','.join([new] * len(levels))}", changed + 1
        out.append(line)
    text = "\n".join(out) + "\n"
    return text, changed


def fix_tips(text):
    """Highlighted hotkey letter in tooltips follows the new hotkey of the same section."""
    out, keys = [], {}
    blocks = re.split(r"(?m)^(?=\[)", text)
    for block in blocks:
        hk = re.search(r"(?m)^Hotkey=(\w)(?:,\w)*$", block)
        rk = re.search(r"(?m)^Researchhotkey=(\w)(?:,\w)*$", block)
        uk = re.search(r"(?m)^Unhotkey=(\w)(?:,\w)*$", block)

        def sub(field, m):
            nonlocal block
            if m:
                block = re.sub(rf'(?m)^("?{field}=.*)$',  # hero skills: "Tip=""..."",""..."""
                               lambda t: re.sub(r"\|cffffcc00\w\|r", f"|cffffcc00{m.group(1)}|r", t.group(1)), block)

        sub("Tip", hk)
        sub("Researchtip", rk)
        sub("Untip", uk)
        sub("Awakentip", hk)
        sub("Revivetip", hk)
        out.append(block)
    return "".join(out)


def main():
    game = Path(sys.argv[1] if len(sys.argv) > 1 else r"C:\Program Files (x86)\Warcraft 3")
    dest = Path(sys.argv[2]) if len(sys.argv) > 2 else game / "CustomKeys.txt"
    sample = (game / "CustomKeysSample.txt").read_text(encoding="utf-8-sig")
    text, changed = regrid(sample, button_positions(game))
    dest.write_text(fix_tips(text), encoding="utf-8-sig")
    print(f"{dest}: {changed} hotkeys set to grid")


def self_check():
    assert grid_key("0,0") == "Q" and grid_key("3,2") == "V" and grid_key("1,1") == "S"
    assert grid_key("0,-11") is None
    sample = ("[obar]\nTip=Build |cffffcc00B|rarracks\nHotkey=B\n[CmdCancel]\nHotkey=512\n"
              "[Rome]\nTip=Iron (|cffffcc00M|r),Steel (|cffffcc00M|r)\nHotkey=M,M\n"
              '[AOhx]\n"Tip=""Hex (|cffffcc00X|r)"""\nHotkey=X\n')
    text, n = regrid(sample, {"obar": {"Buttonpos": "1,0"}, "CmdCancel": {"Buttonpos": "3,2"},
                              "Rome": {"Buttonpos": "0,0"}, "AOhx": {"Buttonpos": "3,2"}})
    text = fix_tips(text)
    assert "Hotkey=W\n" in text and "|cffffcc00W|r" in text and "Hotkey=512" in text and n == 3
    assert "Hotkey=Q,Q" in text and "Steel (|cffffcc00Q|r)" in text and "Hex (|cffffcc00V|r)" in text
    print("ok")


if __name__ == "__main__":
    self_check() if "--test" in sys.argv else main()
