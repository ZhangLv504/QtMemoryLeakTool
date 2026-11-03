# 项目进度与架构契约

> 状态：阶段一架构设计已冻结，代码待实现。阶段二未启动。

## 核心需求与阶段划分

### 阶段一：底层内存泄漏拦截与日志落盘 (Core)
- **目标**：重载全局 `operator new` 和 `operator delete`。
- **机制**：通过宏 `#define new new(__FILE__, __LINE__)` 捕获分配源文件与行号。
- **要求**：设计一个安全的内部哈希表或静态池，用于保存 `<指针, 内存信息>` 的映射。该数据结构在分配和释放时不能触发对全局 `new` 的递归调用。
- **输出**：程序退出或特定触发时，将未释放的内存块（地址、大小、文件名、行号）写入本地 `.log` 文件。

### 阶段二：Qt 对象树监控界面 (UI)
- **目标**：实现一个基于 Qt Widgets 的界面，用于实时或快照式显示 `QObject` 及其子对象的树形拓扑结构。
- **机制**：使用 `QTreeView` 和自定义 `QAbstractItemModel`，递归解析目标对象的 `QObject::children()`。
- **要求**：提供一个简单的测试窗口，包含若干带有父子关系的 `QPushButton`、`QLabel` 等控件，用以验证对象树的正确显示。

## 当前唯一目标
**直接生成阶段一（底层内存泄漏拦截与日志落盘）的完整源码**，包括内存跟踪器的头文件、源文件以及如何安全替换全局 `new` 的宏定义头文件，并给出一个简单的 `main.cpp` 验证文件输出。

## 阶段交付契约

> 本契约对所有阶段生效，优先级高于“一次性生成”。

1. **阶段化推进**：严格按阶段顺序开发，禁止跨阶段一次性交付全部功能。
2. **每阶段产出记录**：每个阶段完成后，必须在本文件追加该阶段的：
   - 已完成事项清单（做了什么）
   - 关键架构设计说明（类职责、数据结构、线程模型、防递归/死锁策略等）
   - 已知限制与下一阶段入口
3. **每阶段可运行**：每个阶段必须交付一个能独立编译、链接并运行的 `main` 入口（或独立可执行目标），确保用户先看到该阶段的部分功能，再进入下一阶段。
4. **架构先行**：每阶段开写前，先在本文件补充该阶段的架构设计与接口契约，代码实现必须与契约一致。
5. **无损增量**：后续阶段的代码不得破坏前一阶段已通过验证的功能与日志格式。

## 阶段一架构设计与接口契约

> 状态：设计冻结，待实现。实现必须与下表一致；如需偏离，先改本节再写代码。

### 1. 范围与非目标

**范围内**
- 重载全局 `operator new` / `operator new[]` / `operator delete` / `operator delete[]`（含 sized、nothrow、C++17 aligned 形式）。
- 通过 `MemHook.h` 的 `new` 宏捕获 `__FILE__` / `__LINE__`。
- 静态定长哈希表记录存活块，退出或手动触发时写 `.log`。
- 线程安全、防递归、无堆分配的跟踪器内部实现。

**非目标（阶段一不做）**
- 任何 Qt 界面/对象树代码（阶段二）。
- `malloc/realloc/free`、`mmap`、第三方分配器拦截。
- 调用栈回溯、泄漏去重、GUI 展示、增量/实时上报。
- 跨 DLL 边界分配（Qt DLL 内部走各自 CRT，不受本钩子影响）。

### 2. 目录与构建目标结构

```
src/core/MemTrackerConfig.h   编译期常量
src/core/MemTracker.h         公开 API（不含 new 宏）
src/core/MemTracker.cpp       全局钩子 + 静态表 + 落盘
src/core/MemHook.h            new 宏开关（必须最后 include）
demo/phase1/main.cpp          阶段一验证入口（include MemHook.h）
demo/phase1/plain_new.cpp     不包含宏的 TU，验证全局钩子与 file=<unknown>
```

CMake 改造（替换现有"GLOB 单目标"结构）：
- 新增静态库 `memtrack_core`：`MemTracker.cpp`，`PUBLIC` include `src/core`，`cxx_std_17`。
- 新增可执行 `Phase1Demo`：`demo/phase1/*.cpp` + `memtrack_core`，MSVC 加 `/FC`（保证 `__FILE__` 为绝对路径）。
- 移除 `src/main.cpp`（当前是 `std::vector` 练习代码，不属于任何阶段功能，内容不迁移）。
- 原 `QtMemoryLeakTool` 单目标撤除；阶段二再新增 `Phase2App` 链接 `Qt5::Widgets` + `memtrack_core`。
- 阶段一 demo 不链接 Qt，但项目仍 `find_package(Qt5)`（阶段二依赖）。

