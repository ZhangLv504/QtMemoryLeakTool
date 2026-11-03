// =============================================================================
// 文件：src/core/MemTrackerConfig.h
// 项目：QtMemoryLeakTool / QtMemTracker
// 阶段：阶段一 —— 底层内存泄漏拦截与日志落盘
// 用途：跟踪器的编译期配置。所有宏均可由 CMake 覆盖，本文件只提供默认值，
//       不引入任何运行时依赖，供核心库与验证程序共同包含。
// =============================================================================
#pragma once

// 静态哈希表槽数，必须是 2 的幂（开放寻址用位与代替取模）。
// 每个槽默认 48 字节，16384 槽约占 768 KB BSS；该值同时决定可同时跟踪的
// 存活块上限，表满时只增加溢出计数、不会扩容（扩容需要堆分配，会触发被
// 重载的 operator new）。
#ifndef MEMTRACK_TABLE_CAPACITY
#define MEMTRACK_TABLE_CAPACITY 16384u
#endif

// 日志路径静态缓冲长度。路径随 setLogPath 复制进静态数组，超过该长度的
// 路径会被拒绝，保证落盘路径本身不产生堆分配。
#ifndef MEMTRACK_PATH_MAX
#define MEMTRACK_PATH_MAX 512u
#endif

// 1：start() 注册 atexit，进程正常退出时自动写一份 dump_reason=atexit 的日志；
// 0：不注册自动落盘，只能调用 memtrack::dumpLeaks() 手动导出。
#ifndef MEMTRACK_ENABLE_ATEXIT
#define MEMTRACK_ENABLE_ATEXIT 1
#endif

// 1：编译 C++17 aligned new/delete 重载族（_aligned_malloc/_aligned_free）；
// 0：不提供对齐分配钩子，对齐分配既不记录也不参与泄漏统计。
#ifndef MEMTRACK_ENABLE_ALIGNED
#define MEMTRACK_ENABLE_ALIGNED 1
#endif

// 自动日志根目录，相对进程当前工作目录。start() 为空路径时会创建
// <root>/<phase>/<运行时间戳>_pid<pid>/ 三级目录；显式 setLogPath/start(path)
// 不参与该布局，因此不受此宏影响。
#ifndef MEMTRACK_LOG_ROOT
#define MEMTRACK_LOG_ROOT "logs"
#endif

// 阶段目录名（如 phase1/phase2）的静态缓冲长度，含结尾 '\0'。
// 阶段名由 setPhaseName() 在首次自动 start 之前指定。
#ifndef MEMTRACK_PHASE_MAX
#define MEMTRACK_PHASE_MAX 64u
#endif

// 编译期守住契约：容量为 2 的幂是索引算法前提；路径缓冲至少容纳默认文件名
// 加常见目录前缀，避免默认配置下就不可用；阶段名至少要能放下 "phase1"。
static_assert(MEMTRACK_TABLE_CAPACITY >= 2u,
              "MEMTRACK_TABLE_CAPACITY must be at least 2");
static_assert((MEMTRACK_TABLE_CAPACITY & (MEMTRACK_TABLE_CAPACITY - 1u)) == 0u,
              "MEMTRACK_TABLE_CAPACITY must be a power of two");
static_assert(MEMTRACK_PATH_MAX >= 16u,
              "MEMTRACK_PATH_MAX must be at least 16");
static_assert(MEMTRACK_PHASE_MAX >= 8u,
              "MEMTRACK_PHASE_MAX must be at least 8");
