# STM32U5 Bootloader 代码分析与使用说明

本仓库是面向 STM32U5A9J-DK / STM32U5A9NJH6Q 的 MCUboot 风格 Bootloader 工程。当前实现采用双槽 Direct XIP，不做镜像交换；Bootloader 在启动时校验两个槽位中的镜像，选择版本号最高且校验通过的镜像，并直接跳转到该槽位执行。固件更新通过 PC13 触发的 YMODEM 下载模式完成，下载目标始终是当前活动槽的另一侧。

当前 README 按源码当前状态编写，重点覆盖启动流程、Flash 分区、镜像校验、回退机制、YMODEM 升级、构建方式和测试注意事项。

## 当前状态速览

| 项目 | 当前实现 |
| ---- | -------- |
| MCU | STM32U5A9NJH6Q, Cortex-M33 |
| 开发板 | STM32U5A9J-DK |
| 启动策略 | MCUboot Direct XIP |
| 镜像槽位 | Primary + Secondary 两个可执行槽 |
| 当前分区 | 调试布局，Bootloader 64 KB，Primary/Secondary 各 24 KB |
| HASH 后端 | STM32U5 HASH 外设, `MCUBOOT_SHA256_BACKEND_STM32_HASH` |
| 完整性校验 | SHA-256 TLV 校验，作为签名验真的 digest 输入 |
| 签名验真 | EC256/ECDSA P-256，默认使用 STM32U5 PKA 硬件后端 |
| 下载协议 | USART1 上的 YMODEM CRC16 |
| 下载触发 | 复位时 PC13 为高电平 |
| 活动槽记录 | Flash 最后 8 KB User Area 的 offset 0 |
| 看门狗 | IWDG，长时间擦除/接收时会喂狗 |

## 重要限制

1. 当前 `Boot/sysflash/sysflash.h` 是调试分区：Bootloader 为 `0x10000` 字节，两个应用槽各 `0x6000` 字节。应用镜像、TLV 和 trailer 必须全部装入槽内。
2. `MDK-ARM/Boot.sct` 已更新为 64 KB Bootloader 区域；Keil/EIDE 当前 IROM 布局也应保持 `0x08000000 + 0x10000`。发布前仍建议检查 map 文件，确认 Bootloader 本体没有覆盖 Primary 槽。
3. 当前签名验真只支持 EC256/ECDSA P-256 路径，不支持 RSA、Ed25519、加密镜像或压缩镜像。
4. `Tests/` 是独立的主机侧测试，不在 Keil 工程内。部分测试仍保留旧分区断言，运行前需要与当前调试分区同步。
5. User Area 当前只保存 active slot 记录。代码没有实现 README 旧版本中提到的 `download_state` 断点恢复字段。

## 目录结构

```text
.
├── Boot/
│   ├── bootutil/                 MCUboot 裁剪/移植层
│   │   ├── include/bootutil/      镜像格式、公开 API、槽状态、SHA 抽象
│   │   └── src/                  启动决策、校验、trailer、跳转、活动槽记录
│   ├── flash_map_backend/        STM32U5 内部 Flash 的 flash_area 适配层
│   ├── mcuboot_config/           MCUboot 功能开关和日志宏
│   ├── sysflash/                 Flash 分区常量
│   └── ext/tinycrypt/            TinyCrypt 源码，当前 HASH 外设后端启用时不作为主后端
├── Core/
│   ├── Inc/                      CubeMX 生成头文件
│   └── Src/                      main、GPIO、USART、HASH、PKA、IWDG、HAL MSP
├── Ymodem/                       YMODEM 接收实现
├── MDK-ARM/                      Keil MDK 工程和启动文件
├── Vscode/.eide/                 EIDE 工程配置
├── docs/                         设计记录、方案调研和阶段性计划
├── keys/                         本地签名密钥目录，生产私钥不得入库
├── test_app/                     本地调试应用镜像样例
├── Tests/                        主机侧单元测试/结构测试
├── Boot.ioc                      STM32CubeMX 配置
└── README.md
```

## 硬件和外设

| 功能 | 引脚/外设 | 配置 |
| ---- | --------- | ---- |
| YMODEM / printf | USART1 PA9(TX), PA10(RX) | 115200-8N1, 无流控, FIFO 关闭 |
| 预留串口 | USART2 PD5(TX), PA15(RX) | 115200-8N1 |
| 下载触发 | PC13 | 输入下拉，高电平进入下载模式 |
| HASH | HASH 外设 | `HASH_DATATYPE_8B`, SHA-256 |
| ECDSA 验签 | PKA 外设 | P-256/ECDSA verify，默认硬件后端 |
| 看门狗 | IWDG | LSI, prescaler 256, reload 625 |
| 系统时钟 | MSI + PLL | SYSCLK/HCLK/PCLK = 160 MHz |

`Core/Src/usart.c` 中的 `fputc()` 将 `printf` 输出重定向到 USART1，并提供了 `__use_no_semihosting` 以及最小 no-semihosting stub。因此当前调试输出走串口，不依赖 semihosting。

## Flash 分区

分区定义来自 `Boot/sysflash/sysflash.h`。当前是调试布局，使用 STM32U5 lower 2 MB Flash window。

