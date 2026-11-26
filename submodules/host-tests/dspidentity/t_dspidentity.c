/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The apitests' identity of the DSP's PCM in the device tree (modules/rostests/apitests/include/apitest_dspidentity.h, the header they compile): the audio child's instance path and the KS interface paths of its subdevices are the PCM's, in either case, with the audio filter device's prefix or without; another bus's, another device's and a longer device number are not; under ASan and UBSan
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <apitest_dspidentity.h>

static int Checks;
static int Failures;

#define CHECK_EQ(a, b) \
    do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        ++Checks; \
        if (_a != _b) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: %lld != %lld\n", __FILE__, __LINE__, #a, #b, _a, _b); \
            ++Failures; \
        } \
    } while (0)

/* The DSP's audio child as p4c4b2's serial log names it: INTCDSP\PCM&CTLR_VEN_8086&CTLR_DEV_7F50\00 */
static const struct
{
    const WCHAR *Text;
    BOOL IsThePcm;
    const char *What;
} Rows[] =
{
    { L"INTCDSP\\PCM&CTLR_VEN_8086&CTLR_DEV_7F50\\00", TRUE, "the audio child's instance path (p4c4b2)" },
    { L"intcdsp\\pcm&ctlr_ven_8086&ctlr_dev_7e28\\00", TRUE, "the Meteor Lake controller, lower case" },
    { L"\\\\?\\intcdsp#pcm&ctlr_ven_8086&ctlr_dev_7f50#00#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\\loopbackwave", TRUE, "a subdevice's KS interface path" },
    { L"{2}.\\\\?\\intcdsp#pcm&ctlr_ven_8086&ctlr_dev_7f50#00#{6994ad04-93ef-11d0-a3cc-00a0c9223196}", TRUE, "mmdevapi's filter device id" },
    { L"{2}.\\\\?\\INTCDSP#PCM&CTLR_VEN_8086&CTLR_DEV_7E28#00#{" L"65e8773d-8f56-11d0-a3b9-00a0c9223196}", TRUE, "upper case, the capture interface class" },
    { L"INTCDSP\\PCM&CTLR_VEN_8086&CTLR_DEV_7F50", TRUE, "the device id alone" },
    { L"HDAUDIO\\FUNC_01&VEN_1AF4&DEV_0022&SUBSYS_1AF40022&REV_1001\\4&12345678&0&0001", FALSE, "an HDA codec" },
    { L"PCI\\VEN_8086&DEV_7F50&SUBSYS_00000000&REV_01\\00&04d9fad45&0&00", FALSE, "the controller's PCI function" },
    { L"USB\\VID_046D&PID_A0F8&MI_00\\7&12345678&0&0000", FALSE, "a USB headset" },
    { L"SWD#MMDEVAPI#{0.0.0.00000000}.{12345678-1234-1234-1234-123456789abc}", FALSE, "a software device" },
    { L"INTCDSPX\\PCM&CTLR_VEN_8086&CTLR_DEV_7F50\\00", FALSE, "another enumerator" },
    { L"INTCDSP\\PCMX&CTLR_VEN_8086&CTLR_DEV_7F50\\00", FALSE, "another device id" },
    { L"INTCDSP\\PCM&CTLR_VEN_8086&CTLR_DEV_1234\\00", FALSE, "another device number" },
    { L"INTCDSP\\PCM&CTLR_VEN_8086&CTLR_DEV_7F505\\00", FALSE, "a device number one digit longer" },
    { L"INTCDSP\\PCM&CTLR_VEN_8086&CTLR_DEV_7F50X\\00", FALSE, "a device number with a letter after it" },
    { L"INTCDSP\\PCM&CTLR_VEN_8087&CTLR_DEV_7F50\\00", FALSE, "another vendor" },
    { L"\\\\?\\intcdsp#pcm&ctlr_ven_8086&ctlr_dev_7f50#00#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\\loopbackintopo", TRUE, "the capture topology's KS interface path" },
    { L"{2}.\\\\?\\intcdsp#pcm&ctlr_ven_8086&ctlr_dev_7f50#00#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\loopbackinwave", TRUE, "the capture filter device id" },
    { L"intcdsp#pcm&ctlr_ven_8086&ctlr_dev_7f50#00#{6994ad04-93ef-11d0-a3cc-00a0c9223196}", TRUE, "a KS interface path without the prefix" },
    { L"{2}.hdaudio#func_01&ven_1af4&dev_0022#4&12345678&0&0001#{6994ad04-93ef-11d0-a3cc-00a0c9223196}", FALSE, "the codec's filter device id" },
    { L"USB#VID_046D&PID_A0F8&MI_00#7&12345678&0&0000#{" L"6994ad04-93ef-11d0-a3cc-00a0c9223196}", FALSE, "the headset's KS interface path" },
    { L"INTCDSP#PCM&CTLR_VEN_8086&CTLR_DEV_7F50#00", TRUE, "the instance path with the interface separator" },
};

int
main(void)
{
    size_t I;

    for (I = 0; I < ARRAY_SIZE(Rows); ++I)
    {
        BOOL Got = DspPcmIdentity(Rows[I].Text);
        if ((Got != FALSE) != (Rows[I].IsThePcm != FALSE))
        {
            fprintf(stderr, "row %u, %s: %ls is %s the PCM\n", (unsigned)I, Rows[I].What, Rows[I].Text, Got ? "taken as" : "not taken as");
            ++Failures;
        }
        ++Checks;
    }
    printf("dspidentity: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
