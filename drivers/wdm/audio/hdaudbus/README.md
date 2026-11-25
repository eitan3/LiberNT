HD Audio Bus Driver for Intel Skylake and newer

* Exposes HD Audio devices
	* Compatible with VMWare, QEMU, and Intel Skylake / Kaby Lake / Apollo Lake / Gemini Lake / Comet Lake / Tiger Lake / Jasper Lake / Alder Lake(-N) / Raptor Lake / Arrow Lake-S/HX
	* Compatible with Windows default codec driver (QEMU / VMWare)
	* Compatible with Intel Display Audio driver (Intel Lakes, and Intel Haswell / Broadwell)
	* Compatible with Realtek ALC283 driver (Intel Haswell / Broadwell)
   	* Compatible with NVIDIA RTX 30-40 Series (HDMI Audio)
   	* Compatible with Asus TUF Gaming X570 Plus (AMD Ryzen 5000)
	* Compatible with Asus ROG Strix B850-A Gaming Wifi (AMD Ryzen 9000)
* Support accessing AudioDSP on Intel platforms
* Compatible with csaudiosstavs and csaudiointcsof
* Working Sleep / Wake
* Compatible with Windows 10 / 11 64-bit

## Intel Smart Sound Technology (SST / cAVS DSP): what is implemented and what is not

The bus driver finds out what a controller offers and says so; it never boots the DSP. The DSP driver of the child it creates is `drivers/wdm/audio/intcdsp` (`intcdsp.sys`), which logs the controller's facts at start and boots the DSP with Sound Open Firmware when its `BootDsp` value asks for it.

Implemented:

* Binding to the Intel SST controllers: the generic `PCI\CC_0403` line and the explicit IDs in `hdaudbus.inf`, among them Arrow Lake-S/HX `8086:7F50` (800-series ACE, class 0403 and class 0401).
* The HD Audio link behind the DSP is enumerated like any other. A codec that answers on it gets its `HDAUDIO\` child and works with the codec function driver (`hdaudio.sys`) while the DSP stays off.
* Finding the NHLT table through the controller's `_DSM`, mapping it and summarising it: every endpoint with its device config, its formats and its devices. The checked build prints the table, every endpoint and the count per link type (`NHLT table`, `NHLT endpoint` and `NHLT links` lines).
* The audio path the controller offers, written as `DspAudioPath` under the `Settings` key of the device and printed as the `audio path` line: `None`, `LegacyHdaOnly` (a codec, no DSP), `DspOptional` (a codec and the DSP) or `DspRequired` (the DSP with endpoints that are not on the HD Audio link, and no codec). The Sound control panel shows a message instead of an empty playback list when the path is `DspRequired`. Two things override that evidence: a per-machine DMI quirk table in `policy.c` (empty until a machine is verified on LiberNT; the checked build prints the machine's SMBIOS strings as the `DMI` line), and a REG_SZ `DspAudioPathOverride` value with one of the four path names under the same `Settings` key, which is the analogue of Linux's `snd_intel_dspcfg.dsp_driver`.
* The `CSAUDIO\ADSP` child device and its `ADSP_BUS_INTERFACE`, which hands a DSP driver the controller registers, the DSP registers, the NHLT table, HD Audio streams with their buffer descriptors, and the bus driver's interrupt lock. Version 2 of the interface also reports whether the controller addresses 64 bits of DMA, which bounds the DSP driver's firmware buffer; a bus of version 1 still binds and gets the range below 4 GiB.
* The DSP driver itself, `intcdsp.sys` (`intcdsp.inf`): probe-only by default, it logs the controller's BAR4, processing-capability and NHLT facts and reads and parses the firmware image once. With `BootDsp=1` (REG_DWORD under `HKLM\SYSTEM\CurrentControlSet\Services\intcdsp\Settings`) it boots the DSP of Arrow Lake-S/HX (`8086:7F50`, the one controller with a register map) with `sof-arl-s.ri` through the loader library `sdk/lib/drivers/fwload`: ROM initialisation over a code-loader stream, the firmware's FW_READY and its configuration reply, every wait polled and bounded, the DSP's IPC interrupts masked throughout. The other five controllers the INF lists install it probe-only.

Not implemented:

* Anything beyond that boot: no IPC transport for streams or pipelines (the IPC4 codec `sofipc4` carries the one configuration message of the boot and is tested on its own), no topology loading (no `.tplg` is packaged; the `*-tplg` names of the controller's `_DSD` are read and unused), and no audio stream that runs through the DSP. The parser libraries under `sdk/lib/drivers/audio/` (`nhlt`, `sofipc4`, `disco`, `softplg`) are built and tested on their own; no driver uses `disco` or `softplg` yet. `softplg` walks an ALSA SoC topology binary into its blocks, elements, controls and vendor tuples without mapping them onto firmware pipelines. `disco` walks a `_DSD` reply generically; the SoundWire DisCo property names wait for MIPI's public specification.
* Runtime power management of a booted DSP: it stays in D0 until the device powers down, and boots again at the next D0 entry.
* SoundWire: no bus, controller or codec driver. The `SoundWire*.h` headers in the tree are interface definitions only.
* The Audio Class eXtension (ACX): headers only.
* Codec fixups for a codec that sits behind the DSP. They are decided per codec after a run shows the codec's pin configuration defaults.

The `CSAUDIO\ADSP` child is created only for an Intel controller of the ACE era (`8086:7E28`, `7F50`, `7728`, `A828`, `E428`, `E328`) whose second memory BAR is the PCI BAR4 resource and whose processing capabilities answer. `intcdsp.inf` matches that child; every other controller gets none.
