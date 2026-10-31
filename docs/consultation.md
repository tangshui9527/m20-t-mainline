# 请教：MSM8909（海信 M20-T）主线 Linux 内核早期挂起，无串口条件下如何定位

> 2026-10-31。设备与全部尝试如下，请有 msm8909/msm8916 主线移植经验的朋友给方向。
> 所有结论均实测验证过，不是猜测。文末是具体问题清单。

## 1. 设备信息（全部实测取证）

| 项目 | 值 |
|---|---|
| 机型 | 海信 M20-T（运营商定制，Uniscope ODM 代工） |
| SoC | MSM8909（骁龙210，4×Cortex-A7，armv7 32位） |
| soc_id | 245 (0xf5) —— 与 ufi210-debian 的 DW01 随身WiFi 完全一致 |
| 板型 | QRD SKUA 公版衍生（platform_id=245, variant_id=0x1000b, subtype=0, pmic0=0x1000d） |
| PMIC | PM8909 |
| 内存 | 1GB（stock /proc/meminfo MemTotal 943MB） |
| 存储 | eMMC 7.4GB |
| WiFi | WCN3620（pronto-v3） |
| 原厂系统 | Android 4.4.4，kernel 3.10（CLO msm-3.10） |
| 安全状态 | **无 secboot 熔丝**（无签名 boot 镜像实测可启动；fastboot 无 flash/boot 命令） |
| 显示 | ILI9806E FWVGA DSI（MDP3，主线不支持显示——我们做无头，显示不需要） |

分区表（GPT lun0 关键项，完整表可提供）：boot 32MiB（sector 396384）、recovery 32MiB、system 896MiB、cache 256MiB、userdata ~5.6GiB、modem 64MiB、sbl1/rpm/tz/aboot 各 512K–1MiB。

## 2. 已跑通的部分（全部实测）