```text
0x08000000  - 0x0800FFFF   Bootloader    64 KB   (0x10000)
0x08010000  - 0x08015FFF   Primary       24 KB   (0x6000)
0x08016000  - 0x0801BFFF   Secondary     24 KB   (0x6000)
0x0801C000  - 0x081FDFFF   Reserved    1928 KB   (0x1E2000)
0x081FE000  - 0x081FFFFF   User Area      8 KB   (0x2000)
```

关键宏：

| 宏 | 值 | 含义 |
| -- | -- | ---- |
| `CY_FLASH_BASE` | `0x08000000` | 内部 Flash 基地址 |
| `CY_FLASH_SIZE` | `0x200000` | lower 2 MB Flash window |
| `CY_BOOT_BOOTLOADER_SIZE` | `0x10000` | Bootloader 逻辑分区大小 |
| `CY_BOOT_PRIMARY_1_SIZE` | `0x6000` | Primary 槽大小 |
| `CY_BOOT_SECONDARY_1_SIZE` | `0x6000` | Secondary 槽大小 |
| `CY_BOOT_USER_AREA_SIZE` | `0x2000` | User Area 大小 |
| `CY_BOOT_USER_AREA_OFFSET` | `0x1FE000` | User Area 相对 Flash base 的偏移 |
| `CY_IMG_HDR_SIZE` | `0x200` | 镜像头大小 |
| `FLASH_DEVICE_INTERNAL_FLASH` | `0x7F` | 内部 Flash 设备 ID |

槽位地址由 `Boot/flash_map_backend/cy_flash_map.c` 生成：

```c
primary_1.fa_off   = CY_FLASH_BASE + CY_BOOT_BOOTLOADER_SIZE;
secondary_1.fa_off = CY_FLASH_BASE + CY_BOOT_BOOTLOADER_SIZE + CY_BOOT_PRIMARY_1_SIZE;
```

因此当前槽位入口为：

```text
Primary slot base   = 0x08010000
Primary vector      = 0x08010200   (slot base + 0x200)
Secondary slot base = 0x08016000
Secondary vector    = 0x08016200   (slot base + 0x200)
```

## 构建方式

主要工程是 Keil MDK：

1. 打开 `MDK-ARM/Boot.uvprojx`。
2. 使用 ARMCLANG 6 编译。
3. 烧录到 `0x08000000`。
4. 复位后 Bootloader 自动运行。

EIDE 配置位于 `Vscode/.eide/eide.yml`，也引用了相同的核心源码、HAL、YMODEM、Boot 移植层。

构建相关注意点：

- `MDK-ARM/Boot.sct` 已定义 `LR_IROM1 0x08000000 0x00010000`，对应 64 KB Bootloader 区域。
- `MDK-ARM/Boot.uvprojx` 当前 `useFile=0`，未显式启用自定义 scatter file；项目内存布局中的 IROM 也需要保持 `0x08000000 + 0x10000`。
- `Vscode/.eide/eide.yml` 当前 IROM 布局为 `0x08000000 + 0x10000`，但 `useCustomScatterFile=false`，因此也依赖工程内存布局约束。
- 如果恢复生产分区或增大应用槽，必须同时更新 `sysflash.h`、测试断言、应用链接脚本和镜像打包参数。

## 启动流程

主入口在 `Core/Src/main.c`。

```text
Reset
  -> HAL_Init()
  -> SystemClock_Config()
  -> MX_GPIO_Init()
  -> MX_USART1_UART_Init()
  -> MX_USART2_UART_Init()
  -> MX_IWDG_Init()
  -> MX_HASH_Init()
  -> check_boot_mode()
       PC13 高电平: YMODEM 下载模式
       PC13 低电平: 正常启动模式
```

### 下载模式

复位时 PC13 为高电平会进入下载模式：

1. `boot_active_slot_read()` 从 User Area 读取上一次成功跳转的活动槽。
2. 如果活动槽是 Primary，则下载目标为 Secondary。
3. 如果活动槽是 Secondary，则下载目标为 Primary。
4. 调用 `Ymodem_Receive(ymodem_buf, target_slot)` 接收镜像。
5. 接收完成后打印结果，短延时后 `NVIC_SystemReset()`。

如果活动槽记录无效，`boot_active_slot_read()` 默认返回 Primary，因此下载目标通常为 Secondary。

### 正常启动模式

正常启动路径：

1. `boot_go(&rsp)` 初始化 MCUboot 状态机并打开两个槽的 `flash_area`。
2. 读取两个槽的 MCUboot image header。
3. 过滤掉 header 无效、header 被擦除、非 bootable、加密或压缩的镜像。
4. 在可用槽中选择版本号最高的镜像。
5. 在 Direct XIP Revert 模式下读取 trailer，必要时擦除未确认镜像。
6. 做 SHA-256 TLV 完整性校验。
7. `boot_go()` 填充 `struct boot_rsp`。
9. `main()` 根据 `rsp.br_image_off` 写入 active slot 记录。
10. `do_boot(&rsp)` 校验向量表并跳转应用。

没有可用镜像时，Bootloader 打印错误并停留在循环中持续喂 IWDG。

## 镜像选择规则

镜像选择逻辑在 `Boot/bootutil/src/loader.c`。

