# 项目进度与架构契约

> 状态：阶段一、阶段二均已完成并通过各自 demo 自校验；当前无未启动阶段。

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
6. **代码注释规范**：所有交付的 C++ 头文件、源文件、构建脚本及验证用 `main.cpp` 必须包含充分注释。至少包括：
    - 文件头注释：文件用途、所属阶段、项目名称；
    - 类与函数注释：职责、参数含义、返回值、调用约束；
    - 关键数据结构与算法说明：哈希表/静态池的设计意图、生命周期；
    - 线程安全说明：锁的粒度、加锁顺序、防死锁策略；
    - 防递归说明：哪些内部路径禁止再次触发全局 `new/delete`；
    - 宏替换说明：`#define new new(__FILE__, __LINE__)` 的边界与使用限制；
    - 资源释放责任：谁分配、谁释放、何时释放；
    - 验证用例注释：每个泄漏场景的制造意图与预期日志结果。
    禁止无注释堆砌代码，也禁止仅机械重复代码语义的无效注释；注释统一使用中文，与项目文档保持一致。

## 阶段一架构设计与接口契约

> 状态：已实现并通过 `Phase1Demo` 验证（见文末完成记录）。实现与本节契约一致；偏离点已回写第 6 节。

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
memtrack_core/MemTrackerConfig.h   编译期常量
memtrack_core/MemTracker.h         公开 API（不含 new 宏）
memtrack_core/MemTracker.cpp       全局钩子 + 静态表 + 落盘
memtrack_core/MemHook.h            new 宏开关（必须最后 include）
demo/phase1/main.cpp               阶段一验证入口（include MemHook.h）
demo/phase1/plain_new.cpp          不包含宏的 TU，验证全局钩子与 file=<unknown>
```

CMake 结构（子目录自治，根文件只做目标编排）：
- 根 `CMakeLists.txt`：仅 `project()`、C++17、全局编译选项（`/utf-8`、`/Zc:__cplusplus`）、`find_package(Qt5)`，以及 `add_subdirectory(memtrack_core)`、`add_subdirectory(demo/phase1)` 两条编排语句。
- `memtrack_core/CMakeLists.txt`：定义静态库 `memtrack_core`（`MemTracker.cpp`、`PUBLIC` include 本目录、`cxx_std_17`、`MEMTRACK_TABLE_CAPACITY` cache 变量注入、`PUBLIC` 链接 `Threads::Threads`、关闭 AUTOMOC/AUTOUIC/AUTORCC）。
- `demo/phase1/CMakeLists.txt`：定义 `Phase1Demo`（`main.cpp` + `plain_new.cpp`），链接 `memtrack_core` 与 `Threads::Threads`，MSVC 加 `/FC`（保证 `__FILE__` 为绝对路径）。
- 移除 `src/main.cpp`（当前是 `std::vector` 练习代码，不属于任何阶段功能，内容不迁移）。
- 原 `QtMemoryLeakTool` 单目标撤除；阶段二新增 `demo/phase2/CMakeLists.txt`，根文件只追加一行 `add_subdirectory(demo/phase2)`。
- 阶段一 demo 不链接 Qt，但项目仍 `find_package(Qt5)`（阶段二依赖）。
- 根文件不设置 `CMAKE_AUTOMOC/AUTOUIC/AUTORCC`；阶段二 Qt 目标在自身子目录内开启，不波及核心库与阶段一 demo。

### 3. 组件职责

| 组件 | 职责 | 约束 |
|---|---|---|
| `MemTrackerConfig.h` | 表容量、路径长度、atexit 开关、aligned 开关 | 纯宏，无 include 依赖 |
| `MemTracker.h` | `memtrack::start/stop/setLogPath/setPhaseName/stats/dumpLeaks`、`Stats` | 仅 `<cstddef>`；**不定义 `new` 宏**；全部 `noexcept` |
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
- **落盘路径零分配**：路径拼接与目录创建只用静态缓冲 + `snprintf` + `_mkdir`/`mkdir` + `std::fopen/fprintf/fclose`（CRT 走 `malloc`，不经过 `operator new`）；目录创建只在 `start()` 配置阶段发生，不在分配钩子内。

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
#include <cstddef>
#include <new>
// 宏展开后的 placement 重载必须在调用点可见，否则重载解析失败
void* operator new(std::size_t size, const char* file, int line);
void* operator new[](std::size_t size, const char* file, int line);
void operator delete(void* p, const char* file, int line) noexcept;
void operator delete[](void* p, const char* file, int line) noexcept;
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

bool  start(const char* logPath = nullptr) noexcept;  // 空则启用自动日志布局
void  stop() noexcept;                                // 仅暂停记录，不丢记录、不落盘
bool  setLogPath(const char* path) noexcept;          // 显式路径，优先于自动布局
bool  setPhaseName(const char* phase) noexcept;       // 自动布局的阶段目录名
Stats stats() noexcept;                               // 原子快照
int   dumpLeaks(const char* path = nullptr) noexcept; // 返回泄漏块数；失败返回 -1
}
```

