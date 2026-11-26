#define STANDALONE
#include <apitest.h>

extern void func_Material(void);

const struct test winetest_testlist[] =
{
    { "Material", func_Material },
    { 0, 0 }
};
