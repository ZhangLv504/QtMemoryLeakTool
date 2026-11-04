/**
 * @file MemTracker.cpp
 * @brief 全局 operator new/delete 拦截与泄漏日志落盘的实现。
 * @details 替换全局 operator new/delete，把每次分配的
 *          <地址, 大小, 文件, 行号> 记入静态哈希表，并在手动调用或进程退出时
 *          写成日志。
 * @warning 跟踪器内部禁止任何堆分配：不使用 std::string/容器，也不调用
 *          operator new，否则会递归进入本文件的钩子。
 * @note 只有一把自旋锁 g_lock 保护整张表。临界区内不做 IO、不分配、不嵌套
 *       加锁，因此不存在锁顺序问题。
 * @note 落盘路径只使用 stdio（CRT 内部走 malloc），并逐槽加锁复制后锁外写出，
 *       既不会递归触发 new，也不会长时间阻塞分配线程。
 */
#include "MemTracker.h"
#include "MemTrackerConfig.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

//! 平台差异只涉及线程/进程 ID、对齐分配函数与目录创建，均封装在本文件内部，
//! 不影响公开头文件。windows.h 只出现在实现 TU，避免污染调用方。
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <direct.h>
#  include <malloc.h>
#  include <process.h>
#  include <windows.h>
#else
#  include <pthread.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace {

using memtrack::Stats;

constexpr std::size_t kCapacity = static_cast<std::size_t>(MEMTRACK_TABLE_CAPACITY);

/**
 * @brief 哈希表槽状态。
 * @details 删除不真正腾空槽位，而是留下墓碑：若直接置空，会截断同一探测链上
 *          后续插入的查找路径。
 */
enum SlotState : std::uint8_t
{
    kSlotEmpty = 0,     ///< 空槽，可作为插入终点。
    kSlotUsed = 1,      ///< 有效记录。
    kSlotTombstone = 2  ///< 已删除，可复用但不可作为查找终点。
};

/**
 * @brief 一条分配记录。
 * @details file 直接保存 __FILE__ 字面量指针，不复制字符串：字面量生命周期
 *          贯穿整个进程，复制反而需要在钩子内分配内存。结构保持 POD，使
 *          g_table 能做零初始化并放入 BSS。
 */
struct BlockInfo
{
    const void* address;   ///< 分配返回的原始指针，用作哈希键。
    std::size_t size;      ///< 调用方请求的字节数。
    const char* file;      ///< 分配位置；未经宏的分配为 nullptr。
    std::uint64_t seq;     ///< 分配序号，全局单调递增，日志按它排序。
    int line;              ///< 分配行号；未知为 0。
    std::uint32_t tid;     ///< 分配线程的 OS ID。
    std::uint32_t align;   ///< 0 表示普通分配，否则为 aligned new 的对齐值。
    std::uint8_t state;    ///< SlotState。
};

//! 静态表放在 BSS，容量固定，生命周期与进程相同：不构造、不析构、不分配，
//! 因此可以在静态初始化期和进程退出后安全访问。表满不会扩容（扩容需要堆），
//! 只累计 tableOverflows，提示调用方调大 MEMTRACK_TABLE_CAPACITY。
BlockInfo g_table[kCapacity] = {};

//! 整表唯一锁。它的存在使 insert/remove/dump 的槽位访问串行化；
//! 持有者不得调用任何可能再次进入钩子的函数。
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;

//! g_enabled 只控制“是否记录分配”；释放路径始终清理既有记录，避免 stop()
//! 之后释放块被误报为泄漏。atexitArmed 允许 stop() 取消本次自动落盘。
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_atexitArmed{false};

//! 计数使用原子量，stats() 无需加锁即可读取；写入发生在 g_lock 内，
//! 并发下各字段是分别读取的，不保证同一时刻的一致快照。
std::atomic<std::uint64_t> g_liveBlocks{0};
std::atomic<std::uint64_t> g_liveBytes{0};
std::atomic<std::uint64_t> g_totalAllocations{0};
std::atomic<std::uint64_t> g_unknownFrees{0};
std::atomic<std::uint64_t> g_tableOverflows{0};

//! 以下状态只允许在持有 g_lock 时读写。
std::uint64_t g_seq = 0;
bool g_atexitRegistered = false;

//! 显式日志路径：setLogPath/start(path) 设置后进入显式模式，优先于自动布局。
//! 路径在锁内复制进静态缓冲，落盘时先复制再 fopen，避免被并发改写。
char g_logPath[MEMTRACK_PATH_MAX] = "memtrack_leaks.log";
bool g_logPathExplicit = false;

