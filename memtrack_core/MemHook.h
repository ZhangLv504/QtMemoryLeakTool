/**
 * @file MemHook.h
 * @brief 把源码中的 new 表达式改写成带 __FILE__/__LINE__ 的 placement 调用。
 * @details 使跟踪器能记录分配位置。仅做宏与重载声明，不包含实现。
 * @warning 本头文件必须是所在翻译单元的最后一个 include。宏定义后，任何后续
 *          头文件中的 new 记号（包括 STL/Qt 容器实现里的 placement new）都会
 *          被改写，导致编译失败或语义改变。
 * @warning 宏会重写 TU 内出现的所有 new 记号，包括显式写法里的 operator new，
 *          因此带宏的 TU 内不能直接写 ::operator new；需要裸分配时放到不包含
 *          本文件的翻译单元中提供包装函数（见 demo/phase1/plain_new.cpp）。
 * @note 宏只覆盖 new，不覆盖 delete；释放路径由全局 operator delete 统一处理。
 * @note 放置 new（new (ptr) T）与 new (std::nothrow) T 在带宏的 TU 中无法书写，
 *       需要时先定义 MEMTRACK_DISABLE_NEW_MACRO 再包含本文件；此类裸分配仍会
 *       被全局钩子捕获，只是位置显示为 file=<unknown> line=0。
 */
#pragma once

#include <cstddef>
#include <new>

/**
 * @brief placement new 重载，供 new 宏注入分配位置。
 * @param[in] size 请求字节数。
 * @param[in] file 展开点所在文件名（__FILE__）。
 * @param[in] line 展开点所在行号（__LINE__）。
 * @return 已分配并登记的块地址。
 * @note 宏展开后 new(__FILE__, __LINE__) 会调用这个重载。按照 C++ 的重载解析
 *       规则，这些声明必须在调用点可见，否则编译器只能看到 <new> 里的标准
 *       重载并报“没有重载函数可以转换所有参数类型”。实现位于 MemTracker.cpp。
 */
void* operator new(std::size_t size, const char* file, int line);

/**
 * @brief placement new[] 重载，供 new 宏注入分配位置。
 * @param[in] size 请求字节数。
 * @param[in] file 展开点所在文件名（__FILE__）。
 * @param[in] line 展开点所在行号（__LINE__）。
 * @return 已分配并登记的块地址。
 */
void* operator new[](std::size_t size, const char* file, int line);

/**
 * @brief placement delete，与 placement new 成对。
 * @param[in] p 待释放指针。
 * @param[in] file 展开点所在文件名（__FILE__）。
 * @param[in] line 展开点所在行号（__LINE__）。
 * @note 构造函数抛异常时，编译器用与 placement new 配对的 placement delete
 *       回收已分配内存；二者必须成对声明，否则内存既不会记录也不会释放。
 */
void operator delete(void* p, const char* file, int line) noexcept;

/**
 * @brief placement delete[]，与 placement new[] 成对。
 * @param[in] p 待释放指针。
 * @param[in] file 展开点所在文件名（__FILE__）。
 * @param[in] line 展开点所在行号（__LINE__）。
 */
void operator delete[](void* p, const char* file, int line) noexcept;

#if !defined(MEMTRACK_DISABLE_NEW_MACRO)
#  define new new(__FILE__, __LINE__)
#endif
