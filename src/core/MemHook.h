// =============================================================================
// 文件：src/core/MemHook.h
// 项目：QtMemoryLeakTool / QtMemTracker
// 阶段：阶段一 —— 底层内存泄漏拦截与日志落盘
// 用途：把源码中的 new 表达式改写成带 __FILE__/__LINE__ 的 placement 调用，
//       使跟踪器能记录分配位置。仅做宏与重载声明，不包含实现。
// =============================================================================
#pragma once

// 宏替换边界（重要）：
// 1. 本头文件必须是所在翻译单元的最后一个 include。宏定义后，任何后续头文件
//    中的 new 记号（包括 STL/Qt 容器实现里的 placement new）都会被改写，
//    导致编译失败或语义改变。
// 2. 宏会重写 TU 内出现的所有 new 记号，包括显式写法里的 operator new，
//    因此带宏的 TU 内不能直接写 ::operator new；需要裸分配时放到不包含本
//    文件的翻译单元中提供包装函数（见 demo/phase1/plain_new.cpp）。
// 3. 宏只覆盖 new，不覆盖 delete；释放路径由全局 operator delete 统一处理。
// 4. 放置 new（new (ptr) T）与 new (std::nothrow) T 在带宏的 TU 中无法书写，
//    需要时先定义 MEMTRACK_DISABLE_NEW_MACRO 再包含本文件；此类裸分配仍会
//    被全局钩子捕获，只是位置显示为 file=<unknown> line=0。
#include <cstddef>
#include <new>

// 宏展开后 new(__FILE__, __LINE__) 会调用下面的 placement 重载。按照 C++ 的
// 重载解析规则，这些声明必须在调用点可见，否则编译器只能看到 <new> 里的
// 标准重载并报“没有重载函数可以转换所有参数类型”。实现位于 MemTracker.cpp。
void* operator new(std::size_t size, const char* file, int line);
void* operator new[](std::size_t size, const char* file, int line);

// 构造函数抛异常时，编译器用与 placement new 配对的 placement delete 回收
// 已分配内存；二者必须成对声明，否则内存既不会记录也不会释放。
void operator delete(void* p, const char* file, int line) noexcept;
void operator delete[](void* p, const char* file, int line) noexcept;

#if !defined(MEMTRACK_DISABLE_NEW_MACRO)
#  define new new(__FILE__, __LINE__)
#endif