可启动镜像必须满足：

- `ih_magic == IMAGE_MAGIC` (`0x96f3b83d`)。
- `ih_hdr_size + ih_img_size + ih_protect_tlv_size` 不溢出，且小于槽大小。
- 未设置 `IMAGE_F_ENCRYPTED_AES128` 或 `IMAGE_F_ENCRYPTED_AES256`。
- 未设置压缩标志。
- 未设置 `IMAGE_F_NON_BOOTABLE`。
- SHA-256 TLV 与实际计算结果一致。
- 镜像整体大小没有覆盖 trailer 区域。

版本比较使用 `boot_compare_version()`：

1. 先比较 major。
2. 再比较 minor。
3. 再比较 revision。
4. 只有定义 `MCUBOOT_VERSION_CMP_USE_BUILD_NUMBER` 时才比较 build number。
5. 版本相等时保留先遇到的候选槽，即 Primary 优先。

当前 Direct-XIP 端口不使用 image header 中的地址字段来决定是否启动镜像。
镜像真正的启动地址由所选槽位的 slot base 和 `ih_hdr_size` 共同决定。

## Image 槽内布局

每个应用槽大小为 `0x6000`。槽内从前到后依次是 MCUboot image header、应用代码 payload、TLV 区域、空闲填充区和 MCUboot trailer：

```text
slot_base + 0x0000
  ├─ Image header area      0x0000 .. 0x01FF   固定 0x200 字节
  │   └─ struct image_header 位于起始处，其余为 header padding
  ├─ Application payload    0x0200 .. 0x0200 + ih_img_size - 1
  │   └─ 应用向量表位于 payload 起始处
  ├─ Protected TLV          可选，长度 ih_protect_tlv_size
  ├─ Standard TLV           以 IMAGE_TLV_INFO_MAGIC 开始
  ├─ Padding / erased area  必须保持在 trailer 之前
  └─ Trailer                0x5FC0 .. 0x5FFF
slot_base + 0x6000
```

关键槽内 offset 如下：

| 区域 | 槽内 offset | 说明 |
| ---- | ----------- | ---- |
| image header | `0x0000..0x01FF` | `--header-size 0x200` |
| app vector/code 起点 | `0x0200` | 应用链接起始地址等于 slot base + `0x200` |
| TLV 起点 | `0x0200 + ih_img_size` | 先 protected TLV，再 standard TLV |
| trailer 起点 | `0x5FC0` | `boot_swap_info_off()` |
| 槽结束 | `0x6000` | 不包含结束 offset |

`struct image_header` 的有效字段只有 32 字节，但本工程按 `CY_IMG_HDR_SIZE = 0x200` 预留整个 header 区。关键字段包括：

| 字段 | 含义 |
| ---- | ---- |
| `ih_magic` | 必须为 `IMAGE_MAGIC` (`0x96f3b83d`) |
| `ih_load_addr` | header 中保留的地址字段；当前 Direct-XIP 端口不依赖它决定启动地址 |
| `ih_hdr_size` | 当前应为 `0x200` |
| `ih_protect_tlv_size` | protected TLV 总长度，参与 HASH 和签名 digest |
| `ih_img_size` | payload/code 长度，不包含 header |
| `ih_flags` | 加密、压缩、non-bootable、ROM fixed 等标志 |
| `ih_ver` | 镜像版本，供槽位选择时比较 |

TLV 区域由 `struct image_tlv_info` 和多个 `struct image_tlv` 条目组成。本工程当前按 EC256 签名镜像使用 TLV，选择如下：

| 区域 | 本项目当前状态 | `it_magic` | `it_tlv_tot` | 是否参与 HASH |
| ---- | -------------- | ---------- | ------------ | ------------- |
| protected TLV | 默认不使用，`ih_protect_tlv_size` 通常为 `0` | 如存在则为 `IMAGE_TLV_PROT_INFO_MAGIC` (`0x6908`) | protected TLV 区总长度，包含本 `image_tlv_info` | 是 |
| standard TLV | 必须存在，保存验签所需材料 | `IMAGE_TLV_INFO_MAGIC` (`0x6907`) | standard TLV 区总长度，包含本 `image_tlv_info` | 否 |

也就是说，按当前 README 的 `imgtool create --key signing-key.pem` 打包路径，镜像通常是：

```text
header + payload + standard TLV + padding + trailer
```

只有以后增加 security counter、dependency、boot record 等需要被签名保护的元数据时，才需要启用 protected TLV，并让 `ih_protect_tlv_size` 非 0。

每个 TLV 条目的字段如下：

| 字段 | 大小 | 含义 |
| ---- | ---- | ---- |
| `it_type` | 2 字节 | TLV 类型，例如 keyhash、SHA256、ECDSA 签名 |
| `it_len` | 2 字节 | value 长度，不包含 `it_type` 和 `it_len` 本身 |
| value | `it_len` 字节 | TLV 数据内容 |

当前签名镜像的 standard TLV 应包含以下条目：

