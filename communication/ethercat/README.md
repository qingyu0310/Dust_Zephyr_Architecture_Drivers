# EtherCAT 从站协议栈

EtherCAT 从站（Slave）实现。基于 Beckhoff SSC 5.13 的协议语义重写，不是 SSC 的 C++ 包装。

- **当前状态**：已在 HPM6E00 + 双 JL1111 + 板载 EEPROM SII 上跑通 OP 稳态与断链恢复
- **协议来源**：SSC 5.13（许可见 [core/LICENSE-BECKHOFF.md](core/LICENSE-BECKHOFF.md)）
- **业务类型**：由使用方通过模板参数声明，核心只认字节与字数

---

## 1. 为什么要重构

### 1.1 SSC 是什么

SSC（Slave Stack Code）是 Beckhoff 提供的参考从站实现，五个 `.c` 文件约 9000 行，全部用全局变量传递状态：

- `ecatslv.c`（2795 行）同时管 AL 状态机、SM 通道、DC 看门狗、SM 区校验
- `ecatappl.c`（1202 行）同时管 ISR 入口、PDO 搬运、应用钩子、SII 处理
- `coeappl.c`（945 行）同时管 CoE 分发、标准对象初始化、字典链表、时间戳

状态散在文件级全局变量里，任何一处都能改任何状态。直接把它搬进 C++ 项目，编译器不会帮你发现任何越界——因为所有东西都在同一作用域。

### 1.2 第一次重构：逐文件改名（已废弃）

第一版做法是「把 SSC 的每个 `.c` 改名成 `.hpp/.cpp`，每个文件一个类，成员照搬全局变量」。

结果被判为「完完全全对着 SSC 生硬改成 C++ 风格」，全部回滚。四个具体病灶：

| 病灶 | 具体表现 |
| --- | --- |
| **切割轴错了** | 按「原来是哪个文件」分，不是按「谁持有状态 / 谁做决定 / 谁执行」分 |
| **常量表搬进 RAM** | 280 行的对象表用运行期 `memcpy` 初始化，白占 RAM |
| **风格只做词法替换** | `static void foo(void)` → `void Foo()`，结构没动 |
| **死代码原样继承** | 8 个 `return 0` 空桩被状态机 30 多处调用 |

最致命的是第一点：文件级切分之后，`EcatStateMachine` 依然要同时持有 Mailbox、ProcessData、Sync、SII、Dictionary 五个引用，只为了「能调到」。

### 1.3 第二次重构：按「状态归属」重切

第二版的第一原则是：

> **切割轴 = 「谁持有状态 / 谁做决定 / 谁执行」，不是「原来是哪个文件」。**

判据很硬：**如果一个类的所有方法都只是把参数转发给另一个类，这个类就不该存在。**

于是 7 个 `.c` 被重新切成 9 个类（映射非一对一）：

| 原文件 | 拆分结果 | 理由 |
| --- | --- | --- |
| `ecatslv.c` | `EcatStateMachine` + `EcatProcessData` + `EcatSync` | 混了 AL 状态机 / PDO 通道 / DC 看门狗 |
| `ecatappl.c` | `EcatDevice` + `EcatProcessData` + `EcatSiiService` + `EcatDiagnostics` | 混了 ISR 入口 / PDO 搬运 / SII / 诊断 |
| `coeappl.c` | `EcatCoe` + `EcatDictionary` + `EcatSync` | 混了服务分发 / 字典 / 时间戳 |

### 1.4 第三次修正：拆掉对象之间的相互注册

第二次重构后，类的**边界**对了，但**关系**没对。`EcatDevice::Init()` 里是这样接线：

```cpp
sync_.BindParameters(dictionary_.Data().sync_manager_out, dictionary_.Data().sync_manager_in);
state_machine_.Bind(mailbox_, process_data_, sync_, sii_service_);
state_machine_.BindDictionary(dictionary_);
dictionary_.BindStateQuery(QueryState, this);
dictionary_.BindTimestamp(QueryTimestamp, this);
sdo_.Bind(dictionary_, SubmitReply, this);
sdo_info_.Bind(dictionary_, SubmitReply, this);
coe_.Bind(sdo_, sdo_info_);
mailbox_.SetDispatcher(DispatchMailbox, this);
```

这叫「父对象拥有所有子对象，子对象之间还互相注册」——一张关系网，不是一棵树。具体病灶：

