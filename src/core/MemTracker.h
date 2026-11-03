// =============================================================================
// 文件：src/core/MemTracker.h
// 项目：QtMemoryLeakTool / QtMemTracker
// 阶段：阶段一 —— 底层内存泄漏拦截与日志落盘
// 用途：内存跟踪器的公开 API。头文件只暴露控制与查询接口，不定义 new 宏、
//       不暴露内部哈希表；宏与 placement 重载声明位于 MemHook.h。
// 约束：本文件的声明全部 noexcept，实现内部不使用堆分配，可在 main 之前、
//       任意线程以及 stop() 之后安全调用。
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace memtrack {

// 跟踪器计数快照。各字段在同一时刻分别读取，并发分配下不保证彼此严格一致；
// 进程正常退出的 atexit 落盘发生在单线程阶段，数值是精确的。
struct Stats
{
    // 当前仍在表中、尚未释放的块数量。
    std::uint64_t liveBlocks;
    // 这些存活块的请求字节数之和（不包含分配器头部开销）。
    std::uint64_t liveBytes;
    // 自 start() 以来成功记录过的分配总次数（含已释放块）。
    std::uint64_t totalAllocations;
    // 释放时未在表中找到记录的次数：可能来自 stop() 期间分配、start() 前分配，
    // 也可能是重复释放。用于诊断，不视为泄漏。
    std::uint64_t unknownFrees;
    // 表满导致记录被丢弃的次数；大于 0 说明报告不完整，应提高表容量。
    std::uint64_t tableOverflows;
    // 当前是否正在记录分配（stop() 后为 false）。
    bool enabled;
};

// 开启跟踪并准备自动落盘。
// logPath：可选日志路径，nullptr 使用当前默认路径；非空时等价于先调用
//          setLogPath。路径为空或超长时直接返回 false 且不改变原有开关状态。
// 返回：true 表示已开启；false 仅由路径参数非法导致。
// 调用约束：幂等；首次调用注册 atexit 处理器，之后 stop()/start() 只切换
//           记录开关并重新布防自动落盘，不会重复注册。本函数不分配内存。
bool start(const char* logPath = nullptr) noexcept;

// 暂停分配记录，并取消本次运行的自动落盘；已记录的块保留，之后调用
// start() 可重新开启。stop() 期间发生的释放仍会清理已有记录，不会造成
// 假泄漏。本函数不分配内存。
void stop() noexcept;

// 设置默认日志路径（同时用于 atexit 自动落盘和 dumpLeaks 的默认目标）。
// path：非空 C 字符串；长度必须小于 MEMTRACK_PATH_MAX。
// 返回：true 表示已复制进静态缓冲；false 表示参数为空或超长，原路径不变。
// 线程安全：与落盘读取路径共用一把自旋锁，只做内存复制，不做 IO。
bool setLogPath(const char* path) noexcept;

// 读取计数快照。无锁读取原子计数，内部不分配内存，可在任意线程调用。
Stats stats() noexcept;

// 将当前存活块快照写入日志文件。
// path：可选目标路径，nullptr 使用 setLogPath/start 设置的默认路径；
//       空串或超长返回 -1。
// 返回：写出的块记录条数；文件无法打开（目录不存在、无权限等）返回 -1。
// 副作用：不修改跟踪状态、不清空记录；同一路径重复调用会覆盖旧文件。
// 调用约束：内部逐槽加锁复制、锁外写盘，可与分配并发；但多个线程同时以
//           同一路径落盘时输出会互相覆盖，调用方应自行串行化 dump 调用。
int dumpLeaks(const char* path = nullptr) noexcept;

} // namespace memtrack