| API | 语义 | 说明 |
|---|---|---|
| `start` | 开启记录并注册 atexit 自动落盘 | 幂等；`logPath` 为空时按 8.1 自动创建运行目录，目录创建失败返回 `false` 且不开启 |
| `stop` | 暂停记录，保留已有记录，**取消**自动落盘 | 期间分配/释放走透传，不记录；之后可再次 `start` 重新开启 |
| `setLogPath` | 设置显式日志路径 | 设置后进入显式路径模式，自动布局不再生效；超过 `MEMTRACK_PATH_MAX` 返回 `false` |
| `setPhaseName` | 设置自动布局的阶段目录名 | 仅首次自动 `start` 之前有效；非空、单段、不含 `/` `\` `:`、不是 `.`/`..`；目录已初始化后返回 `false` |
| `stats` | 读取计数快照 | 不持锁导出，不加锁读原子计数 |
| `dumpLeaks` | 写快照日志，**不清空**记录 | 默认写入本次运行目录并自动编号（`manual-N.log`）；显式路径仍覆盖写（`wb`）；不改变跟踪状态 |

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
6. 后续阶段只允许在头部块内追加新 `key=value`，或在该行末尾追加新键；不得重命名/删除/调整已有键，不得修改首末行；文件位置与命名只能按 8.1 新增，不得改义。

### 8.1 日志存储布局（v2）

> 目录与命名规则自阶段一起生效；日志内容格式仍是上面的 v1。

```
logs/
  phase1/
    20261103-221530_pid20788/
      run.log        ← atexit 自动落盘（dump_reason=atexit）
      manual-1.log   ← 第 1 次 dumpLeaks()（dump_reason=manual）
      manual-2.log   ← 第 2 次 dumpLeaks()
  phase2/
    20261103-223001_pid31044/
      run.log