| 环 | 问题 |
| --- | --- |
| Sync 借用 Dictionary 的可写成员 | 同步参数有两份可写真值 |
| StateMachine 持有五个兄弟引用 | 状态机不是「决策者」而是「调度器」 |
| Dictionary 回调 Device 查状态/时间戳 | 下层反向调用上层 |
| SDO → `SubmitReply` → Device → Mailbox | 服务自己找通道交应答 |
| Mailbox → CoE 回调 → Device → SDO → Device → Mailbox | 五跳回环 |

第三次重构定的目标：

> **父对象单向驱动自己拥有的子对象；子对象只接收本次操作所需的数据并返回结果，不保存兄弟对象、不调用上层、不通过回调绕回兄弟对象。**

---

## 2. 当前架构

### 2.1 分层

```
project/thread/<业务>            板卡、业务类型、策略的组装位置
  EthercatSlave<Rx, Tx>          模板门面：类型检查、业务编解码、外部生命周期
    EcatDevice                   生命周期与事件入口，不实现报文算法（非模板）
      AL 会话    EcatStateMachine        状态决策
      PDO 会话   EcatProcessData / EcatSync   缓冲、同步锁存、通道操作
      邮箱会话   EcatMailbox / EcatSdo / EcatSdoInfo
      对象访问   EcatDictionary           元数据查找、权限、线格式读写
      SII        EcatSiiService
      诊断       EcatDiagnostics
  EcatEscHpm / PhyJl1111         调用方拥有，寿命覆盖 Slave
```

### 2.2 文件结构

```
ethercat/
├── ethercat.hpp                     模板门面 EthercatSlave<Rx, Tx>（实现内联）
├── interface/                       硬件契约层，零厂商符号
│   ├── ecat_esc.hpp                 EcatEsc 纯虚：寄存器/DPRAM/事件/MDIO/SII
│   ├── ecat_profile.hpp             PdoProfile<T> / SlaveProfile<Rx,Tx> / DeviceProfile
│   └── ecat_types.hpp               协议值类型（AlState / PdoLayout / 错误码…）
├── core/                            协议核心，不含任何 hpm_ 前缀符号
│   ├── ecat_device.{hpp,cpp}        组合根：事件入口 + AL 执行编排
│   ├── al/                          状态与同步
│   │   ├── ecat_state_machine.*     只做决策，输出 AlStep
│   │   ├── ecat_sync.*              0x1C32/0x1C33 的唯一权威持有者
│   │   └── ecat_sm.*                SM 通道寄存器叶子
│   ├── pdo/
│   │   └── ecat_process_data.*      两方向缓冲、映射解析、区域校验
│   ├── mailbox/                     邮箱会话
│   │   ├── ecat_mailbox.*           SM0/SM1 传输、帧池、队列、重发
│   │   ├── ecat_sdo.*               SDO 服务（只返回结果，不持通道）
│   │   ├── ecat_sdo_info.*          SDO Info
│   │   ├── ecat_coe.*               无状态的 CoE 服务分类
│   │   └── ecat_sii_service.*       EEPROM 命令解释
│   ├── model/
│   │   └── ecat_dictionary.*        对象字典：查找、读写、CA、权限
│   ├── diag/
│   │   └── ecat_diagnostics.*       观察快照、首次故障冻结、按需回放
│   └── spec/                        编译期常量，全部 0 B RAM
│       ├── ecat_limits.hpp          容量/超时/身份常量
│       ├── ecat_registers.hpp       寄存器偏移
│       ├── ecat_object.hpp          对象类型与访问权限位
│       ├── ecat_application_objects.hpp  标准 + 应用对象描述表
│       └── ecat_wire.hpp            小端读写
```

**硬件实现**在 `zephyr_user/drivers/ethercat/hal/hpm/ecat_hal_hpm.{hpp,cpp}`（子树层），**PHY 驱动**在 `framework/drivers/device/ecat_phy/`（设备模块层）。

### 2.3 依赖方向

```
业务 (project)
    ↓
EthercatSlave<Rx,Tx>
    ↓
EcatDevice ──→ 各协议域（单向）
    ↓
EcatEsc（纯虚）
    ↓
EcatEscHpm（子树 drivers/ethercat/hal/hpm）
```

**只向下，不向上，不横向。** 两条可 grep 验证的硬约束：

