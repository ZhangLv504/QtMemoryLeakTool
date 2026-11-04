# =============================================================================
# 文件：demo/phase1/CMakeLists.txt
# 项目：QtMemoryLeakTool / QtMemTracker
# 阶段：阶段一 —— 底层内存泄漏拦截与日志落盘
#
# 目标：可执行 Phase1Demo（阶段一验证入口）
#   - main.cpp：制造可预期的泄漏场景，并对 Stats 与日志行数自校验；
#   - plain_new.cpp：刻意不含 MemHook.h 的独立 TU，验证全局钩子与 file=<unknown>；
#   - 链接 memtrack_core 获得全局 new/delete 钩子与日志落盘能力；
#   - MSVC 加 /FC，保证 __FILE__ 展开为绝对路径，日志可直接定位分配位置。
# =============================================================================

# main.cpp 使用 std::thread 制造多线程泄漏场景，直接声明本目标的线程依赖。
find_package(Threads REQUIRED)

add_executable(Phase1Demo
    main.cpp
    plain_new.cpp
)

target_link_libraries(Phase1Demo PRIVATE memtrack_core Threads::Threads)

# 阶段一 demo 不含 Q_OBJECT/ui/qrc，不使用 moc/uic/rcc 生成。
set_target_properties(Phase1Demo PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# /FC 让 __FILE__ 展开为绝对路径。
if(MSVC)
    target_compile_options(Phase1Demo PRIVATE /FC)
endif()
