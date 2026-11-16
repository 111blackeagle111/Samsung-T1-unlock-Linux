# T1 Unlocker

English | [Italiano](README.it.md)

A Linux GUI app that unlocks the **hardware-encrypted Samsung Portable SSD T1** —
no Windows required.

The T1 ships with Samsung software for Windows/macOS only: out of the box, a locked
drive shows up on Linux as a small virtual setup disk and the real data partition is
invisible. T1 Unlocker sends the vendor unlock command directly over USB, then mounts
the real partition for you.

![App icon](packaging/icon.svg)

## Features

- **Live drive detection** — status updates every 2 s: no drive / locked / unlocked /
  insufficient USB permissions
- **Unlock with password** — typed in the app, never stored, never logged
- **Automatic mount** of the unlocked partition via udisks2 (best-effort: without
  udisks2 the unlock still works, the mount is just manual)
- **Safe defaults** — the unlock button is only enabled when the drive is locked and a
  password is present; closing the window mid-operation is blocked
- **Universal AppImage** — one portable file, bundles the whole GTK stack
- **BOT debug mode** (`--debug-bot`) — logs CBW/CSW hex of the USB transactions for
  protocol research (never the password payload)

## Supported hardware

| Model | USB IDs | Status |
|---|---|---|
| Samsung Portable SSD T1 (e.g. MU-PS250B) | `04e8:8001` | **Tested on real hardware** (250 GB unit) |
| Samsung Portable SSD T1, classic firmware | `04e8:61f1` / `04e8:61f2` | Same protocol, not tested by the author |

T3/T5 drives are **not** supported (different product IDs).

## Requirements

- Linux **x86_64**, glibc ≥ 2.31 (any mainstream distro from ~2020 on)
- `udev` and `libusb-1.0` (present everywhere; not bundled on purpose — they must
  match the host's daemons)
- `udisks2` running, for the automatic mount (optional)
- USB access to the drive (see below)

## Install & run

Download `T1Unlocker-0.1.0-x86_64.AppImage` from the [Releases](../../releases) page,
then:

```bash
chmod +x T1Unlocker-0.1.0-x86_64.AppImage
./T1Unlocker-0.1.0-x86_64.AppImage
```

If your distro ships without libfuse2, run it with:

```bash
./T1Unlocker-0.1.0-x86_64.AppImage --appimage-extract-and-run
```

### USB permissions

Raw USB access needs a udev rule with the `uaccess` tag, which grants access to the
active desktop session user:

```bash
sudo tee /etc/udev/rules.d/99-t1-unlocker.rules > /dev/null <<'EOF'
SUBSYSTEM=="usb", ATTRS{idVendor}=="04e8", ATTRS{idProduct}=="8001", MODE="0660", TAG+="uaccess"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Unplug and replug the drive after installing the rule. Without it the app shows an
"Insufficient permissions" state with a pointer to this section. On distros without
systemd-logind, use `GROUP="plugdev"` (or your distro's equivalent) instead of the tag.

## Usage

1. Plug the T1 in — the app shows **"Locked drive detected"**
2. Type the password, press **Unlock** (or Enter)
3. Wait a few seconds: the log shows `Unlock successful` and `Partition /dev/sdX1 mounted`
   — the drive appears mounted, typically under `/media/<user>/`

Notes:

- The T1 **re-locks itself at every power cycle** — that is firmware behavior. After
  unplugging (or a suspend that cuts USB power), just unlock again
- One drive at a time: with multiple T1s connected, the first one found is used
- After a failed attempt, unplug/replug before retrying: the firmware's limit on
  failed password attempts is unknown

## How it works

While locked, the T1 exposes a ~128 MB virtual FAT disk (label `T1_SETUP`) instead of
the real drive. The USB device IDs do **not** change between states, so the app tells
them apart from the block-device signature reported by udev.

The unlock sequence (derived from the open-source
[pssdunlock](https://github.com/redchenjs/pssdunlock) research and verified on real
hardware):

1. Reset the USB device and claim its interface (auto-detaching the kernel driver)
2. Send the 31-byte vendor CBW (ATA PASS-THROUGH, Samsung vendor block), then the
   password as a 512-byte zero-padded payload, then read the response — `resp[9] == 0x00`
   means success, `0x02` means wrong password (or already unlocked)
3. Send the *relink* command and a USB port reset, then release the interface so the
   kernel re-probes the drive — the real LUN and its partition appear
4. Wait for the partition and mount it via the udisks2 D-Bus API

## Security notes

- The password is **never** written to disk, passed via argv, or logged. Buffers are
  wiped with `explicit_bzero` on every code path after use
- The debug trace mode logs only the 31-byte command blocks and 13-byte status blocks,
  never the data stage
- **Residual risk, by firmware design:** the Samsung protocol transmits the password
  **in cleartext over the USB bus**. Anyone able to sniff the physical bus can capture
  it. This cannot be mitigated by software on the host
- The encryption itself lives in the drive's firmware — this app only sends the unlock
  command; it cannot recover, reset or change a forgotten password

## Troubleshooting

| Symptom | Meaning / fix |
|---|---|
| "Insufficient permissions (udev/uaccess)" | Install the udev rule above, replug the drive |
| "Wrong password (or drive already unlocked)" | Wrong password, or the drive is in fact unlocked — check the status line |
| "Partition not visible within 30 s" | The drive was slow to re-present the real disk; mount manually: `udisksctl mount -b /dev/sdX1` |
| "USB communication error during unlock" | Drive was unplugged or the USB transaction failed — replug and retry |
| Repeated failures | Unplug/replug before retrying (unknown firmware attempt limit) |

## Limitations

- v1 is **unlock-only**: re-locking and password change require vendor commands that
  are not publicly known yet (see roadmap)
- No firmware operations of any kind; no T3/T5; x86_64 only
- The cross-distro smoke test is still pending (developed and hardware-tested on an
  Ubuntu-based GNOME desktop)

## Roadmap

- **v1.1** — manual re-lock and password change (vendor commands to be reverse
  engineered from the Windows software; the `--debug-bot` trace mode is built for this)
- **v1.2** — drive status panel, in the spirit of Samsung Magician

## Building from source

The easy way (Docker, produces the AppImage):

```bash
packaging/build-appimage.sh
```

Manual build inside an `ubuntu:20.04`-based container (Meson ≥ 0.53, gtkmm-3.0,
libusb-1.0, libgudev, GTest):

```bash
meson setup build --buildtype=release
ninja -C build
meson test -C build   # 17 unit tests, no hardware needed
```

## Credits

- [pssdunlock](https://github.com/redchenjs/pssdunlock) by redchenjs — the reference
  implementation this project learned the protocol from. Its repository declares no
  explicit license; T1 Unlocker does not copy any of its code — only the protocol byte
  values, which are interoperability facts, are reused, with credit
- [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) and
  [linuxdeploy-plugin-gtk](https://github.com/linuxdeploy/linuxdeploy-plugin-gtk) for
  the AppImage bundling

## Disclaimer

This project is not affiliated with or endorsed by Samsung. It is published in the
hope it is useful, but without any warranty; use it at your own risk. Always keep a
backup of your data and of your password — nothing in this app (or anywhere else) can
unlock a T1 without it.

## License

Released under the [MIT License](LICENSE).
