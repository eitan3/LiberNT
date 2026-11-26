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
* The DSP driver itself, `intcdsp.sys` (`intcdsp.inf`): probe-only by default, it logs the controller's BAR4, processing-capability and NHLT facts and reads and parses the firmware image once, keeping the image and the code-loader buffer for the device. With `BootDsp=1` (REG_DWORD under `HKLM\SYSTEM\CurrentControlSet\Services\intcdsp\Settings`) it boots the DSP of Arrow Lake-S/HX (`8086:7F50`, `sof-arl-s.ri`) and of Meteor Lake (`8086:7E28`, `sof-mtl.ri`), the two controllers with a register map (the platform table of `sdk/lib/drivers/fwload`), through the loader library there: ROM initialisation over a code-loader stream, the firmware's FW_READY and its configuration reply, every wait polled and bounded, the DSP's IPC interrupts masked throughout. The other four controllers the INF lists install it probe-only.
* After the boot: the IPC4 transport (`sdk/lib/drivers/audio/sofipc`: one request at a time, a budget per message class, a link that faults on a timeout or a firmware status; `SelfTest=1` runs its self-test), the topology (a `.tplg` selected by the `Topology` override, the DMI quirk table or the controller's `_DSD` names, parsed by `softplg` into a model of the PCM's pipelines and widgets, `intcdsp/topology.c`; `firmware/sof-ipc4-loopback.tplg`, the loopback test topology, is shipped), and the stream manager (`intcdsp/stream.c`): one stream object per open render or capture stream of a PCM, the firmware messages of each transition in one order, the cyclic buffer and the stream through the bus's stream members, a generation in every handle so a handle of an earlier boot names nothing, every transition under one lock, and a release of everything at a power-down; `SelfTest=2` runs the loopback self-test over it.
* The endpoint driver `dsppcm.sys` (`drivers/wdm/audio/drivers/dsppcm`): one `INTCDSP\PCM` child per PCM of the topology, which it asks for the audio interface `GUID_INTELDSP_AUDIO_INTERFACE` on its own PDO; one topology and one WaveRT subdevice per endpoint (render and capture), streaming through the DSP over the stream manager, with the position read from the controller's DMA-position register. Its INF registers the device interfaces of the loopback topology's PCM `Loopback` only.

Not implemented:

* A production topology: only the loopback test topology is shipped, and a PCM of another name gets no device interface (its subdevices need names independent of the PCM; board task). A pin's stream is not re-created after a resume: the power-down releases every stream, and the pin's handle is refused from then on (`STATUS_INVALID_HANDLE`) until the pin closes and opens afresh (P5). The SoundWire DisCo property names wait for MIPI's public specification; `disco` walks a `_DSD` reply generically and hands the driver the controller's topology names.
* Runtime power management of a booted DSP: it stays in D0 until the device powers down, and boots again at the next D0 entry.
* SoundWire: no bus, controller or codec driver. The `SoundWire*.h` headers in the tree are interface definitions only.
* The Audio Class eXtension (ACX): headers only.
* Codec fixups for a codec that sits behind the DSP. They are decided per codec after a run shows the codec's pin configuration defaults.

The `CSAUDIO\ADSP` child is created only for an Intel controller of the ACE era (`8086:7E28`, `7F50`, `7728`, `A828`, `E428`, `E328`) whose second memory BAR is the PCI BAR4 resource and whose processing capabilities answer. `intcdsp.inf` matches that child; every other controller gets none.