| TLV | 类型值 | value 长度 | 当前用途 |
| --- | ------ | ---------- | -------- |
| `IMAGE_TLV_KEYHASH` | `0x01` | 32 字节 | 公钥 hash，用于匹配 `Boot/bootutil/src/keys.c` 中的 `bootutil_keys[]` |
| `IMAGE_TLV_SHA256` | `0x10` | 32 字节 | header + payload + protected TLV 的 SHA-256 digest |
| `IMAGE_TLV_ECDSA_SIG` | `0x22` | ASN.1 DER 编码，当前接收缓冲最大 128 字节 | EC256/ECDSA P-256 签名，解码后为 32 字节 `R` + 32 字节 `S` |

HASH 计算范围是：

```text
[slot_base, slot_base + ih_hdr_size + ih_img_size + ih_protect_tlv_size)
```

也就是 header area、payload 和 protected TLV。standard TLV 本身不在 HASH 输入范围内，但其中的 keyhash 和 ECDSA 签名会被 `image_validate.c` 扫描并用于验签。

trailer 是 Direct XIP Revert 的状态区，不属于 image header/payload/TLV。当前 `bootutil_max_image_size()` 返回 trailer 起点 `0x5FC0`，因此：

```text
ih_hdr_size + ih_img_size + ih_protect_tlv_size + standard_tlv_total <= 0x5FC0
```

如果 image header、payload、TLV 或 padding 覆盖 `0x5FC0..0x5FFF`，Bootloader 会把镜像视为越界或破坏 trailer 状态。

## 镜像校验

校验入口：

- `Boot/bootutil/src/bootuitl_loader.c`: `boot_check_image()`
- `Boot/bootutil/src/image_validate.c`: `bootutil_img_validate()`
- `Boot/bootutil/src/bootutil_img_hash.c`: `bootutil_img_hash()`
- `Boot/bootutil/src/bootutil_sha_stm32u5_hash.c`: STM32 HASH 外设后端
- `Boot/bootutil/src/image_ecdsa.c`: ECDSA 签名验证入口
- `Boot/bootutil/src/bootutil_ecdsa_stm32u5_pka.c`: STM32U5 PKA ECDSA 后端
- `Boot/bootutil/src/bootutil_ecdsa_tinycrypt.c`: TinyCrypt ECDSA 后端

SHA-256 计算范围：

```text
[slot_base, slot_base + ih_hdr_size + ih_img_size + ih_protect_tlv_size)
```

也就是 image header、payload 和 protected TLV 区域。随后扫描 protected TLV 和 standard TLV，EC256 签名配置下会使用该 digest 验证 `IMAGE_TLV_ECDSA_SIG`。

当前配置启用 SHA-256 digest 计算和 EC256 签名验真：

```c
#define MCUBOOT_SIGN_EC256 1
#define MCUBOOT_SHA256_BACKEND_STM32_HASH 1
#define MCUBOOT_ECDSA_BACKEND_STM32U5_PKA 1
```

`image_validate.c` 会先计算 SHA-256，再查找 keyhash TLV 和 ECDSA 签名 TLV。keyhash 必须能匹配 `Boot/bootutil/src/keys.c` 中内置公钥，随后才调用 ECDSA 后端验证签名。只带 `IMAGE_TLV_SHA256` 而没有有效 `IMAGE_TLV_KEYHASH`/`IMAGE_TLV_ECDSA_SIG` 的镜像不会通过当前验真路径。

如果取消 HASH 硬件后端宏，`Boot/bootutil/include/bootutil/crypto/sha.h` 会回退到 TinyCrypt SHA-256 包装层。但当前 Keil/EIDE 工程中虽然仍包含 TinyCrypt 源文件，HASH 主路径使用的是 STM32 HASH 外设。

## HASH 适配层和可移植性

本工程把镜像校验逻辑和具体 SHA-256 实现解耦。上层校验代码只依赖 `Boot/bootutil/include/bootutil/crypto/sha.h` 暴露的统一接口：

```c
int bootutil_sha_init(bootutil_sha_context *ctx);
int bootutil_sha_update(bootutil_sha_context *ctx,
                        const void *data,
                        uint32_t data_len);
int bootutil_sha_finish(bootutil_sha_context *ctx,
                        uint8_t *output);
int bootutil_sha_drop(bootutil_sha_context *ctx);
```

`bootutil_img_hash()` 负责从 Flash 分块读取镜像数据，调用上述接口持续喂入数据并输出 32 字节 SHA-256 digest；`image_validate.c` 只拿 digest 去比对 `IMAGE_TLV_SHA256`。因此镜像格式、TLV 扫描、槽位选择、Direct XIP Revert 都不关心底层 HASH 来自硬件还是软件。

### 硬件 HASH 后端

当前默认启用 STM32U5 HASH 外设：

```c
#define MCUBOOT_SHA256_BACKEND_STM32_HASH 1
```

对应实现位于 `Boot/bootutil/src/bootutil_sha_stm32u5_hash.c`，依赖 `Core/Src/hash.c` 中的全局 `HASH_HandleTypeDef hhash` 和 STM32 HAL HASH API。

硬件后端的关键职责：

- `bootutil_sha_init()` 调用 `HAL_HASH_Init()` 初始化 HASH 外设。
- `bootutil_sha_update()` 把任意长度字节流整理成 HASH 外设适合处理的数据。
- `bootutil_sha_finish()` 调用 `HAL_HASHEx_SHA256_Accmlt_End()` 或 `HAL_HASHEx_SHA256_Start()` 输出 digest。
- `bootutil_sha_drop()` 调用 `HAL_HASH_DeInit()` 并清空上下文。

