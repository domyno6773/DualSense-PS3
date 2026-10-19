# PS3 DualSense & DualShock 4 Controller Plugin (Enhanced DS45Pad) - v2.0.0

> [!CAUTION]
> # <span style="color:#e63946;">⚠️ CRITICAL DISCLAIMER & HARDWARE NOTICE</span>
>
> <span style="color:#e63946;">**ALL TESTS HAVE BEEN CONDUCTED EXCLUSIVELY ON MY SPECIFIC CONSOLE CONFIGURATION (PS3 SUPER SLIM, HFW / PS3HEN 4.93) WITH NO OTHER ACTIVE PLUGINS AND WITHOUT WEBMAN. ANY OTHER SETUP OR CONFIGURATION THAT EXPERIENCES ISSUES IS STRICTLY OUTSIDE MY RESPONSIBILITY AND WILL NOT BE ADDRESSED.**</span>
>
> <span style="color:#e63946;">**ANY AND ALL ISSUES OR BUG REPORTS MUST BE SUBMITTED DIRECTLY VIA GITHUB ISSUES. FROM NOW ON, ANY REQUESTS FOR HELP OR SUPPORT ON REDDIT WILL BE STRICTLY IGNORED.**</span>
>
> <span style="color:#e63946;">**I ACCEPT ABSOLUTELY NO RESPONSIBILITY FOR WHAT YOU DO TO YOUR CONSOLES. YOU ARE FULLY CONSCIOUS AND AWARE OF YOUR OWN ACTIONS. PROCEED AT YOUR OWN RISK.**</span>

