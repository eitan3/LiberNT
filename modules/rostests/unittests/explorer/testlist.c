#define STANDALONE
#include <apitest.h>

extern void func_Calendar(void);
extern void func_Power(void);

const struct test winetest_testlist[] =
{
    { "Calendar", func_Calendar },
    { "Power", func_Power },
    { 0, 0 }
};
