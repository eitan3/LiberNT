#define STANDALONE
#include <apitest.h>

extern void func_Audio(void);
extern void func_Calendar(void);
extern void func_Dock(void);
extern void func_Power(void);
extern void func_StartModel(void);
extern void func_Tray(void);
extern void func_Widgets(void);

const struct test winetest_testlist[] =
{
    { "Audio", func_Audio },
    { "Calendar", func_Calendar },
    { "Dock", func_Dock },
    { "Power", func_Power },
    { "StartModel", func_StartModel },
    { "Tray", func_Tray },
    { "Widgets", func_Widgets },
    { 0, 0 }
};
