# U-Boot 启动菜单 DHCPv4 Server 设计

## 目标与生命周期

在启用了 MediaTek bootmenu 的 U-Boot 中加入轻量 DHCPv4 Server。服务在启动菜单开始显示前初始化，通过 U-Boot cyclic 机制每 10 ms 轮询网卡，并在用户进入 U-Boot console 后继续运行。其他网络命令临时接管网络栈时记录 DHCP 状态，命令结束后自动恢复网卡和 UDP handler；进入 Web Recovery 时重新确认 handler，使未配置静态地址的直连客户端仍能访问恢复页面。DHCP 初始化或网卡启动失败不能阻断原有启动菜单和启动流程。

## 协议与地址分配

服务监听 UDP 67，接受来自 UDP 68 的 BOOTREQUEST，只处理 Ethernet DHCPDISCOVER 和 DHCPREQUEST。响应包括 DHCPOFFER、DHCPACK 和必要的 DHCPNAK，并使用二层、三层广播发送。响应保留至少 300 字节的 BOOTP 长度以兼容 Windows 客户端。服务地址和掩码来自板级 `CONFIG_IPADDR`、`CONFIG_NETMASK`；默认地址池从子网主机号 100 开始，共 101 个地址，即常见 `/24` 网段的 `.100` 到 `.200`。最多记录 8 个客户端，以 MAC 的 FNV-1a 哈希选择稳定地址并避免已记录地址冲突。租期为 3600 秒，网关和 DNS 指向 U-Boot 地址。

## 组件与数据流

`net/mtk_dhcpd.c` 负责协议解析、租约、响应构造、UDP handler 管理、网卡生命周期和 cyclic 注册；`include/net/mtk_dhcpd.h` 暴露 start、poll、stop 和运行状态。`cmd/bootmenu.c` 在交互菜单显示前启动服务，此后 cyclic 借助菜单及命令行原有的 `schedule()` 调用持续收包。`failsafe/failsafe.c` 在 HTTP 网络循环前确认 DHCP 已启动；`net/net.c` 在 MTK TCP 初始化后重新挂载 handler，并在其他网络命令退出后恢复 DHCP。Kconfig 默认在 MediaTek bootmenu 且具备编译期 IP/掩码时启用，同时选择 CYCLIC 并提供地址池配置。

## 错误处理与验证

所有报文先检查端口、最小长度、BOOTP 类型、硬件类型、MAC 长度和 DHCP magic cookie；选项解析严格检查剩余长度。REQUEST 指定其他服务器时忽略，请求地址越界、跨子网或已分配给其他 MAC 时回复 NAK。启动失败保持菜单可用；服务持续到 U-Boot 将控制权交给系统或复位。静态验证包括 `git diff --check`、配置符号和调用点检查；经用户允许后编译 `mt7986_jdcloud_re-cp-03_defconfig`，最终在设备上抓包验证 DORA 流程、菜单倒计时/停留阶段配址、U-Boot console 持续配址、网络命令后恢复和 Web Recovery 续租。
