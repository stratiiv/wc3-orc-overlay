/* Checks the forwarding: run in a folder holding ijl15.dll (ours) and ijl15_orig.dll (the game's).
 * Build with ORC_DEBUG to also see "d3d8 hook" in %TEMP%\orc_hints.log after HOOK_DELAY_MS. */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    static const char *names[] = {"ijlGetLibVersion", "ijlInit", "ijlFree", "ijlRead", "ijlWrite", "ijlErrorStr"};
    HMODULE m = LoadLibraryA("ijl15.dll");
    const void *(__stdcall *ver)(void);
    int i;
    if (!m) { printf("load failed: %lu\n", GetLastError()); return 1; }
    for (i = 0; i < 6; i++) {
        void *f = GetProcAddress(m, names[i]);
        HMODULE owner = NULL;
        char path[MAX_PATH] = "";
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, f, &owner);
        GetModuleFileNameA(owner, path, MAX_PATH);
        printf("%s -> %s\n", names[i], path);
        if (!f || !strstr(path, "ijl15_orig")) return 1;
    }
    ver = (void *)GetProcAddress(m, "ijlGetLibVersion");
    if (!ver()) return 1;
    Sleep(14000);  /* let the hook thread run */
    puts("OK");
    return 0;
}
