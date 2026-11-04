# =============================================================================
# 文件：demo/phase2/CMakeLists.txt
# 项目：QtMemoryLeakTool / QtMemTracker
# 阶段：阶段二 —— Qt 对象树监控界面
#
# 目标：可执行 Phase2App
#   - main.cpp：应用入口、测试窗口搭建、--self-check 自校验；
#   - ObjectTreeModel.h/.cpp：只读对象树模型（Q_OBJECT，需要 moc）；
#   - 链接 memtrack_core 获得阶段一全局 new/delete 钩子与日志落盘；
#   - 仅对本目标开启 AUTOMOC，核心库与阶段一 demo 仍与 Qt 元对象系统解耦；
#   - MSVC 加 /FC，保证 __FILE__ 为绝对路径。
# =============================================================================

# Qt 依赖在子目录内声明，保持目标自治（根文件只做编排）。
find_package(Qt5 5.15.2 EXACT REQUIRED COMPONENTS Widgets)

add_executable(Phase2App
    main.cpp
    ObjectTreeModel.cpp
    ObjectTreeModel.h
    LeakLogReader.cpp
    LeakLogReader.h
)

target_link_libraries(Phase2App PRIVATE
    memtrack_core
    Qt5::Widgets
)
target_compile_features(Phase2App PRIVATE cxx_std_17)

# 本目标含 Q_OBJECT，需要 AUTOMOC；没有 .ui/.qrc，UIC/RCC 保持关闭。
set_target_properties(Phase2App PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC OFF
)

# /FC 让 __FILE__ 展开为绝对路径（与阶段一 demo 一致）。
if(MSVC)
    target_compile_options(Phase2App PRIVATE /FC)
endif()
