/**
 * @file plain_new.cpp
 * @brief 阶段一验证辅助 TU：底层内存泄漏拦截与日志落盘。
 * @details 本翻译单元刻意不包含 MemHook.h，用来证明即使代码中没有 new 宏，
 *          全局 operator new/delete 仍然生效；同时提供“跟踪暂停期间分配”的
 *          构造/释放函数，供 main.cpp 制造 unknownFrees 场景。
 * @warning 不得 include MemHook.h，否则宏会改写本文件内的 new，失去验证意义。
 */
#include <cstdint>

/**
 * @brief 场景四：未经宏的普通分配。
 * @details 全局钩子会以 file=<unknown> line=0 记录该块；调用方不得释放它，
 *          预期在日志中留下 1 条 size=32 的泄漏记录。
 */
void leakPlainNew()
{
    auto* p = new std::uint64_t[4];
    (void)p;
}

/**
 * @brief 场景五（上半）：在 memtrack::stop() 期间分配。
 * @details 此时 g_enabled=false，钩子只透传不登记记录。返回的指针由调用方
 *          在 start() 之后转交 destroyUntrackedBlock 释放，以触发
 *          unknownFrees 计数。
 * @return 未登记的堆块指针。
 */
void* makeUntrackedBlock()
{
    return new int(7);
}

/**
 * @brief 场景五（下半）：在 start() 之后释放未登记的块。
 * @param[in] p 由 makeUntrackedBlock 返回的指针。
 * @note 预期：表中查不到地址，unknownFrees 加一，内存仍被正确归还给 malloc。
 */
void destroyUntrackedBlock(void* p)
{
    delete static_cast<int*>(p);
}