### 3. 组件职责

| 组件 | 职责 | 约束 |
|---|---|---|
| `MemTrackerConfig.h` | 表容量、路径长度、atexit 开关、aligned 开关 | 纯宏，无 include 依赖 |
| `MemTracker.h` | `memtrack::start/stop/setLogPath/stats/dumpLeaks`、`Stats` | 仅 `<cstddef>`；**不定义 `new` 宏**；全部 `noexcept` |
| `MemTracker.cpp` | 全局 new/delete 重载、静态表读写、日志落盘、atexit 注册 | 不 include `MemHook.h`；只用 CRT/OS API，不用 STL 容器 |
| `MemHook.h` | `#define new new(__FILE__, __LINE__)` | 先 `#include <new>`；提供 `MEMTRACK_DISABLE_NEW_MACRO` 关闭开关 |
| `demo/phase1/main.cpp` | 验证入口，`MemHook.h` 放在所有 include 之后 | 自校验 `Stats`，失败返回非 0 |
| `demo/phase1/plain_new.cpp` | 无宏 TU：制造 unknown 位置泄漏、提供未跟踪块构造/释放函数 | 不 include `MemHook.h` |

### 4. 数据结构

```cpp
struct BlockInfo {              // POD，x64 下 48 字节
    const void* address;        // 块起始地址
    std::size_t size;           // 请求大小（0 按 0 记录）
    const char* file;           // __FILE__ 字面量指针；未知为 nullptr
    std::uint64_t seq;          // 分配序号，锁内自增，全局单调
    int line;                   // 未知为 0
    std::uint32_t tid;          // OS 线程 ID
    std::uint32_t align;        // 0 = 普通分配；否则为 aligned 对齐值
    std::uint8_t  state;        // Empty / Used / Tombstone
};
```

- 表：`static BlockInfo g_table[MEMTRACK_TABLE_CAPACITY];`（BSS，容量默认 16384 且必须为 2 的幂，x64 约 768 KB）。
- 冲突：开放寻址线性探测；删除留 Tombstone，插入优先复用 Tombstone；表满时 `tableOverflows++` 并放弃记录（分配本身仍成功）。
- 哈希：`splitmix64(reinterpret_cast<std::uintptr_t>(p))`。
- 计数：`liveBlocks`、`liveBytes`、`totalAllocations`、`unknownFrees`、`tableOverflows`（锁内维护，读取时经原子快照）。
- 不使用 `std::unordered_map`/`std::vector`/`std::string`，避免钩子内递归分配。

### 5. 线程模型与防递归/死锁

- **线程模型**：钩子可被任意线程调用；单把 `std::atomic_flag` 自旋锁保护整张表。临界区内只做探测/写入，无系统调用、无 IO、无内存分配，持锁时间 O(1)。
- **防递归**：`thread_local` 重入深度计数，`depth > 0` 时直接透传（正常分配/释放但不记录）。表为静态 POD、后备分配用 `malloc`，正常情况下不会递归；该守卫用于异常机制、CRT 内部等病态路径。
- **防死锁**：临界区不嵌套、不在持锁时做 IO；`dumpLeaks` 逐槽"加锁复制一条 → 解锁格式化写出"，全程不长时间持锁，也不使用第二把锁。
- **初始化顺序**：跟踪器状态全为静态 POD/原子量，无构造函数，避免静态初始化期分配与顺序问题。
- **落盘路径零分配**：只用 `snprintf` + `std::fopen/fprintf/fclose`（CRT 走 `malloc`，不经过 `operator new`）。

### 6. 全局钩子与宏契约

**必须提供的重载形式**

```cpp
// 普通
void* operator new(std::size_t);
void* operator new[](std::size_t);
void  operator delete(void*) noexcept;
void  operator delete[](void*) noexcept;
void  operator delete(void*, std::size_t) noexcept;
void  operator delete[](void*, std::size_t) noexcept;
// nothrow
void* operator new(std::size_t, const std::nothrow_t&) noexcept;
void* operator new[](std::size_t, const std::nothrow_t&) noexcept;
void  operator delete(void*, const std::nothrow_t&) noexcept;
void  operator delete[](void*, const std::nothrow_t&) noexcept;
// 宏捕获位置（放置形式）
void* operator new(std::size_t, const char*, int);
void* operator new[](std::size_t, const char*, int);
void  operator delete(void*, const char*, int) noexcept;   // 构造抛异常时配对调用
void  operator delete[](void*, const char*, int) noexcept;
// C++17 aligned
void* operator new(std::size_t, std::align_val_t);
void* operator new[](std::size_t, std::align_val_t);
void* operator new(std::size_t, std::align_val_t, const char*, int);
void* operator new[](std::size_t, std::align_val_t, const char*, int);
void  operator delete(void*, std::align_val_t) noexcept;
void  operator delete[](void*, std::align_val_t) noexcept;
void  operator delete(void*, std::size_t, std::align_val_t) noexcept;
void  operator delete[](void*, std::size_t, std::align_val_t) noexcept;
void  operator delete(void*, const char*, int, std::align_val_t) noexcept;  // 若编译器需要
```

