# =============================================================================
# 文件：CMakeLists.txt（项目根）
# 项目：QtMemoryLeakTool / QtMemTracker
# 阶段：阶段一 —— 底层内存泄漏拦截与日志落盘（阶段二将追加 Qt 界面目标）
#
# 职责：只做项目级设置与目标编排。
#   - 项目级设置：C++ 标准、全局编译选项、Qt5 查找（阶段二使用）；
#   - 目标编排：用 add_subdirectory 把构建细节下放到各子目录，
#     源文件、include 目录、依赖与目标级选项均由子目录自己的 CMakeLists.txt 负责。
#
# 目录结构：
#   memtrack_core/CMakeLists.txt  静态库 memtrack_core（全局 new/delete 钩子 + 日志落盘）
#   demo/phase1/CMakeLists.txt    可执行 Phase1Demo（阶段一验证入口）
#
# 注意：根文件不设置 CMAKE_AUTOMOC/AUTOUIC/AUTORCC。
#   核心库与阶段一 demo 均不含 Q_OBJECT/ui/qrc，保持与 Qt 完全解耦；
#   阶段二新增 Qt 目标时，在 demo/phase2/CMakeLists.txt 内只对自身目标开启。
# =============================================================================
cmake_minimum_required(VERSION 3.17)

project(QtMemoryLeakTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------- 项目级编译选项 ----------
# /utf-8 保证源码与执行字符集为 UTF-8；/Zc:__cplusplus 让 __cplusplus 反映真实标准。
if(MSVC)
    add_compile_options(/utf-8)
    add_compile_options(/Zc:__cplusplus)
else()
    add_compile_options(-finput-charset=UTF-8)
endif()

# ---------- Qt5 支持（阶段二使用；阶段一目标不链接 Qt） ----------
if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt5_DIR)
    set(CMAKE_PREFIX_PATH "E:/Software/Qt/5.15.2/msvc2019_64" CACHE PATH "Qt5 prefix")
endif()

find_package(Qt5 5.15.2 EXACT REQUIRED COMPONENTS Core Gui Widgets)

# ---------- 目标编排 ----------
# 阶段二新增 Qt 界面时，只需在这里追加一行 add_subdirectory(demo/phase2)。
add_subdirectory(memtrack_core)
add_subdirectory(demo/phase1)