```

1. 根目录为 `MEMTRACK_LOG_ROOT`（默认 `logs`），相对进程当前工作目录；由 `start()` 自动创建，创建失败时 `start()` 返回 `false` 且不开启记录。
2. 第二级为阶段目录，由 `setPhaseName()` 指定，默认 `default`；阶段一入口设为 `phase1`，阶段二设为 `phase2`。
3. 第三级为一次进程运行的目录：`<yyyyMMdd-HHmmss>_pid<pid>`，时间取首次自动 `start()` 的时刻；`stop()` 后再次 `start()` 复用同一目录，一次运行的所有日志集中存放。
4. 自动落盘写 `run.log`；手动 `dumpLeaks()` 依次写 `manual-1.log`、`manual-2.log`…，同一次运行内不互相覆盖。
5. 显式路径优先：`setLogPath()` 或 `start(path)` 一旦设置，默认落盘全部写到该路径，不创建自动目录（兼容阶段一早期行为）。
6. 未调用过 `start()` 且无显式路径时，`dumpLeaks()` 回退到旧默认 `memtrack_leaks.log`（相对当前工作目录）。
7. 自动布局只改变文件位置与命名，日志内容仍是 v1；解析方按阶段目录区分阶段，按运行目录时间戳/`generated_at` 区分运行。

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
| `MEMTRACK_LOG_ROOT` | `"logs"` | 自动日志根目录，相对当前工作目录 |
| `MEMTRACK_PHASE_MAX` | 64 | 阶段目录名静态缓冲长度（含结尾 `\0`） |

### 11. 阶段一验收场景（`Phase1Demo` 内自校验）

| 场景 | 预期 |
|---|---|
| 1000 次 `new/delete` 循环（main 内） | 不产生泄漏记录；Tombstone 被复用 |
| 主线程故意泄漏 `new int[10]`、`new char[24]` | 2 条记录，40 + 24 字节 |
| 2 个工作线程各泄漏 1 块（`int[4]`、`long long[2]`） | 2 条记录，各 16 字节，`tid` 不同 |
| 无宏 TU（`plain_new.cpp`）泄漏 `new uint64_t[4]` | 1 条记录，32 字节，`file=<unknown> line=0` |
| `stop()` 后在无宏 TU 分配、`start()` 后释放 | `unknownFrees == 1` |
| 手动 `dumpLeaks()` | `logs/phase1/<yyyyMMdd-HHmmss>_pid<pid>/manual-1.log` 存在，5 条块，头部 `dump_reason=manual` |
| 程序正常退出 | 同一运行目录下 `run.log` 存在，`dump_reason=atexit`，`live_blocks=5`、`live_bytes=128`、`table_overflows=0` |
| 目录布局 | `logs/phase1/<运行目录>/` 三级结构成立，阶段目录名来自 `setPhaseName("phase1")` |
| 退出码 | `Stats` 与预期值全部一致返回 0，否则返回非 0 |

### 12. 已知限制与阶段二入口

**已知限制**
- 宏会破坏带宏 TU 内的放置 new / `new (std::nothrow)` / 显式 `operator new` 写法；需用 `MEMTRACK_DISABLE_NEW_MACRO` 或置于无宏 TU。
- 无调用栈，仅文件+行号；未通过宏的分配只有 `file=<unknown> line=0`。
- 表容量固定，溢出记录丢弃但计数；容量需按目标程序分配峰值调整。
- 重复释放的第二次会记为 `unknownFrees`，不能可靠识别双重释放。
- `start` 之前、`stop` 期间的块不受跟踪；`atexit` 早于早于 `main` 构造的静态对象析构（其析构中释放的块仍会出现在报告里，属预期内偏差）。
- Windows 下路径经 `fopen` 按当前 ANSI 代码页处理，非 ASCII 路径可能打不开。
- 自动日志布局相对当前工作目录：从不同目录启动会在各自 CWD 下生成 `logs/`；CWD 不可写时 `start()` 返回 `false`。
- 显式路径模式不参与自动编号，多次 `dumpLeaks()` 会覆盖同一文件。
- 跨 DLL 不生效（各模块 CRT 独立）。

**阶段二入口**
- 新增 `Phase2App`（Qt5 Widgets）链接 `memtrack_core`，不改动钩子与日志格式，仅在头部追加键。
- Qt 自身大量走 `operator new`，进场前需评估 `MEMTRACK_TABLE_CAPACITY`；可在 UI 初始化完成后 `start()`，避免无关噪声。
- 对象树面板只读 `QObject::children()`，不参与内存跟踪数据结构。

## 阶段一完成记录（2026-11-03）

### 已完成事项

- 新增核心源码：`memtrack_core/MemTrackerConfig.h`、`MemTracker.h`、`MemTracker.cpp`、`MemHook.h`。
- 新增阶段一验证入口：`demo/phase1/main.cpp`、`demo/phase1/plain_new.cpp`。
- 构建改造：静态库 `memtrack_core`（关闭 AUTOMOC/AUTOUIC/AUTORCC，不依赖 Qt）+ 可执行 `Phase1Demo`；移除练习用 `src/main.cpp`。
- CMake 改为子目录自治：根 `CMakeLists.txt` 只保留项目级设置与 `add_subdirectory` 编排，核心库目录与目标同名（`memtrack_core/`，原 `src/core/` 及空的 `src/` 已移除），`memtrack_core/CMakeLists.txt` 与 `demo/phase1/CMakeLists.txt` 各自声明目标、依赖与选项；`MEMTRACK_TABLE_CAPACITY` 的 cache 变量随核心库目录声明。
- 全局钩子按契约覆盖：普通/数组/sized/nothrow/宏 placement/C++17 aligned 的分配与释放。
- 日志 v1 落盘：manual 与 atexit 两条路径均验证。
- 日志自动布局（8.1）：`setPhaseName("phase1")` 后由 `start()` 创建 `logs/phase1/<yyyyMMdd-HHmmss>_pid<pid>/`，手动写 `manual-N.log`、退出写 `run.log`；显式路径模式保持优先。
- 按契约第 6 条补齐全部交付文件的中文注释：文件头、函数职责与约束、静态表/哈希设计、线程安全与防递归策略、宏边界、资源释放责任、验证场景与预期日志。

### 实现要点（与架构契约一致）

- 静态表 `BlockInfo[MEMTRACK_TABLE_CAPACITY]`（默认 16384，开放寻址 + 墓碑复用），`splitmix64` 哈希；跟踪器内部零堆分配，只用 CRT/OS API。
- 单把 `std::atomic_flag` 自旋锁保护整表；`thread_local` 重入深度守卫；`dump` 逐槽“加锁复制→解锁写盘”，不长时间持锁。
- 分配失败抛 `std::bad_alloc`，nothrow 形式返回 `nullptr`；未命中表的释放计入 `unknownFrees`，按记录中的 `align` 或调用形式选择 `free` / `_aligned_free`。
- `stop()` 仅暂停记录并取消自动落盘，释放仍会清理既有记录；`start()` 可重新开启。
- 自动日志目录在 `start()` 配置阶段用静态缓冲 + `snprintf` + `_mkdir` 创建，不进入分配钩子；路径状态由 `g_lock` 保护，目录创建与路径格式化均在锁外，不违反“临界区无 IO/无分配”。
- 契约补充（已同步到第 6 节）：`MemHook.h` 必须声明四个 placement 分配/释放重载，否则 MSVC 在调用点重载解析失败（构建首轮即暴露，已修正）。

### 验证结果

- 构建：`cmake -S . -B build -G "Visual Studio 17 2022" -A x64` + `cmake --build build --config RelWithDebInfo`，成功生成 `Phase1Demo.exe`。
- CMake 拆分为子目录自治后重新配置构建通过，产物位置变为 `build/memtrack_core/RelWithDebInfo/memtrack_core.lib` 与 `build/demo/phase1/RelWithDebInfo/Phase1Demo.exe`，运行结果与拆分前一致。
- 运行：`live_blocks=5 live_bytes=128 unknown_frees=1 table_overflows=0 manual_rows=5`，输出 `PASS`，退出码 0。
- `logs/phase1/<yyyyMMdd-HHmmss>_pid<pid>/manual-1.log` 与同目录 `run.log`：`dump_reason` 分别为 `manual` / `atexit`，各 5 条块记录；两个工作线程 `tid` 不同；无宏 TU 的块为 `file=<unknown> line=0`。
- 连续两次运行生成两个独立的时间戳运行目录，`manual-N.log` 编号与 `run.log` 命名符合 8.1。
- 1000 次 `new/delete` 循环未产生误报；墓碑槽被复用。

### 已知限制

沿用第 12 节列表，阶段一未新增限制。

### 下一阶段入口

- 新增 `Phase2App`（Qt5 Widgets）并链接 `memtrack_core`，不改动钩子与日志 v1 格式，只在头部追加键；入口调用 `setPhaseName("phase2")`，日志落到 `logs/phase2/...`。
- Qt 初始化会产生大量非目标分配，建议 UI 构建完成后再 `start()`，并按分配峰值调整 `MEMTRACK_TABLE_CAPACITY`。

## 阶段二架构设计与接口契约

> 状态：架构已冻结，按本节实现；实现完成后在下文追加完成记录。阶段一已通过验证，本阶段不得破坏其钩子、日志格式与 `Phase1Demo`。

### 1. 范围与非目标

**范围内**
- 新增 Qt5 Widgets 可执行目标 `Phase2App`：`QTreeView` + 自定义 `QAbstractItemModel`，快照式显示目标 `QObject` 及其递归子对象。
- 提供一个测试窗口，内含带父子关系的 `QPushButton` / `QLabel` / `QWidget`，用于验证对象树拓扑。
- 链接 `memtrack_core`：阶段名设为 `phase2`，UI 构建完成后才开始记录，退出时自动落盘。
- 提供 `--self-check` 无界面自校验模式（offscreen 平台），供构建后验证对象树模型与日志集成。

**非目标（阶段二不做）**
- 实时增量刷新、对象编辑/删除、QML、跨线程对象树采集。
- 修改阶段一全局钩子、静态表或日志 v1 格式。
- 跟踪 Qt DLL 内部通过各自 CRT 完成的分配。
- 展示非 `QObject` 的内存泄漏明细（该部分仍由阶段一 `.log` 承担）。

### 2. 目录与构建目标结构

```
demo/phase2/CMakeLists.txt    Phase2App 目标声明
demo/phase2/main.cpp          应用入口、测试窗口搭建、自校验
demo/phase2/ObjectTreeModel.h 只读对象树模型声明（Q_OBJECT）
demo/phase2/ObjectTreeModel.cpp 快照构建与 QAbstractItemModel 实现
```

- 根 `CMakeLists.txt` 仅追加一行 `add_subdirectory(demo/phase2)`，Qt5 查找沿用根文件的 `find_package`。
- `demo/phase2/CMakeLists.txt` 自行声明目标、源文件、`Qt5::Widgets` 与 `memtrack_core` 依赖；仅对本目标开启 `AUTOMOC`（`AUTOUIC`/`AUTORCC` 关闭），不波及核心库与阶段一 demo。
- MSVC 下加 `/FC`，保持与阶段一一致的绝对路径语义。
- `Phase2App` 为控制台子系统可执行文件（便于自校验输出与退出码采集），运行时同时显示主窗口。

### 3. 组件职责

| 组件 | 职责 | 约束 |
|---|---|---|
| `ObjectTreeModel` | 持有某一时刻的对象树快照，按 4 列只读展示；`refresh()` 重建快照 | 只在 GUI 线程使用；不持有对象所有权；不修改对象树 |
| `main.cpp` | 创建 `QApplication`、测试窗口、模型与视图；处理 `--self-check`；集成 `memtrack` | 不包含 `MemHook.h`；UI 构建完成后再 `start()` |
| `Phase2App` | 阶段二可独立运行入口 | 启动默认显示窗口；`--self-check` 校验后自动退出 |

### 4. 数据结构

```cpp
struct ObjectNode {                 // 快照节点，值语义，仅 GUI 线程访问
    QPointer<QObject> object;       // 对应对象；对象已销毁时为空，仅用于诊断
    QString className;              // QObject::metaObject()->className() 快照
    QString objectName;             // QObject::objectName() 快照，空串显示 <unnamed>
    QString address;                // 对象地址，格式 0x%016llX
    int childCount;                 // children().size() 快照
    int row;                        // 在父节点 children 中的行号
    ObjectNode* parent;             // 父节点指针；根节点为 nullptr
    QVector<ObjectNode> children;   // 递归子节点
};
```

- 模型根节点 `m_rootNode` 即目标对象（如主窗口），作为唯一顶层行；`row = 0`，`parent = nullptr`。
- 快照构建完成后不再修改容器，`QModelIndex::internalPointer` 指向稳定的节点地址。
- 展示名称优先 `objectName`，为空时回退 `<unnamed>`；类型来自 `metaObject()`。
- 列定义固定为：`对象`、`类型`、`子对象数`、`地址`。

### 5. 线程模型

- `QObject::children()` 与模型重构全部在 GUI 线程执行；模型不提供锁，不跨线程共享。
- 刷新期间使用 `beginResetModel()` / `endResetModel()`，视图不持有跨快照的持久索引。
- 快照只缓存字符串与 `QPointer`，刷新后即使原对象被销毁，`data()` 也不会解引用悬垂指针。

### 6. 与阶段一跟踪器的集成

- 入口在首次 `start()` 前调用 `memtrack::setPhaseName("phase2")`，并在 UI 全部构建完成后再 `memtrack::start()`，避免 Qt 初始化噪声占满静态表。
- 日志自动落到 `logs/phase2/<yyyyMMdd-HHmmss>_pid<pid>/`，内容仍为 v1；本阶段不新增头部键。
- 阶段二的所有翻译单元均不包含 `MemHook.h`（避免宏侵入 Qt 头文件）；Qt 代码中的裸 `new` 由全局钩子照常记录，位置为 `file=<unknown> line=0`。
- `--self-check` 模式在退出前调用一次 `dumpLeaks()`，生成 `manual-1.log`；进程退出再由 atexit 写 `run.log`。

### 7. `ObjectTreeModel` 接口契约

```cpp
class ObjectTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit ObjectTreeModel(QObject* parent = nullptr);
    void setRootObject(QObject* root);   // 设置目标对象并立即 refresh
    QObject* rootObject() const;         // 返回 QPointer 提升后的指针，可能为 nullptr
    void refresh();                      // 重建快照；根为空时模型为空
    void setSearchText(const QString& text); // 按 objectName 子串高亮，空串清除
    QString searchText() const;          // 当前搜索文本

    // QAbstractItemModel 只读实现
    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
};
```

- 只读：`flags()` 不返回 `Qt::ItemIsEditable`；未实现 `setData`。
- `rowCount` 对有效父索引返回其 `children.size()`，对无效父索引返回 `1`（有根）或 `0`。
- `data()` 响应 `Qt::DisplayRole` 与 `Qt::BackgroundRole`；搜索命中时 BackgroundRole 返回淡蓝色（`QColor(204,229,255)`），整行 4 列生效；列越界或索引无效返回空 `QVariant`。
- `setSearchText()` 只递归发送 `BackgroundRole` 的 `dataChanged`，不重建快照，展开状态与选中项保持不变；匹配规则为 `objectName` 子串、不区分大小写。
- `setRootObject(nullptr)` 清空模型；不接管对象生命周期。

### 8. 测试窗口结构（对象名固定，供自校验）

```
mainWindow
└─ centralWidget
   └─ mainSplitter
      ├─ objectTreeView          （QTreeView）
      └─ testPanel
         ├─ searchEdit             （QLineEdit，按对象名实时高亮）
         ├─ testGroupBox
         │  ├─ infoLabel
         │  ├─ okButton
         │  └─ nestedPanel
         │     └─ nestedButton
         ├─ refreshButton
         └─ hintLabel