//! 自动布局状态：首次自动 start() 解析出本次进程的运行目录后 g_runDir 有效。
//! stop()/start() 复用同一目录；时间戳只在创建时取一次。
char g_phaseName[MEMTRACK_PHASE_MAX] = "default";
char g_runDir[MEMTRACK_PATH_MAX] = {};
bool g_runDirReady = false;

//! 手动 dump 序号，锁内自增，保证同一次运行内的 manual-N.log 互不覆盖。
std::uint32_t g_manualSeq = 0;

//! 每个线程记录“当前是否已在跟踪器内部”。>0 时再次触发全局 new/delete
//! （例如 CRT 内部异常路径）必须直接透传，既避免递归，也避免同线程自锁。
thread_local unsigned g_reentryDepth = 0;

/**
 * @brief 单锁自旋守卫。
 * @details 临界区都只有 O(1) 探测或槽位复制，不自旋太久；YieldProcessor 仅
 *          用于降低忙等对超线程的争抢。
 */
class SpinLock
{
public:
    SpinLock() noexcept
    {
        while (g_lock.test_and_set(std::memory_order_acquire))
        {
#if defined(_WIN32)
            ::YieldProcessor();
#endif
        }
    }

    ~SpinLock() noexcept
    {
        g_lock.clear(std::memory_order_release);
    }

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
};

/**
 * @brief RAII 重入守卫。
 * @details 构造时把本线程深度加一，析构时减一。entered() 为 false 说明本次
 *          调用发生在跟踪器内部，调用方必须走透传路径。
 */
class ReentryGuard
{
public:
    ReentryGuard() noexcept
        : entered_(g_reentryDepth == 0u)
    {
        ++g_reentryDepth;
    }

    ~ReentryGuard() noexcept
    {
        --g_reentryDepth;
    }

    bool entered() const noexcept
    {
        return entered_;
    }

private:
    bool entered_;  ///< 本次调用是否由跟踪器外部发起。
};

/**
 * @brief splitmix64 雪崩混合。
 * @param[in] value 待混合的整数（通常是分配地址）。
 * @return 混合后的 64 位值。
 * @details 分配地址通常连续且低位对齐，直接取模会扎堆；先做整数混合再取低位，
 *          可让连续地址均匀落到槽位。
 */
std::uint64_t splitmix64(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

/**
 * @brief 计算地址对应的槽位下标。
 * @param[in] address 分配地址。
 * @return 槽位下标。
 * @note 容量是 2 的幂，用位与代替取模。
 */
std::size_t slotIndex(const void* address) noexcept
{
    const auto raw = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(address));
    return static_cast<std::size_t>(splitmix64(raw)) & (kCapacity - 1u);
}

/**
 * @brief 读取当前线程的 OS ID。
 * @return Windows 为 GetCurrentThreadId，POSIX 为 pthread_self 转换值。
 */
std::uint32_t currentThreadId() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentThreadId());
#else
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(::pthread_self()));
#endif
}

/**
 * @brief 读取当前进程 ID。
 * @return 进程 ID。
 */