- 后备分配：Windows 普通块 `std::malloc/std::free`，对齐块 `_aligned_malloc/_aligned_free`（POSIX 走 `posix_memalign`）。释放时必须依据 `BlockInfo::align` 选择对应释放函数，禁止混用。
- 未命中表的释放：`unknownFrees++` 后仍按调用形式释放（普通 `free` / 对齐 `_aligned_free`），不崩溃。
- 分配失败：`operator new` 抛 `std::bad_alloc`，nothrow 形式返回 `nullptr`；记录只在分配成功后写入。

**宏规则**

```cpp
// MemHook.h
#pragma once
#include <new>
#if !defined(MEMTRACK_DISABLE_NEW_MACRO)
#  define new new(__FILE__, __LINE__)
#endif
```

1. `MemHook.h` 必须是所在 TU 的**最后一个 include**；之后的头文件不得包含 `<new>`/STL/Qt 容器实现。
2. 需要放置语法 `new (ptr) T` 或 `new (std::nothrow) T` 的 TU 先定义 `MEMTRACK_DISABLE_NEW_MACRO` 再 include；此类分配仍被全局钩子记录，位置显示为 `file=<unknown> line=0`。
3. `MemTracker.cpp` 与 `plain_new.cpp` 不包含 `MemHook.h`。
4. 宏同样会展开 `operator new` 写法中的 `new` 记号，因此带宏的 TU 内禁止直接写 `::operator new`；需要裸分配时放到无宏 TU 提供包装函数。
5. `__FILE__` 以字面量指针直接存入表，不拷贝（字面量生命周期贯穿进程）。
6. MSVC 由 `/FC` 保证绝对路径；日志中路径按原样输出。

### 7. 公开 API 契约

```cpp
namespace memtrack {

struct Stats {
    std::uint64_t liveBlocks;
    std::uint64_t liveBytes;
    std::uint64_t totalAllocations;
    std::uint64_t unknownFrees;
    std::uint64_t tableOverflows;
    bool          enabled;
};

bool  start(const char* logPath = nullptr) noexcept;  // 默认 memtrack_leaks.log
void  stop() noexcept;                                // 仅暂停记录，不丢记录、不落盘
bool  setLogPath(const char* path) noexcept;          // 复制进静态 char[512]
Stats stats() noexcept;                               // 原子快照
int   dumpLeaks(const char* path = nullptr) noexcept; // 返回泄漏块数；失败返回 -1
}
```

| API | 语义 | 说明 |
|---|---|---|
| `start` | 开启记录并注册 atexit 自动落盘 | 幂等；`logPath` 为空用默认路径；路径超长返回 `false` 且不开启 |
| `stop` | 暂停记录，保留已有记录，**取消**自动落盘 | 期间分配/释放走透传，不记录；之后可再次 `start` 重新开启 |
| `setLogPath` | 设置默认落盘路径 | 任意时刻可调用；超过 `MEMTRACK_PATH_MAX` 返回 `false` |
| `stats` | 读取计数快照 | 不持锁导出，不加锁读原子计数 |
| `dumpLeaks` | 写快照日志，**不清空**记录 | 同路径重复写会覆盖（`wb`）；不改变跟踪状态 |

所有 API `noexcept`、无动态分配，可在 `start` 之前或 `stop` 之后调用。

### 8. 日志格式契约（v1，只增不改）

```
# qtmemtrack leak report v1
generated_at=2026-11-03T21:40:12
process_id=14532
dump_reason=atexit
table_capacity=16384
live_blocks=5
live_bytes=128
unknown_frees=1
table_overflows=0
block seq=1 addr=0x000001F2A3B4C5D0 size=40 tid=14532 align=0 line=57 file=D:/AiProjs/TwoProjs/CustomProjs/QtMemoryLeakTool/demo/phase1/main.cpp
block seq=2 addr=0x000001F2A3B4C5E0 size=24 tid=14532 align=0 line=58 file=D:/AiProjs/TwoProjs/CustomProjs/QtMemoryLeakTool/demo/phase1/main.cpp
# end
```

