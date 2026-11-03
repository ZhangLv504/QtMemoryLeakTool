// =============================================================================
// 文件：demo/phase1/main.cpp
// 项目：QtMemoryLeakTool / QtMemTracker
// 阶段：阶段一验证入口 —— 底层内存泄漏拦截与日志落盘
// 用途：按 PROJECT_PROGRESS.md 第 11 节的验收场景制造泄漏，校验 Stats 与
//       日志文件，正常时输出 PASS 并以退出码 0 结束。
//
// 预期产出（详见各场景注释）：
// - 手动日志 memtrack_manual.log：5 条块记录，dump_reason=manual；
// - 退出日志 memtrack_leaks.log：5 条块记录，dump_reason=atexit；
// - 统计值：live_blocks=5、live_bytes=128、unknown_frees=1、table_overflows=0。
//
// 本文件使用 printf 而不是 iostream：iostream 的静态初始化会在跟踪器启动前
// 产生堆分配，干扰计数；stdio 的缓冲区由 CRT malloc 分配，不经过 operator new。
// =============================================================================
#include <cstdio>
#include <cstdint>
#include <thread>

#include "MemTracker.h"
#include "MemHook.h" // 必须是最后一个 include：宏会影响之后所有 new 记号

// 以下三个函数定义在不包含 MemHook.h 的翻译单元中，用于验证：
// 1. 未携带位置信息的分配仍被全局钩子记录（file=<unknown>）；
// 2. 跟踪关闭期间分配的块，在重新开启后释放会被计入 unknownFrees。
void leakPlainNew();
void* makeUntrackedBlock();
void destroyUntrackedBlock(void* p);

namespace {

// 场景一：1000 次“分配-释放”。
// 意图：验证表项的插入/移除与墓碑复用，且往复分配不会产生假泄漏。
// 预期日志：0 条记录；live_blocks 不因该循环增长。
void churn()
{
    for (int i = 0; i < 1000; ++i)
    {
        int* p = new int[4];
        delete[] p;
    }
}

// 场景三：工作线程故意泄漏一块 int[4]。
// 意图：验证多线程并发写入静态表的正确性，并确认日志带不同 tid。
// 预期日志：每调用一次产生 1 条记录，size=16，tid 为该工作线程 ID。
void leakInWorker()
{
    int* p = new int[4];
    (void)p;
}

} // namespace

int main()
{
    // 使用默认日志路径 memtrack_leaks.log；start 同时布防 atexit 自动落盘。
    if (!memtrack::start())
    {
        std::printf("memtrack::start failed\n");
        return 2;
    }

    churn();

    // 场景二：主线程故意泄漏两块。
    // 预期日志：int[10] 对应 size=40，char[24] 对应 size=24，位置为主线程文件行号。
    int* leakedInts = new int[10];
    char* leakedChars = new char[24];
    (void)leakedInts;
    (void)leakedChars;

    // 场景三：两个工作线程各泄漏一块 int[4]。
    // 预期日志：新增 2 条 size=16 的记录，tid 与主线程不同且两者互不相同。
    std::thread first(leakInWorker);
    std::thread second(leakInWorker);
    first.join();
    second.join();

    // 场景四：无宏 TU 中的普通 new。
    // 预期日志：1 条 size=32、file=<unknown> line=0 的记录，证明全局钩子
    // 覆盖了未经过 new 宏的分配。
    leakPlainNew();

    // 场景五：未知释放。
    // 停止记录后分配（不登记），重新开启后释放（表中查不到）。
    // 预期日志：unknown_frees=1；该块本身不产生泄漏记录。
    memtrack::stop();
    void* untracked = makeUntrackedBlock();
    memtrack::start();
    destroyUntrackedBlock(untracked);

    // 场景六：手动导出。预期返回 5（上述 5 个泄漏块），
    // memtrack_manual.log 的 dump_reason=manual，且不影响跟踪状态。
    const int manualRows = memtrack::dumpLeaks("memtrack_manual.log");

    // 场景七：统计自校验。各字段与验收表一致才算通过；
    // 失败时退出码非 0，便于 CI/人工快速发现回归。
    const memtrack::Stats s = memtrack::stats();

    std::printf("live_blocks=%llu live_bytes=%llu unknown_frees=%llu "
                "table_overflows=%llu manual_rows=%d\n",
                static_cast<unsigned long long>(s.liveBlocks),
                static_cast<unsigned long long>(s.liveBytes),
                static_cast<unsigned long long>(s.unknownFrees),
                static_cast<unsigned long long>(s.tableOverflows),
                manualRows);

    const bool ok = s.liveBlocks == 5u
                 && s.liveBytes == 128u
                 && s.unknownFrees == 1u
                 && s.tableOverflows == 0u
                 && manualRows == 5;

    std::printf(ok ? "PASS\n" : "FAIL\n");

    // main 返回后 atexit 处理器再写一份 memtrack_leaks.log（dump_reason=atexit），
    // 内容应与手动日志的 5 条块记录一致。
    return ok ? 0 : 1;
}
