#define STANDALONE
#include <apitest.h>

extern void func_Store(void);

const struct test winetest_testlist[] =
{
    { "Store", func_Store },
    { 0, 0 }
};