```bash
# core/ 与 interface/ 里不得出现厂商符号
grep -rn "hpm_\|HPM_" core/ interface/     # → 空（仅注释提及）

# HAL 里不得出现协议符号
grep -rn "AlState\|EcatSdo\|kAlStatus" drivers/ethercat/hal/hpm/   # → 空
```

### 2.4 对象间关系的三条规则

| 关系 | 规则 |
| --- | --- |
| 父 → 自己拥有的子 | 允许，直接调用并检查返回值 |
| 算法 → 描述表 / 字节视图 / 状态值 | 允许，借用止于本次调用 |
| 硬件操作 → `EcatEsc` | 允许，不接触具体 HPM SDK |
| **SDO → Dictionary 实例** | **禁止**，改由父层传入值与结果 |
| **Sync → Dictionary 可写成员** | **禁止**，同步参数由 Sync 自己拥有 |
| **StateMachine → 多个协议域** | **禁止**，状态机只判断事实 |
| **下层 → Device 状态/时间戳回调** | **禁止**，每次操作显式提供快照 |
| 服务指针集合 / 万能 Context / `friend` | **禁止** |

### 2.5 关键设计点

#### 状态机只决策，不执行

`EcatStateMachine` 不持有任何兄弟域，只输出一个 `AlStep`：

```cpp
struct AlStep
{
    AlAction action = AlAction::None;   // EnterSafeOp / LeaveOp / Publish …
    uint16_t code   = 0;                // 该动作要用的 AL 状态码
    AlState  target = AlState::Init;
    bool     ack    = false;
};
```

`EcatDevice::ExecuteAlStep()` 负责把动作落到硬件。挖出来的好处是状态机的判断逻辑可以单独读、单独测，不用在 1000 行里翻。

#### 服务返回结果，父层提交

SDO / SDO Info 的 `Handle()` 返回：

```cpp
struct Result
{
    uint8_t  code   = 0;   // 非 0 为邮箱错误码
    uint16_t length = 0;   // 应答总字节数，0=无应答
};
```

应答原地写在收到的帧缓冲里（本就在池里），由 `EcatMailbox::CompleteReceived()` 统一提交。服务不再持 `ReplySink`，`Device::SubmitReply` 这个回环被删掉。

#### 业务类型由模板参数定死

```cpp
// project/thread/test/trd_test.cpp
struct PdoOutputs { uint32_t counter[64]; };
struct PdoInputs  { uint32_t counter[64]; };

using Slave = ecat::EthercatSlave<PdoOutputs, PdoInputs>;

static Slave slave_ {};

bool thread_init()
{
    static ecat::EcatEscHpm esc {};
    static ::PhyJl1111 phy0 {};
    static ::PhyJl1111 phy1 {};
    static ::EcatPhy* const phys[] = { &phy0, &phy1 };

    return slave_.Init(&esc, phys);
}

static TaskResult Task(void*)
{
    PdoInputs inputs {};
    // …填 inputs…
    slave_.SetInputs(inputs);
    slave_.Poll();

    const PdoOutputs& outputs = slave_.Outputs();   // 强类型，不是裸字节
    return TaskResult::Continue;
}
```

模板只在两处出现：`EthercatSlave<Rx, Tx>` 和 `SlaveProfile<Rx, Tx>`。`EcatDevice` 及以下全部非模板，通过运行期 `DeviceProfile` 接收布局：

```
SlaveProfile<Rx,Tx>  ──sizeof(T)──→  DeviceProfile{rx_bytes, tx_bytes, rx_words, tx_words}
                                            ↓
                              dictionary_.SetProfile() / process_data_.SetProfile()
                                            ↓
                     映射表条目数、PDO 长度、CA 长度、子索引范围全部跟着变
```

**改业务结构体，核心代码一行不用动。**

### 2.6 线程 Task 必须单拍

`Task()` 每次只推一次 `Poll()` 就返回。绝不许在里面写 `while` 死循环或阻塞调用——`Thread` 外壳的计时、统计、故障钩子全在「任务体返回之后」执行，任务体不返回就等于绕过整个线程框架（`thread show` 里 `task last` 会永远是 `0.000 us`）。

```cpp
static TaskResult Task(void* context)
{
    slave_.Poll();                  // 单拍，立刻返回
    return TaskResult::Continue;
}
```

---

## 3. 初始化顺序

顺序不能改，每一步都有理由：