long currentProcessId() noexcept
{
#if defined(_WIN32)
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

/**
 * @brief 后备分配器。
 * @param[in] size 请求字节数；0 按 1 字节处理。
 * @param[in] align 0 表示普通分配，非 0 表示对齐分配。
 * @return 分配成功返回指针，失败返回 nullptr。
 * @warning 必须使用 malloc/_aligned_malloc，绝不能调用 operator new，否则会
 *          重新进入本文件的钩子。两种形式返回的指针必须用对应的 rawFree 形式
 *          释放，禁止混用。
 */
void* rawAlloc(std::size_t size, std::size_t align) noexcept
{
    const std::size_t bytes = size == 0u ? 1u : size;
    if (align != 0u)
    {
#if defined(_WIN32)
        return ::_aligned_malloc(bytes, align);
#else
        void* p = nullptr;
        if (::posix_memalign(&p, align, bytes) != 0)
        {
            return nullptr;
        }
        return p;
#endif
    }
    return std::malloc(bytes);
}

/**
 * @brief 后备释放函数，与 rawAlloc 配对。
 * @param[in] p 待释放指针；nullptr 直接返回。
 * @param[in] align 必须来自分配时的记录或 delete 调用形式。
 * @warning 普通块走 free，对齐块走 _aligned_free；混用会导致堆损坏。
 */
void rawFree(void* p, std::size_t align) noexcept
{
    if (p == nullptr)
    {
        return;
    }
#if defined(_WIN32)
    if (align != 0u)
    {
        ::_aligned_free(p);
        return;
    }
#else
    (void)align;
#endif
    std::free(p);
}

/**
 * @brief 填充槽位并更新计数。
 * @param[out] slot 目标槽位。
 * @param[in] address 分配地址。
 * @param[in] size 请求字节数。
 * @param[in] file 分配位置；可为 nullptr。
 * @param[in] line 分配行号。
 * @param[in] align 对齐值；0 表示普通分配。
 * @note 调用方必须已持有 g_lock（g_seq 与计数更新依赖该前提）。
 */
void fillSlot(BlockInfo& slot,
              const void* address,
              std::size_t size,
              const char* file,
              int line,
              std::uint32_t align) noexcept
{
    slot.address = address;
    slot.size = size;
    slot.file = file;
    slot.seq = ++g_seq;
    slot.line = line;
    slot.tid = currentThreadId();
    slot.align = align;
    slot.state = kSlotUsed;
    g_liveBlocks.fetch_add(1, std::memory_order_relaxed);
    g_liveBytes.fetch_add(size, std::memory_order_relaxed);
    g_totalAllocations.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief 插入一条分配记录。
 * @param[in] address 分配地址。
 * @param[in] size 请求字节数。
 * @param[in] file 分配位置；可为 nullptr。
 * @param[in] line 分配行号。
 * @param[in] align 对齐值；0 表示普通分配。
 * @return true 表示记录成功；false 表示地址重复或表已满。
 * @note 调用方必须持有 g_lock。
 * @details 开放寻址线性探测：优先复用探测路径上遇到的第一个墓碑，找不到空槽
 *          时再使用墓碑；整表既无空槽也无墓碑则丢弃记录并累计溢出。
 *          重复地址视为内部异常（正常流程中释放会先移除记录），直接返回 false。
 */
bool tableInsert(const void* address,
                 std::size_t size,
                 const char* file,
                 int line,
                 std::uint32_t align) noexcept
{
    std::size_t index = slotIndex(address);
    std::size_t tombstone = kCapacity;

    for (std::size_t probe = 0; probe < kCapacity; ++probe)
    {
        BlockInfo& slot = g_table[index];

        if (slot.state == kSlotEmpty)
        {
            BlockInfo& target = (tombstone != kCapacity) ? g_table[tombstone] : slot;
            fillSlot(target, address, size, file, line, align);
            return true;
        }

        if (slot.state == kSlotUsed)
        {
            if (slot.address == address)
            {
                return false;
            }
        }
        else if (tombstone == kCapacity)
        {
            //! 只记住第一个墓碑，保证不会把新记录插到更靠后的位置而跳过空槽。
            tombstone = index;
        }

        index = (index + 1u) & (kCapacity - 1u);
    }

    if (tombstone != kCapacity)
    {
        fillSlot(g_table[tombstone], address, size, file, line, align);
        return true;
    }

    g_tableOverflows.fetch_add(1, std::memory_order_relaxed);
    return false;
}

/**
 * @brief 移除一条分配记录。
 * @param[in] address 待查找的分配地址。
 * @param[out] alignOut 命中时写入记录中的对齐值。
 * @return true 表示命中并移除；false 表示未找到。
 * @note 调用方必须持有 g_lock。
 * @details 探测必须越过墓碑、直到遇到真正的空槽才能判定未找到，否则会漏掉
 *          因墓碑被复用而“跨过”的记录。命中后把槽位标记为墓碑并清空字段。
 */
bool tableRemove(const void* address, std::uint32_t& alignOut) noexcept
{
    std::size_t index = slotIndex(address);

    for (std::size_t probe = 0; probe < kCapacity; ++probe)
    {
        BlockInfo& slot = g_table[index];

        if (slot.state == kSlotEmpty)
        {
            return false;
        }

        if (slot.state == kSlotUsed && slot.address == address)
        {
            alignOut = slot.align;
            const std::size_t size = slot.size;

            slot.state = kSlotTombstone;
            slot.address = nullptr;
            slot.size = 0;
            slot.file = nullptr;
            slot.line = 0;
            slot.tid = 0;
            slot.align = 0;

            g_liveBlocks.fetch_sub(1, std::memory_order_relaxed);
            g_liveBytes.fetch_sub(size, std::memory_order_relaxed);
            return true;
        }

        index = (index + 1u) & (kCapacity - 1u);
    }

    return false;
}

/**
 * @brief 已开启跟踪时的分配路径。
 * @param[in] size 请求字节数。
 * @param[in] align 对齐值；0 表示普通分配。
 * @param[in] file 分配位置；可为 nullptr。
 * @param[in] line 分配行号。
 * @return 分配成功返回指针，失败返回 nullptr。
 * @details 先向后备分配器要内存，成功后才登记记录。分配失败不写表；表满时
 *          记录被丢弃但内存照常返回（由调用方决定抛异常还是返回 nullptr）。
 * @note 本函数不抛异常，避免在 new 内部产生额外分配。
 */
void* allocateTracked(std::size_t size,
                      std::size_t align,
                      const char* file,
                      int line) noexcept
{
    void* p = rawAlloc(size, align);
    if (p == nullptr)
    {
        return nullptr;
    }

    {
        SpinLock lock;
        tableInsert(p, size, file, line, static_cast<std::uint32_t>(align));
    }
    return p;
}

/**
 * @brief 释放路径。
 * @param[in] p 待释放指针；nullptr 直接返回。
 * @param[in] fallbackAlign 来自 delete 调用形式的对齐值，用于未命中记录时兜底。
 * @details 资源责任：无论是否命中记录，都要把内存还给后备分配器。
 *          - 重入时（跟踪器内部释放）直接透传，绝不加锁，避免自锁；
 *          - 命中记录：用记录中的 align 选择释放函数，然后移除记录；
 *          - 未命中：可能是 stop()/start() 之前分配的块或重复释放，计入
 *            unknownFrees 并无害释放。
 * @note 释放不受 g_enabled 影响，保证 stop() 之后释放仍能清理旧记录。
 */
void freeTracked(void* p, std::size_t fallbackAlign) noexcept
{
    if (p == nullptr)
    {
        return;
    }

    if (g_reentryDepth != 0u)
    {
        rawFree(p, fallbackAlign);
        return;
    }

    ReentryGuard guard;

    std::uint32_t storedAlign = 0;
    bool found = false;
    {
        SpinLock lock;
        found = tableRemove(p, storedAlign);
    }

    if (!found)
    {
        g_unknownFrees.fetch_add(1, std::memory_order_relaxed);
        rawFree(p, fallbackAlign);
        return;
    }

    rawFree(p, static_cast<std::size_t>(storedAlign));
}

/**
 * @brief 抛异常版分配入口。
 * @param[in] size 请求字节数。
 * @param[in] align 对齐值；0 表示普通分配。
 * @param[in] file 分配位置；可为 nullptr。
 * @param[in] line 分配行号。
 * @return 分配成功的指针。
 * @throw std::bad_alloc 分配失败时抛出（与标准 operator new 一致）。
 * @note 仅在“已开启且非重入”时登记记录；否则透传。记录只发生在分配成功之后。
 */
void* newOrThrow(std::size_t size, std::size_t align, const char* file, int line)
{
    void* p = nullptr;
    {
        ReentryGuard guard;
        if (guard.entered() && g_enabled.load(std::memory_order_relaxed))
        {
            p = allocateTracked(size, align, file, line);
        }
        else
        {
            p = rawAlloc(size, align);
        }
    }

    if (p == nullptr)
    {
        throw std::bad_alloc();
    }
    return p;
}

/**
 * @brief nothrow 版分配入口。
 * @param[in] size 请求字节数。
 * @param[in] align 对齐值；0 表示普通分配。
 * @param[in] file 分配位置；可为 nullptr。
 * @param[in] line 分配行号。
 * @return 分配成功返回指针，失败返回 nullptr。
 * @note 不抛异常；记录规则同 newOrThrow。
 */
void* newNoThrow(std::size_t size, std::size_t align, const char* file, int line) noexcept
{
    ReentryGuard guard;
    if (guard.entered() && g_enabled.load(std::memory_order_relaxed))
    {
        return allocateTracked(size, align, file, line);
    }
    return rawAlloc(size, align);
}

/**
 * @brief 生成本地时间字符串。
 * @param[in] format strftime 格式串。
 * @param[out] buffer 输出缓冲。
 * @param[in] capacity 缓冲字节数。
 * @note 失败时输出空串，调用方按“无时间戳”处理。
 */
void formatLocalTime(const char* format, char* buffer, std::size_t capacity) noexcept
{
    std::time_t now = std::time(nullptr);
    std::tm local{};

#if defined(_WIN32)
    if (::localtime_s(&local, &now) != 0)
    {
        buffer[0] = '\0';
        return;
    }
#else
    if (::localtime_r(&now, &local) == nullptr)
    {
        buffer[0] = '\0';
        return;
    }
#endif

    if (std::strftime(buffer, capacity, format, &local) == 0u)
    {
        buffer[0] = '\0';
    }
}

/**
 * @brief 格式化日志头部使用的 ISO 8601 时间（秒精度）。
 * @param[out] buffer 输出缓冲。
 * @param[in] capacity 缓冲字节数。
 */
void formatTimestamp(char* buffer, std::size_t capacity) noexcept
{
    formatLocalTime("%Y-%m-%dT%H:%M:%S", buffer, capacity);
}

/**
 * @brief 格式化运行目录名使用的时间戳。
 * @param[out] buffer 输出缓冲。
 * @param[in] capacity 缓冲字节数。
 * @note 紧凑、无分隔歧义，便于目录排序。
 */
void formatRunStamp(char* buffer, std::size_t capacity) noexcept
{
    formatLocalTime("%Y%m%d-%H%M%S", buffer, capacity);
}

/**
 * @brief 创建单级目录。
 * @param[in] path 目录路径。
 * @return true 表示创建成功或目录已存在；false 表示创建失败。
 * @note 只使用 CRT 的 _mkdir/mkdir（内部走 malloc/OS），不触发被重载的
 *       operator new。调用方负责保证父目录已经创建。
 */
bool ensureDirectory(const char* path) noexcept
{
#if defined(_WIN32)
    if (::_mkdir(path) == 0)
    {
        return true;
    }
#else
    if (::mkdir(path, 0755) == 0)
    {
        return true;
    }
#endif
    return errno == EEXIST;
}

/**
 * @brief 规划并创建本次运行的日志目录。
 * @param[in] phase 阶段目录名。
 * @param[out] runDir 输出本次运行的目录路径。
 * @param[in] capacity runDir 的字节数。
 * @return true 表示目录已就绪；false 表示路径拼接溢出或目录创建失败，调用方
 *         应放弃开启跟踪。
 * @details 目录布局为
 *          <MEMTRACK_LOG_ROOT>/<phase>/<yyyyMMdd-HHmmss>_pid<pid>/。
 *          时间取首次 start 的时刻，pid 用于区分同一秒启动的多个进程。
 * @note 全程只用静态缓冲 + snprintf + mkdir，不分配内存。
 */
bool prepareRunDirectory(const char* phase, char* runDir, std::size_t capacity) noexcept
{
    char stamp[32];
    formatRunStamp(stamp, sizeof(stamp));
    if (stamp[0] == '\0')
    {
        return false;
    }

    char rootDir[MEMTRACK_PATH_MAX];
    char phaseDir[MEMTRACK_PATH_MAX];
    const int rootLen = std::snprintf(rootDir, sizeof(rootDir), "%s", MEMTRACK_LOG_ROOT);
    const int phaseLen = std::snprintf(phaseDir, sizeof(phaseDir), "%s/%s",
                                       MEMTRACK_LOG_ROOT, phase);
    const int runLen = std::snprintf(runDir, capacity, "%s/%s/%s_pid%ld",
                                     MEMTRACK_LOG_ROOT, phase, stamp, currentProcessId());

    if (rootLen <= 0 || static_cast<std::size_t>(rootLen) >= sizeof(rootDir)
        || phaseLen <= 0 || static_cast<std::size_t>(phaseLen) >= sizeof(phaseDir)
        || runLen <= 0 || static_cast<std::size_t>(runLen) >= capacity)
    {
        return false;
    }

    return ensureDirectory(rootDir)
        && ensureDirectory(phaseDir)
        && ensureDirectory(runDir);
}

/**
 * @brief 解析一次默认落盘的目标路径。
 * @param[out] out 输出路径缓冲。
 * @param[in] capacity 缓冲字节数。
 * @param[in] manual true 表示手动 dump（使用 manual-N.log），false 表示 atexit。
 * @return true 表示路径已写入且未溢出。
 * @details 解析规则：
 *          - 显式路径模式：返回 setLogPath/start(path) 设置的路径（可能覆盖
 *            旧文件）；
 *          - 自动模式：atexit 用 <runDir>/run.log，手动 dump 用
 *            <runDir>/manual-N.log；
 *          - 自动目录尚未初始化（未 start 且无显式路径）：回退到旧默认文件。
 * @note 路径状态在锁内复制，snprintf 在锁外执行；锁内不做 IO。
 */
bool buildDefaultDumpPath(char* out, std::size_t capacity, bool manual) noexcept
{
    char runDir[MEMTRACK_PATH_MAX];
    char explicitPath[MEMTRACK_PATH_MAX];
    std::uint32_t seq = 0;
    bool useRunDir = false;

    {
        SpinLock lock;
        if (!g_logPathExplicit && g_runDirReady)
        {
            std::memcpy(runDir, g_runDir, sizeof(runDir));
            useRunDir = true;
            if (manual)
            {
                seq = ++g_manualSeq;
            }
        }
        else
        {
            std::memcpy(explicitPath, g_logPath, sizeof(explicitPath));
        }
    }

    int written = 0;
    if (!useRunDir)
    {
        written = std::snprintf(out, capacity, "%s", explicitPath);
        return written > 0 && static_cast<std::size_t>(written) < capacity;
    }

    if (manual)
    {
        written = std::snprintf(out, capacity, "%s/manual-%u.log", runDir, seq);
    }
    else
    {
        written = std::snprintf(out, capacity, "%s/run.log", runDir);
    }
    return written > 0 && static_cast<std::size_t>(written) < capacity;
}

/**
 * @brief 日志落盘。
 * @param[in] path 目标文件路径。
 * @param[in] reason 写入 dump_reason 的原因字符串（"manual"/"atexit"）。
 * @return 实际写出的块行数；文件打不开返回 -1。
 * @details 格式契约见 PROJECT_PROGRESS.md 第 8 节：头部键序固定、每条块一行且
 *          file 放在行尾，后续阶段只能追加键。
 * @note 无堆分配：仅用 snprintf 风格的 fprintf 与 stdio；std::fopen 内部走 CRT
 *       malloc，不经过被重载的 operator new，因此不会递归进入钩子。
 * @note 锁粒度：逐槽加锁复制一条记录后立刻解锁再格式化写出，既保证每条记录
 *       完整，又不会在持锁期间做 IO；并发分配时各行的可见性可能略有先后。
 */
int dumpToFile(const char* path, const char* reason) noexcept
{
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr)
    {
        return -1;
    }

    const Stats snapshot = memtrack::stats();
    char timestamp[32];
    formatTimestamp(timestamp, sizeof(timestamp));

    std::fprintf(file, "# qtmemtrack leak report v1\n");
    std::fprintf(file, "generated_at=%s\n", timestamp);
    std::fprintf(file, "process_id=%ld\n", currentProcessId());
    std::fprintf(file, "dump_reason=%s\n", reason);
    std::fprintf(file, "table_capacity=%llu\n",
                 static_cast<unsigned long long>(kCapacity));
    std::fprintf(file, "live_blocks=%llu\n",
                 static_cast<unsigned long long>(snapshot.liveBlocks));
    std::fprintf(file, "live_bytes=%llu\n",
                 static_cast<unsigned long long>(snapshot.liveBytes));
    std::fprintf(file, "unknown_frees=%llu\n",
                 static_cast<unsigned long long>(snapshot.unknownFrees));
    std::fprintf(file, "table_overflows=%llu\n",
                 static_cast<unsigned long long>(snapshot.tableOverflows));

    int written = 0;
    for (std::size_t i = 0; i < kCapacity; ++i)
    {
        BlockInfo copy{};
        bool used = false;
        {
            SpinLock lock;
            if (g_table[i].state == kSlotUsed)
            {
                copy = g_table[i];
                used = true;
            }
        }

        if (!used)
        {
            continue;
        }

        //! file 是行尾原始值（允许空格）；未知位置按契约输出 <unknown>。
        std::fprintf(file,
                     "block seq=%llu addr=0x%016llX size=%llu tid=%lu align=%lu line=%d file=%s\n",
                     static_cast<unsigned long long>(copy.seq),
                     static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(copy.address)),
                     static_cast<unsigned long long>(copy.size),
                     static_cast<unsigned long>(copy.tid),
                     static_cast<unsigned long>(copy.align),
                     copy.line,
                     copy.file != nullptr ? copy.file : "<unknown>");
        ++written;
    }

    std::fprintf(file, "# end\n");
    std::fflush(file);
    std::fclose(file);
    return written;
}

