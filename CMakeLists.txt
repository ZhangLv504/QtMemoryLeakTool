# =============================================================================
# 文件：CMakeLists.txt
# 项目：QtMemoryLeakTool / QtMemTracker
# 阶段：阶段一 —— 底层内存泄漏拦截与日志落盘（阶段二将追加 Qt 界面目标）
# 目标结构：
#   memtrack_core  静态库，只含全局内存钩子与日志实现，不依赖 Qt；
#   Phase1Demo     阶段一验证可执行，链接 memtrack_core。
# 说明：
#   - 底层钩子与 Qt 界面严格分离，因此两个阶段一目标都关闭 AUTOMOC/AUTOUIC/
#     AUTORCC，核心库不会引入任何 Qt 依赖；
#   - 阶段二新增 Phase2App 时再链接 Qt5::Widgets 与 memtrack_core；
#   - 静态库同样能替换全局 operator new：main.obj 引用的符号由本库提供，
#     链接器优先于 CRT 默认实现解析。
# =============================================================================
cmake_minimum_required(VERSION 3.17)

project(QtMemoryLeakTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------- Qt5 支持（阶段二使用；阶段一目标不链接 Qt） ----------
if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt5_DIR)
    set(CMAKE_PREFIX_PATH "E:/Software/Qt/5.15.2/msvc2019_64" CACHE PATH "Qt5 prefix")
endif()

find_package(Qt5 5.15.2 EXACT REQUIRED COMPONENTS Core Gui Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ---------- MSVC 编码 ----------
# /utf-8 保证源码与执行字符集为 UTF-8；/Zc:__cplusplus 让 __cplusplus 反映真实标准。
if(MSVC)
    add_compile_options(/utf-8)
    add_compile_options(/Zc:__cplusplus)
else()
    add_compile_options(-finput-charset=UTF-8)
endif()

find_package(Threads REQUIRED)

# ---------- 阶段一：底层内存跟踪静态库 ----------
# 表容量通过缓存变量注入编译期宏，必须是 2 的幂；调大后 BSS 占用随之增加
# （每槽约 48 字节），表满时跟踪器只计数溢出、不会扩容。
set(MEMTRACK_TABLE_CAPACITY 16384 CACHE STRING "Power-of-two slot count of the memtrack hash table")

add_library(memtrack_core STATIC
    src/core/MemTracker.cpp
)
target_include_directories(memtrack_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
)
target_compile_features(memtrack_core PUBLIC cxx_std_17)
target_compile_definitions(memtrack_core PRIVATE
    MEMTRACK_TABLE_CAPACITY=${MEMTRACK_TABLE_CAPACITY}
)
# 核心库不包含 Q_OBJECT/ui/qrc，关闭自动生成以保持与 Qt 完全解耦。
set_target_properties(memtrack_core PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# ---------- 阶段一验证可执行 ----------
# plain_new.cpp 刻意不含 MemHook.h，用于验证全局钩子与 file=<unknown> 路径。
add_executable(Phase1Demo
    demo/phase1/main.cpp
    demo/phase1/plain_new.cpp
)
target_link_libraries(Phase1Demo PRIVATE memtrack_core Threads::Threads)
set_target_properties(Phase1Demo PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# /FC 让 __FILE__ 展开为绝对路径，日志中的分配位置可直接定位。
if(MSVC)
    target_compile_options(Phase1Demo PRIVATE /FC)
endif()