[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Platform](https://img.shields.io/badge/Platform-PS3_CFW_%7C_PS3HEN-orange.svg)]()
[![Tested Firmware](https://img.shields.io/badge/Tested%20on-Firmware%204.93%20(PS3HEN)-success.svg)]()
[![Hardware Verified](https://img.shields.io/badge/Status-Verified_Working_on_Hardware-brightgreen.svg)]()
[![Donate with PayPal](https://img.shields.io/badge/Donate-PayPal-00457C.svg?logo=paypal&logoColor=white)](https://www.paypal.com/paypalme/domyno6773)

A high-performance background VSH PRX/SPRX plugin for the **PlayStation 3 (CFW and PS3HEN)** providing native **USB and internal Bluetooth** support for the **PlayStation 5 DualSense** (`CFI-ZCT1`), **DualSense Edge**, and **DualShock 4** controllers — featuring native physical **PlayStation (PS / Home) button** support with **zero button combinations**!

---

## 🚀 What's New in Version 2.0.0 (Bluetooth Overhaul)

This major release completely solves previous Bluetooth limitations on PS3HEN / CFW:

1. **Zero Double-Input in XMB / Dashboard**:
   - By assigning the physical controller to **Port 7** in PS3 Accessory Settings, the plugin keeps the virtual pad on Port 0 completely silent in XMB. D-pad, analog sticks, and button presses are never duplicated.
2. **Native XMB PS Button Hold (Console Power Menu)**:
   - Holding the PS button ($\ge 250\text{ ms}$) in XMB natively brings up the console shutdown menu.
   - The menu remains open upon releasing the button, allowing full navigation, confirming shutdown/turn off controller with **Cross ($\times$)**, and canceling back to dashboard with **Circle ($\bigcirc$)**.
3. **Full In-Game Recognition as Player 1 (Syscall 573)**:
   - PS3 games listen to Port 0. Through kernel Syscall 573 (`INSERT_INTO_GAME`), your Bluetooth DualSense is mapped directly as **Player 1** in all games.
   - **START** (Options button) and **SELECT** (Create button) work natively.
   - Analog sticks feature calibrated circularity and deadzones, with full analog sensitivity on **L2 / R2**.
4. **Interactive In-Game PS Menu Overlay**:
   - Tap the physical PS button at any time during gameplay to access the in-game XMB overlay.
5. **Seamless USB & Bluetooth Coexistence**:
   - Plugging in a USB cable while playing on Bluetooth charges the controller safely without creating ghost controllers or duplicate inputs.

---

> [!IMPORTANT]
> ## 🎮 MANDATORY SETUP: REASSIGN CONTROLLER TO PORT 7
>
> To use the DualSense wirelessly over Bluetooth with zero conflicts, **you must reassign the controller to Port 7** in the PS3 system settings:
>
> 1. Turn on your PS3, enable HEN (if on PS3HEN), and connect your DualSense via Bluetooth.
> 2. On the XMB dashboard, navigate to:  
>    **Settings** $\rightarrow$ **Accessory Settings** $\rightarrow$ **Reassign Controllers**
> 3. Change the controller number from **1** to **7** (Controller 7).
>
> ### Why is this step mandatory?
> * The PS3 kernel Bluetooth stack registers the physical gamepad on a native port.
> * To avoid double inputs on the dashboard while ensuring the controller acts as **Player 1 in every game**, the plugin injects input to **Port 0 (Player 1)** via Syscall 573.
> * Parking the physical controller on **Port 7** ensures the XMB receives exactly one clean input stream, while games recognize you as Player 1 without phantom controllers!

---

## 🌟 Key Features

* **Physical PS / Home Button**: Pressing the physical PlayStation button natively opens the in-game XMB overlay and power menus on **both USB and internal Bluetooth** (no button shortcuts or combos needed).
* **Native Internal Bluetooth**: Connect directly to the PS3 built-in Bluetooth radio without any external USB dongles or adapters.
* **Full In-Game PS Menu Navigation**: Over Bluetooth, the PS menu is fully interactive — navigate with D-Pad/analog sticks, confirm with **Cross**, cancel with **Circle**, and close with the **PS button**.
* **Seamless Auto Mode (`MODE = AUTO`)**: Plug in via USB for charging & low-latency play, or unplug to seamlessly continue playing over wireless Bluetooth.
* **Clean In-Game Input**: Operates cleanly as Player 1 in all titles (PS3 games, multiMAN, retro emulators) without duplicate player conflicts.
* **Controller Support**: Supports official Sony PS5 DualSense (`054c:0ce6`), DualSense Edge (`054c:0df2`), and DualShock 4 v1 & v2 (`054c:05c4`, `054c:09cc`).
* **Firmware 4.93 Verified**: Thoroughly tested and verified on **PS3 Super Slim running PS3HEN 4.93**.
* **CFW & PS3HEN Compatible**: Signed with 3.55 retail keys (`Revision 0A`, VSH Auth-ID `1070000052000001`).
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
4. **LDD Virtual Pad Forwarding**: When the physical PS button is pressed, the plugin injects `CELL_PAD_CTRL_LDD_PS` via `cellPadLddDataInsert`, waking the VSH menu.
5. **Bidirectional Control Bridge**: During Bluetooth operation:
   * **In XMB**: The virtual pad remains muted to avoid double input, waking only when the PS button is pressed or the shutdown menu is opened.
   * **In Game**: Syscall 573 (`INSERT_INTO_GAME`) directs all inputs cleanly to Port 0 (Player 1), enabling Start, Select, triggers, and sticks.

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
  *(Ensure there is an empty newline at the end of the file).*

### Step 3: Bluetooth Pairing (First Time Only)
1. On the PS3 XMB, navigate to **Settings** $\rightarrow$ **Accessory Settings** $\rightarrow$ **Manage Bluetooth® Devices**.
2. Select **Register New Device** and start scanning.
3. On your DualSense, press and hold the **Create (Share)** button and the **PS button** simultaneously until the lightbar starts blinking rapidly.
4. Select the **Wireless Controller** on the PS3 screen.
5. If prompted for a passkey, enter `0000`.

### Step 4: Reassign Controller to Port 7 (Mandatory!)
1. In XMB, go to **Settings** $\rightarrow$ **Accessory Settings** $\rightarrow$ **Reassign Controllers**.
2. Select your controller and change its number to **7**.

### Step 5: Reboot and Play
1. Restart your PS3.
2. If running **PS3HEN**, click **Enable HEN**.
3. Press the **PS button** on your DualSense — it will connect wirelessly via Bluetooth!
4. Navigate XMB, launch your games, and enjoy full DualSense support.

---

## ⚙️ Configuration (`ds45pad.txt`)

The plugin behavior can be customized by editing `/dev_hdd0/plugins/ds45pad/ds45pad.txt`:

| Option | Default | Description |
|---|---|---|
| `ENABLE` | `1` | `1` = Enable plugin, `0` = Disable plugin. |
| `MODE` | `AUTO` | Transport mode: `AUTO` (USB when cabled, otherwise Bluetooth), `USB` (USB only), `BT` (Bluetooth only). |
| `MAX_PADS` | `1` | Number of simultaneous controllers handled. |
| `POLL_MS` | `10` | Input polling rate in milliseconds (10ms = 100Hz). |
| `INSERT_INTO_GAME` | `1` | Injects pad inputs directly into games via Syscall 573. |
| `ALLOW_INSERT_COMBO` | `1` | Allows toggling in-game injection using `CREATE + OPTIONS + R3`. |
| `DIAG` | `0` | Diagnostic file logging (`0` = Off, `1` = On). Leave at 0 for optimal performance. |
| `NOTIFY` | `1` | `1` = Show on-screen notification in XMB on load/attach. |
| `STICK_CALIBRATION` | `OFF` | Stick response curve calibration (`OFF` or `ON`). |

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
```powershell
cd src
.\build.ps1 -Variant vsh
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

## 💖 Support the Project

If this project made playing on your PS3 with your DualSense or DualShock 4 controller better and you'd like to support continued development, maintenance, and hardware testing, consider buying me a coffee or sending a donation!

<p align="left">
  <a href="https://www.paypal.com/paypalme/domyno6773" target="_blank">
    <img src="https://img.shields.io/badge/Donate-PayPal-00457C?style=for-the-badge&logo=paypal&logoColor=white" alt="Donate with PayPal" />
  </a>
</p>

> [!TIP]
> **PayPal Donation Link**: **[paypal.me/domyno6773](https://www.paypal.com/paypalme/domyno6773)**  
> Any support is greatly appreciated!☕🎮

---

## ⚖️ License

This project is free and open-source software licensed under the **GNU General Public License v2 (GPL-2.0)**. See the [LICENSE](LICENSE) file for full details.