/**
 * @brief atexit 处理器：按当前默认路径写一份快照。
 * @details 资源责任：文件句柄由 dumpToFile 内部关闭；本函数忽略失败与返回值。
 * @note stop() 会取消布防，因此用户主动 stop 后不会自动落盘；手动 dumpLeaks
 *       不受影响。
 * @warning atexit 在 main 之后、静态对象析构之前执行，静态析构期释放的块可能
 *          仍出现在报告中，属契约已知限制。
 */
void atExitDump() noexcept
{
#if MEMTRACK_ENABLE_ATEXIT
    if (!g_atexitArmed.load(std::memory_order_relaxed))
    {
        return;
    }

    char path[MEMTRACK_PATH_MAX];
    if (!buildDefaultDumpPath(path, sizeof(path), false))
    {
        return;
    }
    dumpToFile(path, "atexit");
#endif
}

} // namespace

/**
 * @brief 全局分配钩子组。
 * @details 资源责任：本组函数是所有 operator new 的唯一入口；成功分配的内存
 *          由调用方负责释放，释放时统一落到下方 operator delete 组。跟踪器
 *          只旁观，不接管所有权。
 * @note 普通与数组形式无位置信息，记录为 file=<unknown> line=0。带宏的 TU
 *       不会走到这里（宏改写成 placement 形式），但无宏 TU、STL 内部与 Qt
 *       内部会。
 */