STM32 HASH 外设路径额外处理了两个移植细节：

- 对 4 字节未对齐的输入指针，会拷贝到 `tail_word` 后再送入 HAL。
- 对非 4 字节倍数的数据尾部，会暂存在 `tail_word/tail_len`，最后一次性完成 HASH。

这让 `bootutil_img_hash()` 可以继续按普通字节缓冲区工作，不需要知道硬件外设的对齐约束。当前实现使用全局 `hhash` 和阻塞式 HAL 调用，适合 Bootloader 单线程启动场景；如果以后迁移到 RTOS 或并发环境，需要增加互斥或改成实例化上下文。

### 软件 HASH 后端

软件后端使用 TinyCrypt。`sha.h` 中保留了这一套包装：

```c
typedef struct tc_sha256_state_struct bootutil_sha_context;

tc_sha256_init(ctx);
tc_sha256_update(ctx, data, data_len);
tc_sha256_final(output, ctx);
```

在 `mcuboot_config.h` 里取消 `MCUBOOT_SHA256_BACKEND_STM32_HASH` 后，会进入 fallback 分支：

```c
#define MCUBOOT_USE_TINYCRYPT 1
#define MCUBOOT_SHA256_BACKEND_TINYCRYPT 1
```

TinyCrypt 后端的优点是可移植性强，不依赖 MCU HASH 外设，也更适合主机侧单元测试或没有 HASH 外设的芯片。代价是速度较慢，并且会占用更多 CPU 时间。

### 移植到其他芯片

移植时优先保持 `bootutil_sha_*` 接口不变。推荐顺序：

1. 如果目标芯片没有稳定的 HASH 外设，直接使用 TinyCrypt 软件后端，通常无需改 `bootutil_img_hash()` 和 `image_validate.c`。
2. 如果目标芯片有 HASH/SHA 加速器，新建类似 `bootutil_sha_xxx_hash.c` 的后端文件，实现 `bootutil_sha_init/update/finish/drop`。
3. 在 `mcuboot_config.h` 中增加一个互斥后端宏，例如 `MCUBOOT_SHA256_BACKEND_VENDOR_HASH`。
4. 在 `crypto/sha.h` 中为新宏选择新的 `bootutil_sha_context` 类型和函数声明。
5. 把硬件后端的对齐、分块、尾包、超时、错误码转换都封装在后端内部，不让上层镜像校验代码感知。

这个边界是本工程可移植性的核心：Flash 读取由 `flash_area_*` 适配层隔离，SHA-256 计算由 `bootutil_sha_*` 适配层隔离。换芯片时优先替换这两层，启动状态机和 TLV 校验逻辑应尽量保持不变。

## 软硬件签名验真

当前工程启用 MCUboot EC256 签名验真：

```c
#define MCUBOOT_SIGN_EC256 1
#define MCUBOOT_ECDSA_BACKEND_STM32U5_PKA 1
/* #define MCUBOOT_ECDSA_BACKEND_TINYCRYPT 1 */
#define MCUBOOT_USE_TINYCRYPT 1
```

签名算法固定为 ECDSA P-256，签名输入是 `bootutil_img_hash()` 计算出的 32 字节 SHA-256 digest。`image_validate.c` 的验真顺序为：

1. 计算镜像 header、payload 和 protected TLV 的 SHA-256。
2. 扫描 TLV，查找 `IMAGE_TLV_KEYHASH`。
3. 用 `bootutil_find_key()` 将 keyhash 匹配到 `Boot/bootutil/src/keys.c` 中的内置公钥。
4. 扫描 `IMAGE_TLV_ECDSA_SIG`，读取 ASN.1 DER 编码的 ECDSA 签名。
5. 调用 `bootutil_verify_sig()`，再分派到当前选中的 ECDSA 后端。

当前端口不启用 `MCUBOOT_BUILTIN_KEY` 和 `MCUBOOT_HW_KEY`，使用 MCUboot 默认 keyhash 流程：镜像 TLV 中放公钥 hash，Bootloader 固件中内置完整公钥。`keys.c` 顶部标注为 imgtool 生成文件，正式项目应使用受控私钥重新生成公钥数组，不能继续使用调试密钥。

### 硬件签名后端

默认后端是 STM32U5 PKA：

```c
#define MCUBOOT_ECDSA_BACKEND_STM32U5_PKA 1
```

对应实现位于 `Boot/bootutil/src/bootutil_ecdsa_stm32u5_pka.c`，依赖 `Core/Src/pka.c` 中的全局 `PKA_HandleTypeDef hpka` 和 HAL PKA API。`Core/Src/main.c` 会在进入启动决策前调用 `MX_PKA_Init()`，`Core/Inc/stm32u5xx_hal_conf.h` 也需要启用 `HAL_PKA_MODULE_ENABLED`。

硬件后端的职责：

