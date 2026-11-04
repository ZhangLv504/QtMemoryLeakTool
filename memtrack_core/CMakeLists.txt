# =============================================================================
# 文件：memtrack_core/CMakeLists.txt
# 项目：QtMemoryLeakTool / QtMemTracker
# 阶段：阶段一 —— 底层内存泄漏拦截与日志落盘
#
# 目标：静态库 memtrack_core
#   - 内容：全局 operator new/delete 重载、静态哈希表、日志落盘（MemTracker.cpp）；
#   - 对外头文件：本目录下的 MemTrackerConfig.h / MemTracker.h / MemHook.h，
#     以 PUBLIC include 目录传播给链接本库的目标；
#   - 依赖：不含 Qt；POSIX 下 currentThreadId 使用 pthread_self，需要 Threads。
#
# 为何静态库也能替换全局 operator new：
#   可执行目标里对 operator new/delete 的引用，会由本库提供的强符号解析，
#   链接器优先使用它们而不是 CRT 的默认实现。
# =============================================================================

# 表容量通过缓存变量注入编译期宏，必须是 2 的幂（MemTrackerConfig.h 有 static_assert）。
# 调大后 BSS 占用随之增加（每槽约 48 字节）；表满时跟踪器只计数溢出、不会扩容。
# 在库自己的目录声明，保持核心库的编译期配置随库自治。
set(MEMTRACK_TABLE_CAPACITY 16384 CACHE STRING "Power-of-two slot count of the memtrack hash table")

# pthread 是 currentThreadId 的实现依赖，统一由本库声明。
find_package(Threads REQUIRED)

add_library(memtrack_core STATIC
    MemTracker.cpp
)

target_include_directories(memtrack_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(memtrack_core PUBLIC cxx_std_17)
target_compile_definitions(memtrack_core PRIVATE
    MEMTRACK_TABLE_CAPACITY=${MEMTRACK_TABLE_CAPACITY}
)
# PUBLIC：链接本库的可执行目标同时获得 pthread（std::thread 场景也会用到）。
target_link_libraries(memtrack_core PUBLIC Threads::Threads)

# 核心库不含 Q_OBJECT/ui/qrc；即使上层目录开启了 AUTOMOC 也保持与 Qt 解耦。
set_target_properties(memtrack_core PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