void* operator new(std::size_t size)
{
    return newOrThrow(size, 0u, nullptr, 0);
}

void* operator new[](std::size_t size)
{
    return newOrThrow(size, 0u, nullptr, 0);
}

/**
 * @brief placement new，由 MemHook.h 的宏生成。
 * @param[in] size 请求字节数。
 * @param[in] file 展开点所在文件名（__FILE__）。
 * @param[in] line 展开点所在行号（__LINE__）。
 * @return 已登记分配的地址。
 */
void* operator new(std::size_t size, const char* file, int line)
{
    return newOrThrow(size, 0u, file, line);
}

/**
 * @brief placement new[]，由 MemHook.h 的宏生成。
 * @param[in] size 请求字节数。
 * @param[in] file 展开点所在文件名（__FILE__）。
 * @param[in] line 展开点所在行号（__LINE__）。
 * @return 已登记分配的地址。
 */
void* operator new[](std::size_t size, const char* file, int line)
{
    return newOrThrow(size, 0u, file, line);
}

/**
 * @brief nothrow 版全局 new 与 new[]。
 * @param[in] size 请求字节数。
 * @return 分配成功返回指针，失败返回 nullptr（不抛异常）。
 */
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return newNoThrow(size, 0u, nullptr, 0);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return newNoThrow(size, 0u, nullptr, 0);
}