稳定规则：
1. 首行固定 `# qtmemtrack leak report v1`，末行固定 `# end`。
2. 头部为 `key=value`，顺序固定；`dump_reason` 取值 `atexit` 或 `manual`。
3. 每条块一行，`block` 开头；键顺序固定为 `seq addr size tid align line file`，`file` 是行尾原始值（允许空格），未知时为 `<unknown>`。
4. 地址格式固定 `0x%016llX`。
5. 行序不保证按 `seq` 排序（解析方按 `seq` 自行排序）；`seq` 全局单调递增。
6. 后续阶段只允许在头部块内追加新 `key=value`，或在该行末尾追加新键；不得重命名/删除/调整已有键，不得修改首末行与文件名约定。

### 9. 关键流程（实现必须逐分支覆盖）

- **分配**：`enabled == false || reentry > 0` → 直接走后备分配并返回（不记录）。否则后备分配→成功则 `seq++` 后插入表；插入失败（表满）仅 `tableOverflows++`；分配失败抛 `bad_alloc` / 返回 `nullptr`。
- **释放**：守卫透传时直接释放。否则按地址查表：命中→取 `align` 决定释放函数、移除记录（置 Tombstone）、`liveBlocks--/liveBytes-=size`；未命中→`unknownFrees++` 后仍释放。
- **dump**：`fopen(path,"wb")` 失败返回 -1；写头部（含当前 `Stats` 与 `dump_reason`）；逐槽加锁复制一条 Used 记录→解锁→格式化写出；写 `# end`、`fflush`、`fclose`；返回写出条数。
- **atexit**：仅在"已 `start` 且未被 `stop` 取消"时执行一次自动 dump，`dump_reason=atexit`；异常/返回值忽略。

### 10. 编译期配置

| 宏 | 默认 | 含义 |
|---|---|---|
| `MEMTRACK_TABLE_CAPACITY` | 16384 | 表槽数，必须为 2 的幂；CMake cache 变量注入 |
| `MEMTRACK_PATH_MAX` | 512 | 日志路径静态缓冲长度 |
| `MEMTRACK_ENABLE_ATEXIT` | 1 | 是否支持自动落盘 |
| `MEMTRACK_ENABLE_ALIGNED` | 1 | 是否编译 aligned 重载族 |

### 11. 阶段一验收场景（`Phase1Demo` 内自校验）

| 场景 | 预期 |
|---|---|
| 1000 次 `new/delete` 循环（main 内） | 不产生泄漏记录；Tombstone 被复用 |
| 主线程故意泄漏 `new int[10]`、`new char[24]` | 2 条记录，40 + 24 字节 |
| 2 个工作线程各泄漏 1 块（`int[4]`、`long long[2]`） | 2 条记录，各 16 字节，`tid` 不同 |
| 无宏 TU（`plain_new.cpp`）泄漏 `new uint64_t[4]` | 1 条记录，32 字节，`file=<unknown> line=0` |
| `stop()` 后在无宏 TU 分配、`start()` 后释放 | `unknownFrees == 1` |
| 手动 `dumpLeaks("memtrack_manual.log")` | 文件存在，5 条块，头部 `dump_reason=manual` |
| 程序正常退出 | `memtrack_leaks.log` 存在，`dump_reason=atexit`，`live_blocks=5`、`live_bytes=128`、`table_overflows=0` |
| 退出码 | `Stats` 与预期值全部一致返回 0，否则返回非 0 |

### 12. 已知限制与阶段二入口

**已知限制**
- 宏会破坏带宏 TU 内的放置 new / `new (std::nothrow)` / 显式 `operator new` 写法；需用 `MEMTRACK_DISABLE_NEW_MACRO` 或置于无宏 TU。
- 无调用栈，仅文件+行号；未通过宏的分配只有 `file=<unknown> line=0`。
- 表容量固定，溢出记录丢弃但计数；容量需按目标程序分配峰值调整。
- 重复释放的第二次会记为 `unknownFrees`，不能可靠识别双重释放。
- `start` 之前、`stop` 期间的块不受跟踪；`atexit` 早于早于 `main` 构造的静态对象析构（其析构中释放的块仍会出现在报告里，属预期内偏差）。
- Windows 下路径经 `fopen` 按当前 ANSI 代码页处理，非 ASCII 路径可能打不开。
- 跨 DLL 不生效（各模块 CRT 独立）。

**阶段二入口**
- 新增 `Phase2App`（Qt5 Widgets）链接 `memtrack_core`，不改动钩子与日志格式，仅在头部追加键。
- Qt 自身大量走 `operator new`，进场前需评估 `MEMTRACK_TABLE_CAPACITY`；可在 UI 初始化完成后 `start()`，避免无关噪声。
- 对象树面板只读 `QObject::children()`，不参与内存跟踪数据结构。