- 解析内置 DER 公钥，提取未压缩 P-256 公钥点。
- 解析镜像 TLV 中的 ASN.1 DER ECDSA 签名，拆成 32 字节 `R` 和 32 字节 `S`。
- 填充 P-256 曲线参数、消息 digest、公钥和签名字段。
- 调用 `HAL_PKA_ECDSAVerif()`，再用 `HAL_PKA_ECDSAVerif_IsValidSignature()` 判断签名是否有效。
- 在耗时操作前后喂 IWDG，避免硬件验签期间看门狗复位。

硬件后端适合 STM32U5 目标板上的正式启动路径，速度更快，CPU 占用更低，但移植时必须确认目标芯片 PKA/ECC 外设的曲线参数格式、字节序、超时和错误码语义。

### 软件签名后端

软件后端是 TinyCrypt ECDSA：

```c
/* #define MCUBOOT_ECDSA_BACKEND_STM32U5_PKA 1 */
#define MCUBOOT_ECDSA_BACKEND_TINYCRYPT 1
#define MCUBOOT_USE_TINYCRYPT 1
```

对应实现位于 `Boot/bootutil/src/bootutil_ecdsa_tinycrypt.c`。它复用同一套 ASN.1 公钥/签名解析逻辑，最后调用 `uECC_verify(..., uECC_secp256r1())` 完成 P-256 验签。

软件后端的优点是可移植性好，适合没有 PKA 外设的芯片、早期 bring-up 或主机侧验证。代价是验签耗时更长，并且会占用更多 CPU 时间。切换后端时必须保证 `MCUBOOT_ECDSA_BACKEND_STM32U5_PKA` 和 `MCUBOOT_ECDSA_BACKEND_TINYCRYPT` 只定义一个；`mcuboot_config.h` 已经用预处理检查强制这一点。

### 签名镜像要求

应用镜像必须由与 `keys.c` 内置公钥匹配的私钥签名。当前验真路径需要镜像至少包含：

| TLV | 类型值 | 作用 |
| --- | ------ | ---- |
| `IMAGE_TLV_KEYHASH` | `0x01` | 公钥 hash，用于从 `bootutil_keys[]` 中选择公钥 |
| `IMAGE_TLV_SHA256` | `0x10` | 镜像 digest，便于工具和兼容路径识别 |
| `IMAGE_TLV_ECDSA_SIG` | `0x22` | ECDSA P-256 签名 |

如果更换签名私钥，需要同步更新两处：一是用新私钥重新签名应用镜像，二是把对应公钥重新生成到 `Boot/bootutil/src/keys.c`。只替换私钥或只替换公钥都会导致 `bootutil_find_key()` 或 ECDSA 验签失败。

## 槽位 trailer 和 Direct XIP Revert

当前配置：

```c
#define MCUBOOT_DIRECT_XIP 1
#define MCUBOOT_DIRECT_XIP_REVERT 1
#define MCUBOOT_BOOT_MAX_ALIGN 16
```

每个槽末尾保留 MCUboot trailer。STM32U5 Flash 写入粒度为 16 字节 QUADWORD，因此 trailer flag 也按 16 字节对齐写入。

对当前 `0x6000` 槽位，关键 offset 为：

| 字段 | offset | 绝对地址 Primary | 绝对地址 Secondary |
| ---- | ------ | ---------------- | ------------------ |
| `swap_info` | `0x5FC0` | `0x08015FC0` | `0x0801BFC0` |
| `copy_done` | `0x5FD0` | `0x08015FD0` | `0x0801BFD0` |
| `image_ok` | `0x5FE0` | `0x08015FE0` | `0x0801BFE0` |
| `magic` | `0x5FF0` | `0x08015FF0` | `0x0801BFF0` |

`bootutil_max_image_size()` 返回 `boot_swap_info_off(fap)`，所以当前镜像的 header + payload + TLV 最多只能使用到 `0x5FC0` 之前。

Direct XIP Revert 状态机：

```text
首次选择镜像:
  magic 必须有效
  copy_done 未设置 -> Bootloader 写 copy_done
  跳转应用

应用确认成功:
  应用调用 boot_set_confirmed()
  写 image_ok

下次启动:
  copy_done 已设置且 image_ok 已设置 -> 可继续启动

应用没有确认:
  copy_done 已设置但 image_ok 未设置
  Bootloader 判定上次启动失败
  擦除该槽
  尝试另一个槽
```

`boot_set_confirmed()` 会先读取 User Area 中的 active slot 记录，再确认实际运行槽的 trailer。Primary 和 Secondary XIP 运行路径都通过同一套 `boot_set_next(fap, true, true)` 写入 `copy_done` 和 `image_ok`。

## 应用跳转安全检查

`do_boot()` 在跳转前会读取应用向量表：

```text
app_addr = rsp.br_image_off + rsp.br_hdr->ih_hdr_size
sp       = *(uint32_t *)(app_addr + 0)
reset    = *(uint32_t *)(app_addr + 4)
```

`Boot/bootutil/src/boot_jump.h` 检查：

- `app_addr` 必须 128 字节对齐，满足 VTOR 对齐要求。
- `image_size` 至少 8 字节，且地址运算不能溢出。
- SP 必须 8 字节对齐。
- SP 必须落在 STM32U5 SRAM1 到 SRAM5 范围。
- Reset handler bit0 必须为 1，表示 Thumb 状态。
- Reset handler 去掉 bit0 后必须落在应用镜像范围内。

校验通过后，`do_boot()` 会：

