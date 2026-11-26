/* The types and the helper apitest_dspidentity.h takes from the apitests' apitest.h, on the host: a wide character is the host's, the literals are the test's */
#ifndef _APITEST_H
#define _APITEST_H
#include <stddef.h>
#include <wchar.h>
#include <wctype.h>

typedef int BOOL;
typedef wchar_t WCHAR;

/* windef.h's */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* msvcrt's: the first Count wide characters, case-insensitively, 0 when they are the same */
static __inline int _wcsnicmp(const WCHAR *Left, const WCHAR *Right, size_t Count)
{
    size_t I;

    for (I = 0; I < Count; I++)
    {
        wint_t A = towlower(Left[I]), B = towlower(Right[I]);
        if (A != B || !A)
            return A < B ? -1 : 1;
    }
    return 0;
}

#endif
