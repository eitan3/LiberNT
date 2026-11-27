#define STANDALONE
#include <apitest.h>

extern void func_Anim(void);
extern void func_Color(void);
extern void func_Input(void);
extern void func_Layout(void);
extern void func_Painter(void);
extern void func_Palette(void);
extern void func_Tokens(void);

const struct test winetest_testlist[] =
{
    { "Anim", func_Anim },
    { "Color", func_Color },
    { "Input", func_Input },
    { "Layout", func_Layout },
    { "Painter", func_Painter },
    { "Palette", func_Palette },
    { "Tokens", func_Tokens },
    { 0, 0 }
};