1. 关闭全局中断。
2. 禁止 SysTick 并清除 pending SysTick/PendSV。
3. 清除 8 组 NVIC ICER/ICPR，覆盖 STM32U5A9 的 139 个外部 IRQ。
4. `HAL_DeInit()`。
5. 清理 BASEPRI、FAULTMASK、PSP、CONTROL、MSPLIM、PSPLIM。
6. 设置 `SCB->VTOR = app_addr`。
7. 设置 MSP。
8. 重新开中断。
9. 跳转到应用 Reset handler。

## YMODEM 下载流程

下载实现位于 `Ymodem/ymodem.c`，使用 USART1 轮询接收。

```text
Bootloader                         Host
    |                                |
    |---- 'C' ---------------------->|  请求 CRC16 模式
    |<--- Packet 0 ------------------|  文件名 + 文件大小
    |---- ACK ---------------------->|
    |---- 'C' ---------------------->|  请求数据包
    |<--- Packet 1..N ---------------|  SOH 128B 或 STX 1024B
    |---- ACK ---------------------->|
    |<--- EOT -----------------------|
    |---- ACK ---------------------->|
    |<--- Empty Packet --------------|
    |---- ACK ---------------------->|
```

接收行为：

- Packet 0 解析文件名和文件大小。
- 如果文件大小大于目标槽大小，发送 `CA CA` 并中止。
- 首个有效文件包到来后先擦除整个目标槽。
- 数据包按 YMODEM packet 长度写入 Flash。
- 写入后按 32 字节分块读回比对。
- 连续错误超过 `MAX_ERRORS` 后中止。
- 成功或失败都会关闭 `flash_area`。

下载目标由 active slot 决定：

| 当前 active slot | 下载目标 |
| ---------------- | -------- |
| Primary | Secondary |
| Secondary | Primary |

当前代码下载结束后自动 `NVIC_SystemReset()`，不需要手动复位。


## Flash 适配层

`Boot/flash_map_backend/cy_flash_map.c` 实现 MCUboot 需要的 `flash_area_*` API。

### 读取

内部 Flash 是 memory mapped，读取直接 `memcpy()`：

```c
addr = fa->fa_off + off;
memcpy(dst, (const void *)addr, len);
```

### 擦除

- 页大小来自 `FLASH_PAGE_SIZE`，STM32U5 当前为 8 KB。
- 擦除要求地址和长度都按页对齐。
- 当前分区位于 lower 2 MB / Bank 1，擦除时固定使用 `FLASH_BANK_1`。
- 按页逐页擦除，每页后调用 `IWDG_Feed()`。

### 写入

- STM32U5 使用 16 字节 QUADWORD 编程。
- `flash_area_write()` 要求写入地址和长度都按 16 字节对齐。
- 如果某个 16 字节块全是 `0xFF`，会跳过编程，避免对已擦除值做无意义写入。

这与 MCUboot trailer 的 16 字节 flag 单元匹配。

## User Area 和 active slot 记录

`Boot/bootutil/src/boot_active_slot_flash.c` 在 User Area 保存上一次 Bootloader 选择并跳转的槽位。

User Area：

```text
base = 0x081FE000
size = 0x2000
```

记录结构：

```c
typedef struct {
    uint32_t magic;           // 0x41534C54, "ASLT"
    uint8_t  active_slot;     // 0 = Primary, 1 = Secondary
    uint8_t  active_slot_inv; // active_slot 的按位取反
    uint16_t reserved;        // 0xFFFF
    uint32_t checksum;        // magic ^ slot ^ (slot_inv << 8) ^ 0x5A5AA5A5
    uint32_t reserved2;       // 0xFFFFFFFF
} boot_active_slot_record_t;
```

当前结构大小为 16 字节，正好满足 Flash 16 字节写入对齐。

读取校验：

- magic 必须为 `0x41534C54`。
- active slot 必须为 Primary 或 Secondary。
- `active_slot_inv` 必须等于 `~active_slot`。
- checksum 必须匹配。

任一校验失败都会默认返回 Primary。

写入时，如果记录已经是目标槽，直接返回；否则擦除整个 8 KB User Area 后写入新记录。因此不要在当前 User Area 中混放其他持久数据，除非同时改造擦写策略。

## 应用镜像制作

当前调试分区下，应用需要按运行槽位分别链接。应用向量表地址为 slot base + header size。

Primary 应用链接建议：

```text
FLASH ORIGIN = 0x08010200
FLASH LENGTH <= 0x5DC0，并额外预留 TLV/填充空间
```

Secondary 应用链接建议：

```text
FLASH ORIGIN = 0x08016200
FLASH LENGTH <= 0x5DC0，并额外预留 TLV/填充空间
```

打包参数必须匹配 Bootloader 当前分区。

### 密钥管理

Bootloader 当前使用 `Boot/bootutil/src/keys.c` 中内置的 ECDSA P-256 公钥做 keyhash 匹配和签名验真。
如果要替换签名密钥，需要同时更新：

1. 私钥文件，例如 `keys/root-ec-p256.pem`
2. Bootloader 内置公钥文件 `Boot/bootutil/src/keys.c`

生成新的 ECDSA P-256 私钥：