/**
 * @brief 全局 delete 组。
 * @details sized/nothrow/placement 形式都归一到 freeTracked。普通 delete
 *          无法从调用形式得知对齐值，因此未命中记录时只能按普通 free 兜底；
 *          命中的对齐块会从记录中取回 align 并使用 _aligned_free。
 */
void operator delete(void* p) noexcept
{
    freeTracked(p, 0u);
}

void operator delete[](void* p) noexcept
{
    freeTracked(p, 0u);
}

void operator delete(void* p, std::size_t) noexcept
{
    freeTracked(p, 0u);
}

void operator delete[](void* p, std::size_t) noexcept
{
    freeTracked(p, 0u);
}

void operator delete(void* p, const std::nothrow_t&) noexcept
{
    freeTracked(p, 0u);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    freeTracked(p, 0u);
}

/**
 * @brief 构造函数抛异常时编译器调用的配对释放。
 * @param[in] p 待释放指针。
 * @note 必须存在，否则该路径会漏掉记录的清理（内存由编译器按 placement
 *       delete 语义交给这里）。
 */
void operator delete(void* p, const char*, int) noexcept
{
    freeTracked(p, 0u);
}

void operator delete[](void* p, const char*, int) noexcept
{
    freeTracked(p, 0u);
}

#if MEMTRACK_ENABLE_ALIGNED

