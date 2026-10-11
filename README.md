# PS3 DualSense & DualShock 4 Controller Plugin (Enhanced DS45Pad)

[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Platform](https://img.shields.io/badge/Platform-PS3_CFW_%7C_PS3HEN-orange.svg)]()
[![Hardware Verified](https://img.shields.io/badge/Status-Verified_Working_on_Hardware-brightgreen.svg)]()

A background VSH PRX/SPRX plugin for the **PlayStation 3 (CFW and PS3HEN)** that adds native USB support for **PlayStation 5 DualSense** (`CFI-ZCT1`), **DualSense Edge**, and **DualShock 4** controllers — featuring full, native support for the physical **PlayStation (PS / Home) button**!

---

## 🌟 Features

* **Physical PS / Home Button**: Pressing the physical PlayStation button on your DualSense natively opens the XMB menu and in-game pause/quit screen (no weird button combos needed).
* **Native USB Input**: Direct, low-latency USB connection with full digital and analog button support.
* **DualSense & DualShock 4**: Supports official Sony PS5 DualSense (`054c:0ce6`), DualSense Edge (`054c:0df2`), and DualShock 4 v1 & v2 (`054c:05c4`, `054c:09cc`).
* **Extensible Device List**: Easily add third-party controllers or clone gamepads by editing `ds45pad_devices.txt` without recompiling.
* **CFW & PS3HEN Compatible**: Signed with 3.55 retail keys (`Revision 0A`, VSH Auth-ID `1070000052000001`), ensuring 100% stable booting without console freezes.
* **Plug & Play**: Pre-compiled and ready to install directly from the `ps3_ready/` directory.

---

## 🔬 The Problem & Technical Breakthrough

### The PS3 Firmware Limitation
When a DualSense controller is plugged into a PS3 via USB, the console kernel (`sys_hid` in LV2) detects it only as a basic generic USB HID gamepad (`device_type = 1`, `capability = 0x00`). 
* Standard face buttons and analog sticks work, but **the physical PS button does nothing**.
* DualSense USB HID Input Report `0x01` places the physical PS button in **Byte 10, Bit 0** (`0x01`).
* Sony's generic driver only parses basic button bitfields and completely discards Byte 10.

### The Upstream DS45Pad Bug
Previous attempts to intercept the controller in user space (such as the original DS45Pad) failed because:
1. **Duplicate LDD Registration (`0x80110006`)**: In `usb.c`, `cellUsbdRegisterExtraLdd()` was registering all supported devices in a loop using the same static driver name (`"ds45pad"`). When registering the second device entry (index 1), the kernel returned `CELL_USBD_ERROR_LDD_ALREADY_REGISTERED` (`0x80110006`), immediately calling `usb_shutdown()`. Since DualSense was at index 3, it was never registered in the USB driver subsystem.
2. **Input Queue Stealing**: Running diagnostic loops with `cellPadGetData` on Port 0 created single-consumer queue contention with multiMAN and VSH, resulting in dropped or repeating inputs.

### The Solution in this Fork
1. **Dynamic Unique LDD Names**: Each device entry is assigned a unique driver name (`"ds45_0"`, `"ds45_1"`, etc.). All devices, including the DualSense, register cleanly with the PS3 USBD driver.
2. **Direct HID Parsing**: The plugin's USB interrupt handler catches the raw 64-byte USB report, decodes Byte 10 Bit 0, and injects the genuine `CELL_PAD_CTRL_LDD_PS` event to the system.
3. **Optimized Input Loop**: Diagnostics are disabled by default (`DIAG = 0`), eliminating input queue competition and providing fluid, native controls.

---

## 🚀 Installation Guide (PS3HEN & CFW)

Pre-compiled, hardware-verified binaries are included in the [`ps3_ready/`](file:///ps3_ready/) folder.

### Step 1: Copy Files to the Console
Using **FTP** (via multiMAN or webMAN MOD) or a **FAT32 USB drive**:

1. Create the directory on your PS3 internal hard drive:
   ```text
   /dev_hdd0/plugins/ds45pad/
   ```
2. Copy the following files into that directory:
   * `ps3_ready/plugins/ds45pad/ds45pad_vsh.sprx` $\rightarrow$ `/dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx`
   * `ps3_ready/plugins/ds45pad/ds45pad.txt` $\rightarrow$ `/dev_hdd0/plugins/ds45pad/ds45pad.txt`
   * `ps3_ready/plugins/ds45pad/ds45pad_devices.txt` $\rightarrow$ `/dev_hdd0/plugins/ds45pad/ds45pad_devices.txt`

### Step 2: Configure Plugin Autoload
On `/dev_hdd0/`:
* Open or create `/dev_hdd0/boot_plugins.txt` (or `/dev_hdd0/boot_plugins_nocobra.txt` depending on your setup).
* Add this line:
  ```text
  /dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx
  ```
  *(Make sure to leave an empty newline at the end of the file).*

### Step 3: Reboot and Play
1. Restart your PS3.
2. If running **PS3HEN**, click **Enable HEN**. A notification popup will confirm the plugin is loaded.
3. Connect your DualSense controller via a USB cable.
4. Press the **PS button** — the XMB menu will respond instantly!

---

## ⚙️ Configuration (`ds45pad.txt`)

The plugin behavior can be customized by editing `/dev_hdd0/plugins/ds45pad/ds45pad.txt`:

| Option | Recommended | Description |
|---|---|---|
| `ENABLE` | `1` | `1` = Enable plugin, `0` = Disable plugin. |
| `MODE` | `USB` | Transport mode (`USB`). |
| `MAX_PADS` | `1` | Number of simultaneous controllers handled. |
| `POLL_MS` | `10` | Input polling rate in milliseconds. |
| `PS_ONLY` | `1` | Directly injects the physical PS button from USB HID Byte 10 Bit 0. |
| `INSERT_INTO_GAME` | `1` | **Crucial!** Injects pad inputs directly into games via Syscall 573. Required for START (Options) and SELECT (Create) to work properly in games. |
| `ALLOW_INSERT_COMBO` | `1` | Allows toggling in-game injection using `CREATE + OPTIONS + R3`. |
| `DIAG` | `0` | **Keep at 0!** Setting to 1 enables diagnostic logging that can steal input packets. |
| `NOTIFY` | `1` | `1` = Show on-screen notification in XMB on controller attach. |
| `STICK_CALIBRATION` | `OFF` | Stick response curve calibration (`OFF` or `ON`). |

---

## 🎮 Adding Third-Party / Clone Controllers

To add custom or third-party controllers, edit `/dev_hdd0/plugins/ds45pad/ds45pad_devices.txt`:

```text
# Format: VID, PID, MODEL
# MODEL can be DUALSENSE or DS4
0x054c, 0x0ce6, DUALSENSE
```

---

## 🛡️ Recovery & Failsafe

If the console ever fails to boot or freezes due to plugin conflicts:
1. **USB Failsafe**: Insert a USB drive formatted in FAT32 with an empty file named `boot_plugins.txt` in the root (`/dev_usb000/boot_plugins.txt`). CFW/HEN will prioritize the USB file and skip HDD plugins.
2. **FTP Recovery**: Boot into safe mode or multiMAN, connect via FTP, and rename or delete `/dev_hdd0/boot_plugins.txt`.

---

## 🛠️ Building from Source

### Prerequisites
* **Sony Cell SDK PPU Toolchain** (`ppu-lv2-gcc`)
* **scetool** with Retail 3.55 keys (`Revision 0A`)
* Windows (PowerShell) or Linux (Bash + Wine / native toolchain)

> [!NOTE]
> **Legal Notice**: Due to Sony Interactive Entertainment copyright policies, proprietary Cell SDK headers, libraries (`target/ppu/lib/*`), and encryption keys are **not** bundled in this repository. You must provide your own SDK and keys to compile from source.

### Building on Windows (PowerShell)
Set environment variables pointing to your SDK and tools directories:
```powershell
$env:CELL_SDK = "C:\path\to\cell_sdk"
$env:DS45_TOOLS = "C:\path\to\scetool_and_keys"
cd src
.\build.ps1 -Variant vsh
```

### Building on Linux / Git Bash
```bash
export CELL_SDK="/path/to/cell_sdk"
export DS45_TOOLS="/path/to/scetool_and_keys"
cd src
./build.sh
```

The compiled and signed plugin will be output as `src/ds45pad_vsh.sprx`.

---

## 📜 Credits & Acknowledgments

This project is built upon the incredible work of the PS3 homebrew development community:

* **[Dobridp](https://github.com/Dobridp)** — Author of the original [DS45Pad](https://github.com/Dobridp/DS45Pad) project.
* **[OsirisX](https://github.com/OsirisX)** — Author of [PS3xPAD](https://github.com/OsirisX/PS3xPAD), the foundation for custom controller LDD injection on PS3.
* **[aldostools](https://github.com/aldostools)** — For webMAN MOD, PRX signing guides, and invaluable documentation on the PS3 VSH environment.

---

## ⚖️ License

This project is free and open-source software licensed under the **GNU General Public License v2 (GPL-2.0)**. See the [LICENSE](LICENSE) file for full details.
