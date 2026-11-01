/* Перевірка /help: i686-w64-mingw32-gcc -o test_help.exe test_help.c -lgdi32 -luser32 && test_help.exe */
#include <assert.h>
#include "orc.c"
int hook_d3d8(HWND w) { (void)w; return 1; }
int hook_d3d9(HWND w) { (void)w; return 1; }

static int type_keys(const BYTE *k, int n)
{
    int i;
    help_key(VK_RETURN, 0);  /* Enter відкриває чат, він ще закритий */
    for (i = 0; i < n; i++) help_key(k[i], 1);
    return help_key(VK_RETURN, 1);
}

int main(void)
{
    static const BYTE ok[] = {VK_OEM_2, 'H', 'E', 'L', 'P'};
    static const BYTE ru[] = {VK_SHIFT, VK_OEM_5, 'H', 'E', 'L', 'P'};
    static const BYTE fix[] = {VK_OEM_2, 'H', 'X', VK_BACK, 'E', 'L', 'P'};
    static const BYTE bad[] = {VK_OEM_2, 'H', 'E', 'L', 'P', 'P'};
    static const BYTE word[] = {'H', 'E', 'L', 'P'};
    assert(type_keys(ok, 5));
    assert(type_keys(ru, 6));
    assert(type_keys(fix, 7));
    assert(!type_keys(bad, 6));
    assert(!type_keys(word, 4));
    help_key(VK_OEM_2, 1); help_key(VK_ESCAPE, 0);  /* Esc закрив чат: набране забуто */
    assert(!help_key(VK_RETURN, 1));
    return 0;
}