```

- `refreshButton` 点击后调用 `model.refresh()` 并 `view->expandAll()`。
- 控件通过布局建立父子关系，布局同时负责可视排列。

### 9. 启动流程与自校验

- 普通启动：构建窗口 → `setPhaseName("phase2")` → `start()` → `show()` → `app.exec()`。
- `--self-check`：若未设置 `QT_QPA_PLATFORM`，入口设置 `offscreen`；构建并显示窗口，在事件循环首轮用 `QTimer::singleShot(0, ...)` 执行校验：
  1. 根节点显示名为 `mainWindow`；
  2. 第 8 节全部对象名都出现在模型中；
  3. `nestedButton` 位于 `nestedPanel` 的子树内；
  4. 节点总数不少于 10；
  5. 调用 `dumpLeaks()` 并打印统计与 `PASS`/`FAIL`，以 0/1 退出；
  6. 校验对象树默认展开（根与嵌套节点均为展开状态）且水平/垂直滚动条策略为 `ScrollBarAlwaysOn`；
  7. 校验搜索：输入 `nested` 时 `nestedPanel` 的 `BackgroundRole` 有效、根节点无效；清空后高亮消失。
- 校验逻辑只通过 `QAbstractItemModel` 公共接口遍历模型，不访问模型私有成员。

### 10. 阶段二验收场景

| 场景 | 预期 |
|---|---|
| VS 2022 生成器构建 | 生成 `build/demo/phase2/RelWithDebInfo/Phase2App.exe` |
| `--self-check`（offscreen） | 输出 `PASS`，退出码 0，节点数与对象名校验全部通过 |
| 对象树根与拓扑 | 顶层为 `mainWindow`；`nestedButton` 的祖先链包含 `nestedPanel` |
| 刷新 | 点击 `refreshButton` 后模型重置并重新展开 |
| 默认展开与滚动条 | 启动后对象树全部展开；水平、垂直滚动条常驻 |
| 列宽可调 | 四列均为 `QHeaderView::Interactive`，可拖动列边界改变宽度（初始 220/240/120/260） |
| 搜索高亮 | 输入对象名子串后，命中行 4 列显示淡蓝背景；清空输入恢复默认底色 |
| 手动落盘 | `logs/phase2/<运行目录>/manual-1.log`，`dump_reason=manual` |
| 退出落盘 | 同一运行目录 `run.log`，`dump_reason=atexit` |
| 无损增量 | `Phase1Demo` 仍可构建运行并输出 `PASS`；日志 v1 格式未变 |

### 11. 已知限制

- 快照式显示：对象树在构建后新增/删除的子对象需手动刷新，不做增量同步。
- 仅展示 `QObject` 派生对象；普通堆分配仍只能通过阶段一 `.log` 排查。
- Qt DLL 内部经各自 CRT 的分配不受钩子影响，表中数据只覆盖应用侧。
- `--self-check` 使用 offscreen 平台，只验证模型拓扑与集成，不验证实际渲染效果。
- UI 构建完成后再 `start()`，因此 Qt 初始化期的分配不在报告中。

## 阶段二完成记录（2026-11-04）

### 已完成事项

- 新增阶段二目标源码：`demo/phase2/ObjectTreeModel.h`、`ObjectTreeModel.cpp`、`main.cpp`、`CMakeLists.txt`。
- 根 `CMakeLists.txt` 追加 `add_subdirectory(demo/phase2)`，其余编排未改动。
- 实现只读对象树模型：快照递归 `QObject::children()`，固定 4 列（对象/类型/子对象数/地址），使用 `QPointer` 与缓存字符串，刷新后不解引用悬垂指针。
- 实现测试窗口：`QTreeView` + 固定对象名的两级父子控件（`testGroupBox → nestedPanel → nestedButton`），刷新按钮重建快照并展开。
- 集成阶段一跟踪器：UI 构建完成后 `setPhaseName("phase2")` + `start()`；日志自动落到 `logs/phase2/<运行目录>/`，v1 格式未改动。
- 提供 `--self-check`：offscreen 平台启动，事件循环首轮通过模型公共接口校验根节点、12 个必需对象名、嵌套拓扑与节点数，输出统计与 `PASS`/`FAIL`，退出码 0/1。

### 实现要点（与架构契约一致）

- `ObjectTreeModel::refresh()` 使用 `beginResetModel()`/`endResetModel()`；`buildNode()` 递归构建值语义快照，`fixParents()` 在容器停止增长后回填 `parent`/`row`，保证 `internalPointer` 稳定。
- `rowCount`：无效父索引返回 1（有根）或 0，有效父索引返回快照子节点数；`flags()` 只返回 `ItemIsEnabled | ItemIsSelectable`，不实现 `setData`。
- 模型不持有 `QObject` 所有权；目标对象销毁后 `hasRoot()` 为 false，模型为空。
- 阶段二全部翻译单元不包含 `MemHook.h`；Qt 头文件不受 `new` 宏影响，应用侧裸分配仍由全局钩子记录为 `file=<unknown>`。
- `AUTOMOC` 仅在 `Phase2App` 目标开启；`memtrack_core` 与 `Phase1Demo` 仍保持 Qt 元对象解耦。

### 验证结果

- 构建：`cmake -S . -B build -G "Visual Studio 17 2022" -A x64` + `cmake --build build --config RelWithDebInfo`，生成 `build/demo/phase2/RelWithDebInfo/Phase2App.exe`。
- 自校验：`QT_QPA_PLATFORM=offscreen`（由程序在未设置时补齐）运行 `Phase2App.exe --self-check`，输出
  `nodes=42 max_depth=7 live_blocks=1 live_bytes=32 unknown_frees=0 table_overflows=0 manual_rows=1`、`PASS`，退出码 0。
- 日志：`logs/phase2/20261104-172725_pid31892/manual-1.log`（`dump_reason=manual`，1 条 32 字节块，`file=<unknown>`）与同目录 `run.log`（`dump_reason=atexit`，首末行与键序符合 v1）。
- 回归：`Phase1Demo` 重新运行仍输出 `live_blocks=5 live_bytes=128 unknown_frees=1 table_overflows=0 manual_rows=5`、`PASS`，退出码 0。
- 说明：`run.log` 中 `unknown_frees=17`、`live_blocks=0` 属预期——`start()` 之前 Qt 初始化分配的块在退出析构时被释放，表中无记录，计为未知释放。

### 已知限制

沿用阶段二架构契约第 11 节，未新增限制；补充验证口径：本次仅以 offscreen 自校验验证模型拓扑与日志集成，实际窗口渲染与鼠标交互未做人工目视验证。

### 下一阶段入口

两个既定阶段均已完成。后续若扩展，入口保持为：不改动阶段一钩子与日志 v1 格式；新增界面能力放在 `demo/phase2` 或新子目录，通过模型公共接口读取对象树；任何包含 Qt 头文件的翻译单元仍禁止包含 `MemHook.h`。

## 阶段二界面补充记录（2026-11-04）

### 变更事项

- 默认展开：`view->expandAll()` 移到 `window.show()` 之后执行（QTreeView 需可见后完成完整布局），启动即全展开；刷新按钮仍会重建快照并重新展开。
- 滚动条：对象树视图的水平、垂直滚动条策略均设为 `Qt::ScrollBarAlwaysOn`。
- 列宽可调：四列尺寸模式统一为 `QHeaderView::Interactive`（初始宽度 220/240/120/260），用户可拖动列边界，不再被 `Stretch`/`ResizeToContents` 锁定。
- 搜索：右侧面板新增 `searchEdit`（QLineEdit，实时触发）；模型新增 `setSearchText()` / `searchText()`，按 `objectName` 子串、不区分大小写匹配，命中行 4 列使用淡蓝色背景 `QColor(204,229,255)`，空文本清除高亮。
- 搜索只递归发送 `Qt::BackgroundRole` 的 `dataChanged`，不重建快照，展开状态与选中项不丢失。
- `--self-check` 扩展校验默认展开、双滚动条常驻、四列可拖动、搜索命中/清除。

### 验证结果

- `Phase2App.exe --self-check`（offscreen）：`nodes=46 max_depth=7 expand=ok scrollbars=ok columns=ok search=ok live_blocks=1 live_bytes=40 unknown_frees=0 table_overflows=0 manual_rows=1`、`PASS`，退出码 0。
- 日志仍为 v1：`logs/phase2/<运行目录>/manual-1.log`（`dump_reason=manual`）与 `run.log`（`dump_reason=atexit`）。
- 回归：`Phase1Demo` 输出 `live_blocks=5 live_bytes=128 unknown_frees=1 table_overflows=0 manual_rows=5`、`PASS`，退出码 0；核心库与钩子未改动。

## 阶段二扩展：泄漏日志查看（架构与接口契约）

> 状态：架构已冻结，按本节实现后回写完成记录。本扩展沿用阶段一日志 v1 与阶段二 `Phase2App`，不新增可执行目标，不改动钩子。

### 1. 范围与非目标

**范围内**
- 在 `Phase2App` 右侧下方增加日志面板：日志文件选择、刷新列表、加载选中、立即导出并加载。
- 解析 `MEMTRACK_LOG_ROOT/phase2/**/*.log` 的 v1 内容，展示头部统计与块表格（序号/地址/大小/线程/对齐/行号/文件）。
- 启动时自动加载最近一份日志；`--self-check` 校验解析结果与 `dumpLeaks()` 返回值一致。

**非目标**
- 不修改日志格式、跟踪器 API 或全局钩子。
- 不支持 v1 之外的格式、压缩日志或远程日志。
- 不自动监听文件变化；列表与内容由按钮/导出动作刷新。

### 2. 组件与接口

```
demo/phase2/LeakLogReader.h    日志发现与 v1 解析接口
demo/phase2/LeakLogReader.cpp  实现
demo/phase2/main.cpp           日志面板 UI 与自校验扩展
```

```cpp
struct LeakBlock {
    quint64 seq;
    QString address;   // 0x%016llX 原样字符串
    quint64 size;
    quint32 tid;
    quint32 align;
    int     line;
    QString file;      // 行尾原始值，允许空格；未知为 <unknown>
};

