# U-Boot Menu DHCPv4 Server Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 在 MediaTek U-Boot 启动菜单和 Web Recovery 期间为直连客户端自动分配 IPv4 地址。

**Architecture:** 新增一个复用 U-Boot UDP 收发接口的轻量 DHCPv4 Server。bootmenu 启动服务，cyclic 机制在菜单和 U-Boot console 中持续收包，网络循环负责重新挂载被其他网络命令替换的 handler。

**Tech Stack:** U-Boot 2023.07 C、Kconfig、U-Boot DM Ethernet/IPv4/UDP API。

---

### Task 1: DHCPv4 协议实现

**Files:**
- Create: `uboot-mtk-20230718-09eda825/include/net/mtk_dhcpd.h`
- Create: `uboot-mtk-20230718-09eda825/net/mtk_dhcpd.c`
- Modify: `uboot-mtk-20230718-09eda825/net/Makefile`

**Step 1:** 定义 `mtk_dhcpd_start()`、`mtk_dhcpd_poll()`、`mtk_dhcpd_stop()` 和运行状态接口。

**Step 2:** 实现 BOOTP/DHCP 报文和安全的 DHCP option 解析，覆盖 DISCOVER 与 REQUEST。

**Step 3:** 实现 `.100`–`.200` 默认地址池、8 条 MAC 租约、稳定哈希选址和冲突校验。

**Step 4:** 实现广播 OFFER/ACK/NAK，包含 server-id、mask、router、DNS 和 3600 秒租期。

**Step 5:** 将 `mtk_dhcpd.o` 接入 `net/Makefile`。

### Task 2: 配置项

**Files:**
- Modify: `uboot-mtk-20230718-09eda825/net/Kconfig`

**Step 1:** 新增 `MTK_DHCPD`，依赖 `MEDIATEK_BOOTMENU`、`USE_IPADDR` 和 `USE_NETMASK` 并默认启用。

**Step 2:** 新增 `MTK_DHCPD_POOL_START_HOST` 和 `MTK_DHCPD_POOL_SIZE`，默认分别为 100 和 101。

**Step 3:** 选择 `CYCLIC`，用配置解析检查符号依赖和默认值；未获得构建授权前不执行 `make olddefconfig`。

### Task 3: 启动菜单生命周期与轮询

**Files:**
- Modify: `uboot-mtk-20230718-09eda825/cmd/bootmenu.c`

**Step 1:** 在 bootmenu 显示前启动 DHCP，记录启动是否成功。

**Step 2:** 注册 10 ms cyclic 回调，复用菜单和命令行的 `schedule()` 调用轮询网卡。

**Step 3:** 离开菜单进入 U-Boot console 时保持 DHCP 运行。

**Step 4:** 确认 DHCP 失败不改变 bootmenu 返回值。

### Task 4: Web Recovery 集成

**Files:**
- Modify: `uboot-mtk-20230718-09eda825/failsafe/failsafe.c`
- Modify: `uboot-mtk-20230718-09eda825/net/net.c`

**Step 1:** HTTP failsafe 进入 `net_loop(MTK_TCP)` 前确认 DHCP 已启动。

**Step 2:** MTK TCP 的网络初始化完成后调用 start，确保 UDP handler 仍指向 DHCP 分发器。

**Step 3:** 记录网络命令入口时的 DHCP 状态，并在所有返回路径恢复网卡和 handler。

### Task 5: 静态与构建验证

**Files:**
- Verify all files above.

**Step 1:** Run: `git diff --check`

Expected: 无空白错误。

**Step 2:** Run: `rg -n "MTK_DHCPD|mtk_dhcpd_(start|poll|stop)" uboot-mtk-20230718-09eda825`

Expected: 配置、编译项、菜单轮询和 failsafe 生命周期均有对应调用。

**Step 3:** 经用户确认后运行 `make olddefconfig` 和 JDCloud RE-CP-03 U-Boot 构建。

Expected: 配置中 `CONFIG_MTK_DHCPD=y`，U-Boot 编译成功且无新增警告。

**Step 4:** 设备测试：清除客户端静态地址，在菜单倒计时、停止倒计时和 Web Recovery 三个阶段抓取 DHCP DORA 报文并访问 `http://192.168.1.1/`。

Expected: 客户端获得 `192.168.1.100`–`192.168.1.200/24` 地址；进入 U-Boot console 后 DHCP 继续工作；其他网络命令退出后自动恢复；进入 Web Recovery 后仍可配址。