/**
 * @brief C++17 aligned new/delete 重载族。
 * @details 对齐值由调用形式给出，但释放时仍优先使用记录中保存的 align，确保
 *          _aligned_malloc 与 _aligned_free 严格配对。
 * @note 宏无法生成 aligned placement 语法，因此这一组只覆盖无位置信息的对齐
 *       分配。
 */
void* operator new(std::size_t size, std::align_val_t align)
{
    return newOrThrow(size, static_cast<std::size_t>(align), nullptr, 0);
}

void* operator new[](std::size_t size, std::align_val_t align)
{
    return newOrThrow(size, static_cast<std::size_t>(align), nullptr, 0);
}

void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept
{
    return newNoThrow(size, static_cast<std::size_t>(align), nullptr, 0);
}

void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept
{
    return newNoThrow(size, static_cast<std::size_t>(align), nullptr, 0);
}

void operator delete(void* p, std::align_val_t align) noexcept
{
    freeTracked(p, static_cast<std::size_t>(align));
}

void operator delete[](void* p, std::align_val_t align) noexcept
{
    freeTracked(p, static_cast<std::size_t>(align));
}

void operator delete(void* p, std::size_t, std::align_val_t align) noexcept
{
    freeTracked(p, static_cast<std::size_t>(align));
}

void operator delete[](void* p, std::size_t, std::align_val_t align) noexcept
{
    freeTracked(p, static_cast<std::size_t>(align));
}

#endif // MEMTRACK_ENABLE_ALIGNED

