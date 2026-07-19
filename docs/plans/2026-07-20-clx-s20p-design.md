# CLX S20P bootloader support design

## Scope

Add a dedicated CLX S20P build target for the regular OpenWrt `clx_s20p`
image. The `clx_s20p-label2` production/FIT layout is explicitly out of scope.

## Remote source of truth

The OpenWrt S20P support in `/home/jack/v25.12` identifies an MT7986A board
with 2 GiB DDR4, eMMC storage, an MT7531 switch, reset on GPIO 16, the status
LED on GPIO 22, and the MT7531 reset line on GPIO 5. The regular image uses the
same eMMC environment and sysupgrade-tar family as existing MT7986 eMMC boards.

## Implementation

- Add `mt7986_clx_s20p_defconfig` to the active U-Boot tree, based on the
  established MT7986 DDR4/eMMC boot-menu configuration.
- Add an S20P-specific U-Boot device tree containing only bootloader-relevant
  hardware: memory, eMMC, MT7531 Ethernet, reset button, status/WLAN LEDs,
  console, regulators, and watchdog state.
- Add the matching active ATF target for MT7986, DDR4, and eMMC boot.
- Reuse the existing eMMC boot menu, upgrade flow, web failsafe, and persistent
  DHCPv4 server. The new defconfig satisfies the DHCP server's boot-menu,
  management-address, and netmask dependencies.

The resulting build target is `SOC=mt7986 BOARD=clx_s20p ./build.sh`.