struct LeakLogData {
    QHash<QString, QString> header;  // 头部 key=value
    QVector<LeakBlock> blocks;
};

namespace leaklog {
// 递归扫描 rootDir 下所有 *.log，按修改时间倒序返回绝对路径。
QStringList listLogFiles(const QString& rootDir);
// 解析 v1 日志；首行必须为 "# qtmemtrack leak report v1"，遇到 "# end" 结束。
// 成功返回 true；失败返回 false，并可通过 error 获取原因。
bool readLog(const QString& path, LeakLogData& out, QString* error = nullptr);
}
```

- 块行解析使用固定正则：`^block seq=(\d+) addr=(0x[0-9A-Fa-f]+) size=(\d+) tid=(\d+) align=(\d+) line=(-?\d+) file=(.*)$`。
- 其他含 `=` 的行视为头部键；空行与无法识别的行忽略；缺失首行或 `# end` 判为不完整日志。
- 列表根目录由 `MEMTRACK_LOG_ROOT` 宏拼接阶段名 `phase2` 得到，与跟踪器自动布局保持一致。

### 3. 界面结构

```
centralWidget
└─ layoutSplitter（垂直）
   ├─ mainSplitter（现有：对象树 + 测试面板）
   └─ logGroup
      ├─ logSelector / reloadLogsButton / loadLogButton / dumpLogButton
      ├─ logSummary（文件路径与头部统计）
      └─ logTable（7 列：序号/地址/大小/线程/对齐/行号/文件）
```