/**
 * @brief 公开 API 实现。
 * @details 全部接口不分配内存、不抛异常；与钩子的共享状态通过 g_lock 或原子量
 *          访问。
 */
namespace memtrack {

bool setLogPath(const char* path) noexcept
{
    if (path == nullptr || path[0] == '\0')
    {
        return false;
    }

    const std::size_t length = std::strlen(path);
    if (length >= MEMTRACK_PATH_MAX)
    {
        return false;
    }

    SpinLock lock;
    std::memcpy(g_logPath, path, length + 1u);
    //! 显式路径优先于自动布局；之后不再创建 logs/<phase>/... 目录。
    g_logPathExplicit = true;
    return true;
}

bool setPhaseName(const char* phase) noexcept
{
    if (phase == nullptr)
    {
        return false;
    }

    const std::size_t length = std::strlen(phase);
    if (length == 0u || length >= MEMTRACK_PHASE_MAX)
    {
        return false;
    }
    if (std::strcmp(phase, ".") == 0 || std::strcmp(phase, "..") == 0)
    {
        return false;
    }

    //! 只允许单段目录名，避免通过阶段名跳出 logs 根目录。
    for (std::size_t i = 0; i < length; ++i)
    {
        const char c = phase[i];
        if (c == '/' || c == '\\' || c == ':'
            || static_cast<unsigned char>(c) < 0x20u)
        {
            return false;
        }
    }

    SpinLock lock;
    if (g_runDirReady)
    {
        return false;
    }
    std::memcpy(g_phaseName, phase, length + 1u);
    return true;
}

bool start(const char* logPath) noexcept
{
    if (logPath != nullptr && !setLogPath(logPath))
    {
        return false;
    }

    //! 自动布局：首次自动 start 时创建运行目录。目录创建是配置阶段的唯一 IO，
    //! 放在启用记录之前且不持表锁；失败则不开启跟踪，避免产生无处落盘的记录。
    bool needAutoDirectory = false;
    char phase[MEMTRACK_PHASE_MAX];
    {
        SpinLock lock;
        if (!g_logPathExplicit && !g_runDirReady)
        {
            std::memcpy(phase, g_phaseName, sizeof(phase));
            needAutoDirectory = true;
        }
    }

    if (needAutoDirectory)
    {
        char runDir[MEMTRACK_PATH_MAX];
        if (!prepareRunDirectory(phase, runDir, sizeof(runDir)))
        {
            return false;
        }

        SpinLock lock;
        if (!g_runDirReady && !g_logPathExplicit)
        {
            std::memcpy(g_runDir, runDir, sizeof(g_runDir));
            g_runDirReady = true;
        }
    }

    g_enabled.store(true, std::memory_order_release);

#if MEMTRACK_ENABLE_ATEXIT
    //! atexit 只注册一次；反复 stop()/start() 只切换布防标志，避免重复注册
    //! 导致退出时写出多份相同报告。
    bool registerHandler = false;
    {
        SpinLock lock;
        if (!g_atexitRegistered)
        {
            g_atexitRegistered = true;
            registerHandler = true;
        }
        g_atexitArmed.store(true, std::memory_order_relaxed);
    }

    if (registerHandler)
    {
        (void)std::atexit(&atExitDump);
    }
#endif

    return true;
}

void stop() noexcept
{
    g_enabled.store(false, std::memory_order_release);
#if MEMTRACK_ENABLE_ATEXIT
    g_atexitArmed.store(false, std::memory_order_relaxed);
#endif
}

Stats stats() noexcept
{
    Stats result{};
    result.liveBlocks = g_liveBlocks.load(std::memory_order_relaxed);
    result.liveBytes = g_liveBytes.load(std::memory_order_relaxed);
    result.totalAllocations = g_totalAllocations.load(std::memory_order_relaxed);
    result.unknownFrees = g_unknownFrees.load(std::memory_order_relaxed);
    result.tableOverflows = g_tableOverflows.load(std::memory_order_relaxed);
    result.enabled = g_enabled.load(std::memory_order_relaxed);
    return result;
}

int dumpLeaks(const char* path) noexcept
{
    char resolved[MEMTRACK_PATH_MAX];
    if (path != nullptr)
    {
        if (path[0] == '\0')
        {
            return -1;
        }

        const std::size_t length = std::strlen(path);
        if (length >= MEMTRACK_PATH_MAX)
        {
            return -1;
        }
        std::memcpy(resolved, path, length + 1u);
    }
    else
    {
        if (!buildDefaultDumpPath(resolved, sizeof(resolved), true))
        {
            return -1;
        }
    }

    return dumpToFile(resolved, "manual");
}

} // namespace memtrack
