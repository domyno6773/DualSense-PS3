# PS3 DualSense & DualShock 4 Controller Plugin (Enhanced DS45Pad)

[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Platform](https://img.shields.io/badge/Platform-PS3_CFW_%7C_PS3HEN-orange.svg)]()
[![Tested Firmware](https://img.shields.io/badge/Tested%20on-Firmware%204.93%20(PS3HEN)-success.svg)]()
[![Hardware Verified](https://img.shields.io/badge/Status-Verified_Working_on_Hardware-brightgreen.svg)]()

A high-performance background VSH PRX/SPRX plugin for the **PlayStation 3 (CFW and PS3HEN)** providing native **USB and internal Bluetooth** support for the **PlayStation 5 DualSense** (`CFI-ZCT1`), **DualSense Edge**, and **DualShock 4** controllers — featuring full, native support for the physical **PlayStation (PS / Home) button** with **zero button combinations**!

> [!WARNING]
> ### ⚠️ Firmware Compatibility Notice
> **All development, kernel offsets, and real-hardware testing have been performed exclusively on Firmware 4.93 (PS3HEN 4.93).**
> Compatibility with older or different firmware versions (e.g., 4.80 – 4.92, or various other CFW/HEN revisions) is **not guaranteed and has not been tested**. Because the Bluetooth physical PS button recovery relies on direct LV2 kernel Table of Contents (TOC) offsets and internal memory structures (`bt_work`), these structures may differ across firmware releases. Use on other firmware versions at your own discretion.

---

## 🌟 Key Features

* **Physical PS / Home Button**: Pressing the physical PlayStation logo button natively opens the in-game XMB overlay and quit menu on **both USB and internal Bluetooth** (no button shortcuts or combos needed).
* **Native Internal Bluetooth**: Connect your DualSense directly to the console's built-in Bluetooth radio without requiring any external USB dongles or adapters.
* **Full In-Game PS Menu Navigation**: Over Bluetooth, the PS menu is fully interactive — navigate with the D-Pad/analog sticks, confirm with **Cross**, cancel/close with **Circle**, and close with the **PS button**.
* **Seamless Auto Mode (`MODE = AUTO`)**: Plug in via USB for ultra-low latency charging & play, or unplug to seamlessly continue playing over wireless internal Bluetooth.
* **Clean In-Game Input**: In games and apps (multiMAN, retro emulators, PS3 titles), input operates cleanly as Player 1 without ghost controllers or duplicate player conflicts.
* **DualSense & DualShock 4**: Supports official Sony PS5 DualSense (`054c:0ce6`), DualSense Edge (`054c:0df2`), and DualShock 4 v1 & v2 (`054c:05c4`, `054c:09cc`).
* **Firmware 4.93 Verified**: Thoroughly tested and verified on **PS3 Super Slim running PS3HEN 4.93** (untested on older firmware versions).
* **CFW & PS3HEN Compatible**: Signed with 3.55 retail keys (`Revision 0A`, VSH Auth-ID `1070000052000001`), ensuring 100% stable booting without console freezes.
* **Plug & Play**: Pre-compiled and ready to install directly from the `ps3_ready/` directory.

---

## 🔬 The Technical Breakthrough

### The PS3 Bluetooth Firmware Limitation
When a DualSense or DualShock 4 pairs with the PS3's internal Bluetooth radio:
1. The kernel Bluetooth stack (`hid_mgr_read_bt` / `bt_set_controller_info_internal`) verifies the Vendor ID (`0x054C`) and Product ID (`0x0268`). Because the DualSense has `PID == 0x0CE6`, the kernel declassifies it to a generic standard HID gamepad.
2. The kernel's generic parser strips out the physical PS button bit before user-space APIs (`cellPadGetData`, `cellPadGetRawData`) can ever see it.
3. As a result, standard passthrough plugins could never detect the physical PS button over Bluetooth without resorting to unnatural button combinations (such as Select+Start).

### The Kernel Memory Peek Solution (`Syscall 6`)
To restore the genuine physical PS button without risking kernel deadlocks:
1. **Passive Kernel Reading**: The plugin uses PS3HEN / CFW Syscall 6 (`sys_cfw_peek`), a passive 64-bit memory read that executes without taking any kernel mutexes or modifying kernel code.
2. **Buffer Discovery**: By querying the LV2 Table of Contents (TOC offset `-0x3680`), the plugin locates the internal Bluetooth subsystem work structure (`bt_work`).
3. **Raw HID Report Interception**: The raw incoming Bluetooth HID report sits in `bt_work` at offset `0xE8` (Quadwords 29 & 30):
   * `0xA1 0x01 [LX] [LY] [RX] [RY] [Buttons1] [Buttons2] [Buttons3]`
   * **Byte 8 (Buttons 3), Bit 0 (`0x01`) is the physical PS button!**
4. **LDD Virtual Pad Forwarding**: When the physical PS button is pressed, the plugin injects `CELL_PAD_CTRL_LDD_PS` via `cellPadLddDataInsert`, waking the VSH in-game menu.
5. **Bidirectional Control Bridge**: During Bluetooth operation, the full controller state (D-pad, shapes, triggers, sticks) is continuously mirrored to the virtual LDD interface with in-game insertion disabled (`LDD_INSERT_INTO_GAME_OFF`). This ensures:
   * **In Game**: The game only receives input from Controller 1 (the native Bluetooth pad) — no phantom player 2.
   * **In PS Menu**: The overlay receives responsive navigation (Circle to close, Cross to enter, D-pad/sticks to move) from the LDD interface.

---

## 🚀 Installation Guide (PS3HEN & CFW)

Pre-compiled, hardware-verified binaries are included in the [`ps3_ready/`](ps3_ready/) folder.

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
  *(Ensure there is a newline at the end of the file).*

### Step 3: Bluetooth Pairing (First Time Only)
1. On the PS3 XMB, navigate to **Settings** $\rightarrow$ **Accessory Settings** $\rightarrow$ **Manage Bluetooth® Devices**.
2. Select **Register New Device** and start scanning.
3. On your DualSense, press and hold the **Create (Share)** button and the **PS button** simultaneously until the lightbar starts blinking rapidly.
4. Select the **Wireless Controller** on the PS3 screen.
5. If prompted for a passkey, enter `0000`.

### Step 4: Reboot and Play
1. Restart your PS3.
2. If running **PS3HEN**, click **Enable HEN**.
3. Press the **PS button** on your DualSense — it will connect wirelessly via Bluetooth!
4. Press the **PS button** at any time to open the native XMB menu, and press **Circle** to dismiss it!

---

## ⚙️ Configuration (`ds45pad.txt`)

The plugin behavior can be customized by editing `/dev_hdd0/plugins/ds45pad/ds45pad.txt`:

| Option | Default | Description |
|---|---|---|
| `ENABLE` | `1` | `1` = Enable plugin, `0` = Disable plugin. |
| `MODE` | `AUTO` | Transport mode: `AUTO` (USB when cabled, otherwise Bluetooth), `USB` (USB only), `BT` (Bluetooth only). |
| `MAX_PADS` | `1` | Number of simultaneous controllers handled. |
| `POLL_MS` | `10` | Input polling rate in milliseconds (10ms = 100Hz). |
| `INSERT_INTO_GAME` | `1` | Injects USB pad inputs directly into games via Syscall 573. |
| `ALLOW_INSERT_COMBO` | `1` | Allows toggling in-game injection using `CREATE + OPTIONS + R3`. |
| `DIAG` | `0` | Diagnostic file logging (`0` = Off, `1` = On). Leave at 0 for optimal performance. |
| `NOTIFY` | `0` | `1` = Show on-screen notification in XMB on load/attach. |
| `STICK_CALIBRATION` | `1` | Stick response curve calibration (`0` = Off, `1` = On). |

---

## 🎮 Adding Third-Party / Clone Controllers

To add custom or third-party controllers, edit `/dev_hdd0/plugins/ds45pad/ds45pad_devices.txt`:

```text
# Format: VID, PID, MODEL
# MODEL can be DUALSENSE or DS4
0x054c, 0x0ce6, DUALSENSE
0x054c, 0x0df2, DUALSENSE
0x054c, 0x05c4, DS4
0x054c, 0x09cc, DS4
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
* **PS3Xploit Team** — For PS3HEN and LV2 syscall research.

---

## ⚖️ License

This project is free and open-source software licensed under the **GNU General Public License v2 (GPL-2.0)**. See the [LICENSE](LICENSE) file for full details.
