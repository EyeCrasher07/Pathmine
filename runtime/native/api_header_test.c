#include <stddef.h>
#include <stdio.h>

#include <remix/remix_c.h>

_Static_assert(sizeof(void*) == 8, "The native runtime requires a 64-bit process");
_Static_assert(sizeof(remixapi_Interface) == 176, "The original 0.6 API table layout changed");
_Static_assert(offsetof(remixapi_Interface, Startup) == 152, "The Startup slot moved");
_Static_assert(offsetof(remixapi_Interface, Present) == 160, "The Present slot moved");
_Static_assert(sizeof(remixapi_HWND) == 8, "The opaque window handle layout changed");

int main(void) {
  puts("PASS original Remix 0.6 C API layout in a native Linux translation unit");
  return 0;
}