```bash
imgtool keygen -k keys/root-ec-p256.pem -t ecdsa-p256
```

从私钥导出公钥数据：

```powershell
imgtool getpub -k keys/root-ec-p256.pem | Set-Content C:\tmp\pubkey.c -Encoding ascii
```

`C:\tmp\pubkey.c` 中只包含 `ecdsa_pub_key[]` 和 `ecdsa_pub_key_len`。
不要直接用 `>` 覆盖 `Boot/bootutil/src/keys.c`，否则会丢失当前工程需要的 `#include`、`bootutil_keys[]`、`bootutil_key_cnt` 包装层，而且在 PowerShell 下很容易被写成 UTF-16，导致编译报错。

正确做法是：保留 `Boot/bootutil/src/keys.c` 的现有模板结构，只把新导出的：

- `ecdsa_pub_key[]`
- `ecdsa_pub_key_len`

替换进去，然后重新编译并烧录 Bootloader。

`--key` 指向的私钥必须对应 `Boot/bootutil/src/keys.c` 中内置的公钥，否则 Bootloader 会在 keyhash 匹配或 ECDSA 验签阶段拒绝镜像。当前 Bootloader 只支持 EC256/ECDSA P-256 签名，应用打包时不要使用 RSA、Ed25519 或加密/压缩相关选项。

### 生成签名固件

Direct-XIP 下不要依赖 header 中的 `ih_load_addr` 参与启动决策。
应用应按运行槽位分别链接，启动地址始终由 slot base + header size 决定：

- Primary vector: `0x08010200`
- Secondary vector: `0x08016200`

下面的命令假定你从仓库根目录 `E:\Simple_ST\Boot` 执行。

Primary 固件签名：

```bash
imgtool sign --sha 256 --header-size 0x200 --pad-header --align 16 --version 1.0.0 --slot-size 0x6000 --key ..\..\keys\root-ec-p256.pem --pad primary.bin primary-signed.bin
```

Secondary 固件签名：

```bash
  imgtool sign --sha 256 --header-size 0x200 --pad-header --align 16 --version 1.0.0 --slot-size 0x6000 --key ..\..\keys\root-ec-p256.pem --pad secondary.bin secondary-signed.bin
```

如果需要检查签名产物，可以执行：

```bash
imgtool dumpinfo test_app/secondary/secondary-signed.bin
```


## 关键源码索引

| 文件 | 责任 |
| ---- | ---- |
| `Core/Src/main.c` | 系统初始化、下载模式判断、调用 `boot_go()`、写 active slot、跳转应用 |
| `Boot/sysflash/sysflash.h` | Flash 基址、槽大小、User Area 定义 |
| `Boot/flash_map_backend/cy_flash_map.c` | STM32U5 Flash read/write/erase 适配 |
| `Boot/mcuboot_config/mcuboot_config.h` | Direct XIP、Revert、HASH 后端等配置 |
| `Boot/bootutil/include/bootutil/crypto/sha.h` | SHA-256 统一适配接口，选择硬件或软件后端 |
| `Boot/bootutil/include/bootutil/crypto/ecdsa.h` | ECDSA P-256 统一适配接口，选择 PKA 或 TinyCrypt 后端 |
| `Boot/bootutil/src/loader.c` | 槽选择、版本比较、回退擦除、启动响应、最终跳转 |
| `Boot/bootutil/src/bootuitl_loader.c` | header 读取/校验、版本比较、image validate 调用 |
| `Boot/bootutil/src/image_validate.c` | TLV 扫描、SHA-256 digest 计算结果比对、keyhash 匹配和签名 TLV 分派 |
| `Boot/bootutil/src/image_ecdsa.c` | ECDSA 验签公共入口，解析内置公钥并调用当前 ECDSA 后端 |
| `Boot/bootutil/src/bootutil_ecdsa_stm32u5_pka.c` | STM32U5 PKA ECDSA P-256 硬件验签后端 |
| `Boot/bootutil/src/bootutil_ecdsa_tinycrypt.c` | TinyCrypt ECDSA P-256 软件验签后端 |
| `Boot/bootutil/src/bootutil_find_key.c` | 根据镜像 keyhash 匹配内置公钥 |
| `Boot/bootutil/src/keys.c` | imgtool 生成的内置 ECDSA 公钥 |
| `Boot/bootutil/src/bootutil_img_hash.c` | 镜像 HASH 计算 |
| `Boot/bootutil/src/bootutil_sha_stm32u5_hash.c` | STM32 HASH 外设 SHA-256 后端 |
| `Boot/bootutil/src/bootutil_public.c` | trailer magic、copy_done、image_ok、pending/confirmed API |
| `Boot/bootutil/src/boot_active_slot_flash.c` | User Area active slot 持久化 |
| `Boot/bootutil/src/boot_jump.h` | VTOR、SP、Reset handler 合法性检查 |
| `Ymodem/ymodem.c` | YMODEM 接收、擦写目标槽、写后校验 |
| `Core/Src/usart.c` | USART1/2 初始化、`printf` 串口重定向 |
| `Core/Src/hash.c` | HASH 外设初始化 |
| `Core/Src/pka.c` | PKA 外设初始化 |
| `Core/Src/iwdg.c` | IWDG 初始化和喂狗 |