| 步 | 操作 | 为什么 |
| --- | --- | --- |
| 1 | `trd` 声明 ESC、PHY、Rx/Tx 类型 | 寿命必须覆盖事件和会话 |
| 2 | `Prepare()`：接域 → `esc.Open()` 注册回调（不开事件） | 中断进来时域必须已接好 |
| 3 | 读 CommInfo 实际 SM 数，禁用全部通道，清 AL 事件掩码，发布 INIT | 不用固定 4 通道代替实际通道数 |
| 4 | 初始化各实际 PHY 端口（MDIO 地址由 trd 给） | 失败逆序清理，不留可运行 Device |
| 5 | `SetOutputSink` / `SetInputSource` | 业务回调要在事件开启前就位 |
| 6 | `Start()`：置 `ready_` → `StartEvents()` | **解决「事件早于 PHY 与业务回调」的窗口** |

第 6 步是这次重构修掉的一个实际缺陷：原实现在 `Init` 里就 `StartEvents()`，此后才初始化 PHY、才装业务回调，中间那段时间中断随时可能进来碰到半成品。

**销毁顺序**：阻止新事件 → 停邮箱/SDO/Info → 停同步 → 清 PDO → 发布 INIT → 处理待持久化 SII → 关硬件。

---

## 4. 内存与资源

| 项 | 约定 |
| --- | --- |
| **动态分配** | **零**。无 `new` / `malloc` / `unique_ptr` / `std::vector` |
| **常量表** | 全部 `constexpr`，0 B RAM |
| **邮箱缓冲** | 静态池 24 块 × 128 B，位图分配 |
| **PDO 缓冲** | 两方向各 256 B（容量上限，实际长度由 Profile 决定） |
| **协议类虚函数** | 不加。只有 `EcatEsc` / `EcatPhy` 契约层需要 |
| **中断上下文** | PDO 业务钩子跑在 PDI ISR；邮箱/SDO/AL 状态机在线程侧 |

**容量的一个已知限制**：`ObjectData` 的 `input_counter[64]` / `output_counter[64]` 是编译期定长（约 512 B）。传 `counter[8]` 的业务类型时，字典里那 512 B 照样占着，只是对外只暴露前 8 项。功能正确，RAM 不随业务缩小。

---

## 5. 并发约定

| 上下文 | 入口 | 能做什么 |
| --- | --- | --- |
| **PDI ISR** | `OnPdi()` → 完整 `HandleAlEvents()` | 读 AL 事件、AL Control、SM2/SM3 搬运、邮箱收发 |
| **SYNC0/1 ISR** | `OnSync0()` / `OnSync1()` | 锁存点提交输入、看门狗计数 |
| **1 ms 定时器** | `OnTick1ms()` | AL 超时递减、DC 看门狗、时间戳、SII 延迟落盘 |
| **线程** | `Poll()` | 邮箱服务、超时检查、错误检查、诊断 |

**`OnPdi()` 必须走完整 `HandleAlEvents()`**，不能按注释缩成只有 PDO——未确认的事件会持续触发中断，把线程饿死。

数据可见性：`outputs_` 在 PDI ISR 写、线程读；`inputs_` 线程写、ISR 读。都靠「32 位对齐的结构体读写本身不撕裂」保证，**不额外加锁**。

---

## 6. SII 介质双配置

板上用的是 Flash 模拟 EEPROM 还是真实 I2C EEPROM，由 Kconfig 决定，代码里是显式双分支：

```cpp
#if defined(CONFIG_DUST_ECAT_SII_FLASH)
{ /* 后端每次搬一个字 */ }
#elif defined(CONFIG_DUST_ECAT_SII_EEPROM)
{ /* ESC 自己走 I2C 主控，应用层不参与 */ }
#endif
```

**不用 `#else` 隐式表达第二分支**——那样等于没用到第二个符号。

> ⚠️ `sdk_glue_user/drivers/ethercat/ecat_config.h` 里 `ESC_EEPROM_EMULATION` 的宏定义必须**先于** `#include "ecat_port_hw.h"`，否则该头会按「未定义」裁掉 EEPROM 引脚函数，导致 `init_esc_eeprom_pin` 找不到。

---

## 7. 上板记录

### 7.1 环境