1. **引导链**：stock aboot（原厂未动）→ [lk2nd](https://github.com/msm8916-mainline/lk2nd) 23.1 `lk2nd-msm8909`（写入 boot 分区 @0）→ 任意 boot.img（@+512KiB）→ 内核。lk2nd 在此设备完整工作：fastboot、启动菜单、屏幕驱动（ILI9806E 被点亮）。
2. **lk2nd 的 QCDT 补丁**：stock aboot 要求 boot.img 的 DT 段为 dtbTool QCDT 表（magic 0x54444351，反汇编 dev_tree_validate 证实），裸 DTB 被拒（"dtb not found"/"Bad magic"）。lk2nd 官方镜像的 QCDT 表无我们板型的条目，已手工把条目[3]（0xf5/0x1000b/subtype 8→0）补丁后 lk2nd 正常运行。
3. **EDL 9008 firehose**：loader = kclinx/android_device_ans_ul40 的 msm8909.mbn，eMMC 读写实测可靠（写后读回 md5 校验全过）。
4. **stock 内核（3.10）经 lk2nd 启动 → Android 完整可用**（root+dd 均正常）。
5. **软件进入模式**：Android root 下写 4KB 垃圾到 boot+512K → `adb reboot` → lk2nd 检测到第二阶段无效 → 稳定进 lk2nd fastboot；lk2nd fastboot 下 `fastboot oem reboot-edl` → 9008。全程零按键。
6. **CI 构建**：weikaizhi4/linux（7.0 基线）@410d5647 + [ufi210-debian](https://github.com/3899/ufi210-debian) 的 reference config（其在 soc_id 245 的随身WiFi 上实测跑通 Debian）+ 其 zu02-dw01 板级 dts。

## 3. 问题：主线内核在最早阶段挂起（hang，不是 panic）

以下全部实测：

- **6.12.1-msm8916 标签**（msm8916-mainline）+ 我们按 6.12 的 pm8909 dtsi 写的板级 dts（QRD SKUA ids + 裸 `/memory` 节点 1GB）：stock aboot 直启 → 白屏挂起循环；lk2nd 启动 → 同样挂起。
- **7.0 树（weikaizhi4/linux @410d5647）+ ufi210 reference config + zu02-dw01 dtb**：lk2nd `fastboot boot` → Booting OKAY → **挂起**（无 NCM、无 adb、~10s 后看门狗复位）。
- **7.0 内核 + zu02 dtb + 追加裸 `/memory` 节点（1GB）**：lk2nd 试跑 → 同样挂起。
- **7.0 内核 + nokia-leo dtb**（同树已验证的 8909 真机板）：lk2nd 试跑 → 同样挂起。
- **panic=-1 实验**：挂起后不再自动复位（等待 ~10s 的复位消失）→ 证明是 **lk2nd 看门狗复位一个挂死的内核**，不是 panic。
- **stock 内核 3.10 在同样环境下一切正常**（Android 完整可用）。

即：**换内核树、换板级 DTB（含别的 Soc245 实机板的 DTB）、加不加 /memory 节点、stock aboot 还是 lk2nd——全都挂**。只有 3.10 stock 内核能跑。

## 4. 已部署但未奏效的诊断

- **DEBUG_LL RAM 控制台**：补丁 arch/arm/include/debug/msm.S，earlyprintk 输出重定向到 phys 0x8f001000（VMSPLIT_3G，VA 0xcf001000，lowmem 内）；缓冲区通过 DT 保留（ramlog@8f000000，plain reserved 线性映射）。
- 编译确认：CONFIG_DEBUG_LL=y、DEBUG_QCOM_UARTDM=y、DEBUG_LL_INCLUDE="debug/msm.S" 均在最终 .config 中；zImage 字面量门禁通过（GAS 将 `ldr =0xcf001000` 编码为 movw/movt 对，不做字节搜索验证）。
- **三次读取全部为空**：从 Android root `dd if=/dev/mem`（0x8f000000 起 1MB）——但三次读取都发生在 Android 完整启动之后（该区域对 3.10 stock 内核是普通内存，启动即被分配器覆写）。**从未在无覆写的窗口读过。**
- 读回的竞态问题：尝试在 stock 内核的 DT 里保留该区域（dtb17 的 memory 子节点加 m20t_log@8f000000，remove-completely 风格与原厂兄弟节点一致）——**dtc 1.8 重排版和 python-fdt 库重序列化两个版本的 dtb17 都被 lk2nd 拒收**（"bootimage header fields are invalid"，lk2nd 接受未修改的原版 dtb17）。疑似 lk2nd 的 dtb 解析对修改敏感，原因未明。

## 5. 我们的具体疑问

1. **QM8909 早期挂起的已知原因**：msm8916-mainline 生态里，8909 设备（nokia-leo、lenovo-p5100、zte-sapphire、zu02-dw01）之外，有没有人遇到"换 DTB、换树都早期挂起"的情况？最后是什么原因？
2. **RPM/SMEM 握手**：M20-T 的 RPM 固件版本较老（4.4 时代 ODM 设备）。主线内核启动早期对 RPM/SMEM 的依赖——如果 RPM 固件接口版本不匹配，内核会在什么阶段挂？会是无输出挂起吗？（stock 3.10 内核能和这个 RPM 正常通信。）
3. **QRD SKUA 公版**：msm8909 的 QRD 参考设计板有没有人直接跑过主线？QRD SKUA 和 nokia-leo/zu02 这类板的差异里，有什么会让主线内核早期挂起？（例如 PM8909 的 GPIO/电源树差异、DDR 频率配置、trustzone 版本。）
4. **lk2nd 对未知板型的第二阶段内存注入**：lk2nd 启动第二阶段内核时如何填充 /memory？对不在其 device 列表里的板（我们的 M20-T），内存注入是否可靠？（nokia-leo dts 无 /memory 节点也能跑，推测 lk2nd 自己处理，但未验证。）
5. **无串口的早期调试手段**：除了 DEBUG_LL 重定向到内存、解压器/head.S 阶段标记（我们正准备做），社区还有什么无 UART 的早期挂起定位手段？（kgdb over USB? pstore/ramoops 的最早可用时机？CONFIG 的什么组合能让 printk 更早落盘？）
6. **ARM32 DEBUG_LL 重定向内存的坑**：我们把 msm.S 的 addruart 改为写 VA 0xcf001000（VMSPLIT_3G + HIGHMEM，phys 0x8f001000 在 lowmem 内）。earlyprintk 的 printch 在 MMU on 之后调用——这个 VA 在 setup_arch 之前（paging 已由 head.S 建立）是否一定有效？有没有我们没想到的映射时机问题？
7. **lk2nd 拒收修改过的 dtb17**：lk2nd 接受原版 dtb17（QCDT ×125 表内的 DTB），但 dtc 1.8 重排版或 python-fdt 库重序列化（仅加一个 memory 子节点/改一个保留区 size 字段）后的 dtb17 都被拒（"bootimage header fields are invalid"——lk2nd 的报错，但报错点疑似在 dtb 解析而非镜像头）。lk2nd 的 dev_tree 解析对 FDT 有什么隐含要求（版本、布局、字符串表、memory 子节点结构）？附件可提供修改前后的 dtb。
8. **XZ vs GZIP 压缩**：zImage 用 XZ（5MB）和 GZIP（8.9MB）我们都试过，都挂。解压器阶段的挂起如何与内核早期挂起区分？

## 6. 可提供的材料

- 完整分区备份（原厂 boot/aboot/sbl1/rpm/tz/recovery/devinfo/misc 等，md5 校验）
- 未修改的原版 dtb17、两个被 lk2nd 拒收的修改版 dtb17、正常工作的 lk2nd 镜像
- 两次挂起内核的 zImage/Image（含 DEBUG_LL 补丁的 6.12 与 7.0 构建）、完整 .config
- 我们写的全部反汇编笔记（stock aboot 的 boot 流程、QCDT 校验、/memory 注入逻辑）
- ufi210-debian 的 zu02-dw01 dts 补丁（soc_id 245 已验证的最小板级 dts 模板）

## 7. 我们下一步的计划（欢迎纠偏）

1. 解压器 + 内核 head.S 的阶段标记补丁（0xA1/A2/A5 直写 phys 0x8f000000，MMU off 阶段直写物理地址）——把"挂在哪一段"先二分出来
2. 若确认进入 start_kernel 后挂起 → printk 直写内存补丁（绕过 console 注册时机）
3. 对照 nokia-leo 的 dts 逐节点排查我们板级 dts 的差异
4. 考虑买 USB-TTL 接 BLSP1_UART1（GPIO4/5）——实机串口

联系方式可通过文档提供者转达。感谢任何方向性指点。