- `dumpLogButton` 调用 `memtrack::dumpLeaks()` 后刷新列表并加载最新日志。
- 日志表格只读，使用只读选择模式；文件列自动拉伸，其余列可拖动。
- 日志读取发生在 GUI 线程，不参与分配钩子的临界区。

### 4. 自校验扩展

- `--self-check` 在调用 `dumpLeaks()` 后刷新日志列表，解析最新日志并要求：
  1. `dump_reason=manual`；
  2. 解析出的块数等于 `dumpLeaks()` 返回值；
  3. `logTable` 行数等于块数。
- 输出新增 `log=ok|fail`，与既有 expand/scrollbars/columns/search 检查一起决定退出码。

### 5. 已知限制

- 仅解析自动布局下的 `logs/phase2`；显式 `setLogPath` 生成的文件不在列表中。
- 列表按文件修改时间排序，同一时间戳下的顺序不保证稳定。
- 日志较大时一次性读入内存并填充表格，未做分页或流式加载。

### 阶段二扩展完成记录（2026-11-04）

#### 已完成事项

- 新增 `demo/phase2/LeakLogReader.h` / `LeakLogReader.cpp`：递归发现 `logs/phase2/**/*.log`，按修改时间倒序；按 v1 正则解析头部与块行，缺失报告头或 `# end` 判为失败。
- `Phase2App` 布局改为垂直分割：上方保留对象树与测试面板，下方新增日志面板（`logGroup`）。
- 日志面板包含：日志下拉框、刷新列表、加载选中、立即导出并加载、头部统计摘要、7 列只读块表格；启动时自动加载最近一份日志。
- `--self-check` 新增日志检查：`dumpLeaks()` 后用解析器读取最新日志，要求 `dump_reason=manual`、解析块数与返回值一致、表格行数一致。
- CMake 目标源文件追加 `LeakLogReader.cpp/.h`；未改动核心库、钩子与日志格式。

#### 验证结果

- 构建：VS 2022 生成器配置并 `RelWithDebInfo` 构建成功，生成 `build/demo/phase2/RelWithDebInfo/Phase2App.exe`。
- 自校验：`nodes=103 max_depth=9 expand=ok scrollbars=ok columns=ok search=ok log=ok live_blocks=1 live_bytes=48 unknown_frees=0 table_overflows=0 manual_rows=1`、`PASS`，退出码 0。
- 节点数由 46 增至 103 属预期：日志面板的 `QGroupBox`、下拉框、按钮、表格及其内部子对象都进入了 QObject 对象树。
- `logs/phase2/<运行目录>/manual-1.log` 被正确解析：`dump_reason=manual`，1 条块与表格行数一致；`run.log` 仍由 atexit 生成。
- 回归：`Phase1Demo` 输出 `live_blocks=5 live_bytes=128 unknown_frees=1 table_overflows=0 manual_rows=5`、`PASS`，退出码 0。

#### 已知限制

沿用扩展契约第 5 节；补充：本次运行的 `run.log` 在进程退出时才写入，界面只能在下一次启动或手动导出后看到它。