| 项 | 值 |
| --- | --- |
| 板卡 | `uhpm6e00evk`（HPM6E00） |
| PHY | 双 `PhyJl1111`，Port0 / Port1 各一颗 |
| SII 介质 | 板载 I2C EEPROM（CAT24C128） |
| PDO | 双向各 64 UDINT / 256 B |

### 7.2 状态转换

```
[inf] ecat: al_control=0x12 req=0x02
[inf] ecat: al_state -> 0x02                    ← INIT → PREOP
[inf] ecat: al_control=0x04 req=0x04
[inf] ecat: al_state -> 0x04                    ← PREOP → SAFEOP
[inf] ecat: al_control=0x08 req=0x08
[inf] ecat: al_state -> 0x08 (first outputs)    ← 首帧到达，SAFEOP → OP
```

`(first outputs)` 这行证明走的是「`EnterOp` 返回 `kAlStatusInWork` → SM2 首帧 → `CompletePendingOp` 补完」这条路径，而不是靠别的路径碰巧进的 OP。

### 7.3 稳态

```
state=0x08 code=0x0000 output=1 event=0x0000
pdi=8158 sm2=8157 sm3=8157
last_sm2_age_ms=1 max_gap_ms=2
```

`pdi : sm2 : sm3` 同步增长（差 1 是最后一个 SM2 的输入尚未被主站读走），`max_gap_ms=2` 即最坏差 1 个 tick。

### 7.4 断链恢复

拔插网线：

```
[wrn] ecat local state: 0x08 -> 0x04 code=0x001b   ← SM 看门狗超时，退 SAFEOP
[err] ecat rx stopped: state=0x04 code=0x001b output=0 event=0x0000
[inf] ecat: al_control=0x11 req=0x01 ×3            ← 主站拉回 INIT
[inf] ecat: al_state -> 0x02 → 0x04 → 0x08         ← 重建
[inf] ecat rx resumed: state=0x08 sm2=112330
```

`0x001B` 是 `kSmWatchdog`（不是 `0x001A` 同步错误），说明判定走的是过程看门狗分支。首次断链冻结现场（`fault saved: id=1`），第二次只累加 `additional_faults_`，不覆盖。

### 7.5 尚未验证

- AL 转换的失败分支（`EnterSafeOp` 区域冲突、`StartSm` 失败等）
- SDO 分段上传/下载、CA 读 0x1600/0x1A00、错误 toggle
- 邮箱满 / Repeat 重发路径
- SM 非法配置
- FreeRun / SM / DC0 / DC1 四种同步类型的分别验证
- 长时间运行

---

## 8. 排查方法

### 8.1 故障现场

诊断**只观察不参与协议**：不读会确认 AL Control 的寄存器，不改事件处理路径。稳态不刷屏。

| 时机 | 输出 |
| --- | --- |
| 首次故障 | `ecat fault saved: id=N; run 'ecat fault'` |
| 停收 | `ecat rx stopped:` / `ecat rx stalled:`（每次停收一次，之后静默） |
| 恢复 | `ecat rx resumed:` 一条 |
| 主动拉取 | shell `ecat fault` |

`ecat fault` 会分批输出：9 行现场（状态码、硬件寄存器、计数、看门狗 divider/time/timeout） + 最多 8 条 × 5 行历史（每 250 ms 采样，约两秒）。分批是为了不挤满日志记录池。

`ecat fault clear` 清除现场，允许保存下一次。

### 8.2 常见现象

| 现象 | 判断 |
| --- | --- |
| 卡在 INIT 不进 PREOP | EEPROM CRC/LOAD 错误，或 SM0/SM1 配置非法 |
| 进不了 SAFEOP | PDO 布局与 SM2/3 长度不符，或区域重叠 |
| 进不了 OP | RxPDO 存在但没收到首帧输出 |
| OP 后掉回 SAFEOP，`code=0x001B` | SM 看门狗超时——主站停发或网线断了 |
| OP 后掉回 SAFEOP，`code=0x001A` | DC 同步错误 |
| `thread show` 里 `task last=0.000 us` 且 `run` 不涨 | Task 体内有阻塞调用，违反单拍约定 |

---

## 9. 许可

协议语义来源于 Beckhoff SSC 5.13。来源登记与文件对应表见 [core/LICENSE-BECKHOFF.md](core/LICENSE-BECKHOFF.md)。

本实现是重写而非复制，但协议常量、对象索引、状态码取值、线格式均遵循 SSC 定义。
