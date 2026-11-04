// =============================================================================
// 文件：demo/phase2/main.cpp
// 项目：QtMemoryLeakTool / QtMemTracker
// 阶段：阶段二 —— Qt 对象树监控界面
// 用途：阶段二可独立运行入口。构建测试窗口（带父子关系的 QPushButton/QLabel/
//       QWidget），用 QTreeView + ObjectTreeModel 快照展示 QObject 对象树；
//       同时接入阶段一跟踪器（阶段名 phase2，UI 构建完成后再 start）。
//
// 运行方式：
//   Phase2App                 显示主窗口，点击“刷新对象树”重建快照；
//   Phase2App --self-check    使用 offscreen 平台自动校验对象树拓扑与日志集成，
//                             输出 PASS/FAIL 并以 0/1 退出（供构建后验证）。
//
// 约束：本文件不包含 MemHook.h（避免 new 宏侵入 Qt 头文件）；Qt 代码中的分配
//       仍由全局钩子记录，位置为 file=<unknown> line=0。
// =============================================================================
#include <QAbstractItemModel>
#include <QApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

#include <cstdio>
#include <cstring>

#include "MemTracker.h"
#include "ObjectTreeModel.h"

namespace {

// 搭建阶段二测试窗口。
// window：栈上主窗口，作为对象树根；函数内创建的控件都挂在它下面。
// model：对象树模型，最后以 &window 为目标重建快照。
// 返回：对象树视图，供刷新后 expandAll() 使用。
QTreeView* buildTestWindow(QMainWindow& window, ObjectTreeModel& model)
{
    window.setObjectName(QStringLiteral("mainWindow"));
    window.setWindowTitle(QStringLiteral("QtMemTracker 阶段二 - QObject 对象树"));
    window.resize(1200, 960);

    // centralWidget 由 QMainWindow 接管，父子关系进入对象树。
    auto* central = new QWidget(&window);
    central->setObjectName(QStringLiteral("centralWidget"));
    window.setCentralWidget(central);

    auto* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(8, 8, 8, 8);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    rootLayout->addWidget(splitter);

    // 左侧：对象树视图。模型指针在窗口存活期间始终有效。
    auto* view = new QTreeView(splitter);
    view->setObjectName(QStringLiteral("objectTreeView"));
    view->setModel(&model);
    view->setAlternatingRowColors(true);
    view->setUniformRowHeights(true);
    // 水平/垂直滚动条常驻，树较宽或较长时始终可滚动。
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->header()->setStretchLastSection(false);
    // 四列全部 Interactive：用户可拖动列边界调整宽度；不设 Stretch/
    // ResizeToContents，避免列宽被自动锁定。以下仅为初始宽度。
    for (int column = 0; column < 4; ++column)
    {
        view->header()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    view->header()->resizeSection(0, 220);
    view->header()->resizeSection(1, 240);
    view->header()->resizeSection(2, 120);
    view->header()->resizeSection(3, 260);

    // 右侧：测试控件区，故意构造两级父子关系：
    // testGroupBox -> nestedPanel -> nestedButton。
    auto* panel = new QWidget(splitter);
    panel->setObjectName(QStringLiteral("testPanel"));
    auto* panelLayout = new QVBoxLayout(panel);

    // 搜索框：按对象名子串实时高亮，空文本清除高亮。
    auto* searchEdit = new QLineEdit(panel);
    searchEdit->setObjectName(QStringLiteral("searchEdit"));
    searchEdit->setPlaceholderText(QStringLiteral("按对象名搜索（实时高亮）"));
    searchEdit->setClearButtonEnabled(true);
    panelLayout->addWidget(searchEdit);

    auto* group = new QGroupBox(QStringLiteral("对象关系测试区"), panel);
    group->setObjectName(QStringLiteral("testGroupBox"));
    auto* groupLayout = new QVBoxLayout(group);

    auto* info = new QLabel(QStringLiteral("本标签的父对象是 testGroupBox"), group);
    info->setObjectName(QStringLiteral("infoLabel"));
    groupLayout->addWidget(info);

    auto* okButton = new QPushButton(QStringLiteral("确定"), group);
    okButton->setObjectName(QStringLiteral("okButton"));
    groupLayout->addWidget(okButton);

    auto* nested = new QWidget(group);
    nested->setObjectName(QStringLiteral("nestedPanel"));
    auto* nestedLayout = new QVBoxLayout(nested);
    nestedLayout->setContentsMargins(0, 0, 0, 0);

    auto* nestedButton = new QPushButton(QStringLiteral("嵌套按钮"), nested);
    nestedButton->setObjectName(QStringLiteral("nestedButton"));
    nestedLayout->addWidget(nestedButton);

    groupLayout->addWidget(nested);
    panelLayout->addWidget(group);

    auto* refreshButton = new QPushButton(QStringLiteral("刷新对象树"), panel);
    refreshButton->setObjectName(QStringLiteral("refreshButton"));
    panelLayout->addWidget(refreshButton);

    auto* hint = new QLabel(
        QStringLiteral("点击“刷新对象树”重建快照；树中按 QObject 父子关系递归展开。"),
        panel);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    panelLayout->addWidget(hint);
    panelLayout->addStretch(1);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    // 搜索：只更新命中行的背景色，不重建快照，展开状态保持不变。
    QObject::connect(searchEdit, &QLineEdit::textChanged, &window,
                     [&model](const QString& text)
                     {
                         model.setSearchText(text);
                     });

    // 刷新：重建快照后重新展开，保证新增子对象可见。
    QObject::connect(refreshButton, &QPushButton::clicked, &window,
                     [&model, view]()
                     {
                         model.refresh();
                         view->expandAll();
                     });

    // 按钮点击只更新状态栏，用于人工确认控件可用。
    QObject::connect(okButton, &QPushButton::clicked, &window,
                     [&window]()
                     {
                         window.statusBar()->showMessage(
                             QStringLiteral("确定按钮被点击"), 2000);
                     });

    window.statusBar()->showMessage(
        QStringLiteral("对象树为快照式展示，结构变化后请点击刷新"));

    // 以主窗口为根重建一次快照。窗口在 main 栈上，生命周期覆盖模型使用期。
    model.setRootObject(&window);
    return view;
}

// 递归收集模型中的对象名与节点数，仅使用 QAbstractItemModel 公共接口。
void collectNodeNames(const QAbstractItemModel& model,
                      const QModelIndex& parent,
                      QStringList& names,
                      int& nodeCount,
                      int& maxDepth,
                      int depth)
{
    const int rows = model.rowCount(parent);
    for (int row = 0; row < rows; ++row)
    {
        const QModelIndex index = model.index(row, 0, parent);
        if (!index.isValid())
        {
            continue;
        }
        names.append(index.data(Qt::DisplayRole).toString());
        ++nodeCount;
        const int currentDepth = depth + 1;
        if (currentDepth > maxDepth)
        {
            maxDepth = currentDepth;
        }
        collectNodeNames(model, index, names, nodeCount, maxDepth, currentDepth);
    }
}

// 按展示名查找模型索引（深度优先）。
bool findByName(const QAbstractItemModel& model,
                const QModelIndex& parent,
                const QString& name,
                QModelIndex& result)
{
    const int rows = model.rowCount(parent);
    for (int row = 0; row < rows; ++row)
    {
        const QModelIndex index = model.index(row, 0, parent);
        if (!index.isValid())
        {
            continue;
        }
        if (index.data(Qt::DisplayRole).toString() == name)
        {
            result = index;
            return true;
        }
        if (findByName(model, index, name, result))
        {
            return true;
        }
    }
    return false;
}

// 判断 name 是否位于 rootIndex 的后代中（不含根自身）。
bool containsName(const QAbstractItemModel& model,
                  const QModelIndex& rootIndex,
                  const QString& name)
{
    const int rows = model.rowCount(rootIndex);
    for (int row = 0; row < rows; ++row)
    {
        const QModelIndex index = model.index(row, 0, rootIndex);
        if (!index.isValid())
        {
            continue;
        }
        if (index.data(Qt::DisplayRole).toString() == name)
        {
            return true;
        }
        if (containsName(model, index, name))
        {
            return true;
        }
    }
    return false;
}

// 自校验：验证根节点、必需对象名、嵌套拓扑、搜索高亮、展开/滚动条与落盘。
// 只通过模型/视图公共接口检查，不访问私有实现。
bool runSelfCheck(QTreeView& view, ObjectTreeModel& model)
{
    const QModelIndex root = model.index(0, 0);
    if (!root.isValid()
        || root.data(Qt::DisplayRole).toString() != QStringLiteral("mainWindow"))
    {
        std::fprintf(stderr, "self-check: root is not mainWindow\n");
        return false;
    }

    QStringList names;
    int nodeCount = 0;
    int maxDepth = 0;
    collectNodeNames(model, QModelIndex(), names, nodeCount, maxDepth, 0);

    const char* const requiredNames[] = {
        "mainWindow", "centralWidget", "mainSplitter", "objectTreeView",
        "testPanel", "testGroupBox", "infoLabel", "okButton",
        "nestedPanel", "nestedButton", "refreshButton", "hintLabel"};

    bool namesOk = true;
    for (const char* name : requiredNames)
    {
        const QString expected = QString::fromLatin1(name);
        if (!names.contains(expected))
        {
            std::fprintf(stderr, "self-check: missing node %s\n", name);
            namesOk = false;
        }
    }

    QModelIndex nestedPanel;
    const bool nestedOk =
        findByName(model, QModelIndex(), QStringLiteral("nestedPanel"), nestedPanel)
        && containsName(model, nestedPanel, QStringLiteral("nestedButton"));
    if (!nestedOk)
    {
        std::fprintf(stderr, "self-check: nestedButton is not under nestedPanel\n");
    }

    const bool countOk = nodeCount >= 10;
    if (!countOk)
    {
        std::fprintf(stderr, "self-check: node count %d < 10\n", nodeCount);
    }

    // 默认展开：根与嵌套节点都应处于展开状态。
    const bool expandOk = view.isExpanded(root) && view.isExpanded(nestedPanel);
    if (!expandOk)
    {
        std::fprintf(stderr, "self-check: tree is not fully expanded\n");
    }

    // 滚动条常驻：两个方向都必须是 AlwaysOn。
    const bool scrollOk =
        view.horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOn
        && view.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOn;
    if (!scrollOk)
    {
        std::fprintf(stderr, "self-check: scroll bars are not always on\n");
    }

    // 列宽可拖动：四列都必须处于 Interactive 模式。
    bool columnsOk = true;
    for (int column = 0; column < model.columnCount(); ++column)
    {
        if (view.header()->sectionResizeMode(column) != QHeaderView::Interactive)
        {
            columnsOk = false;
        }
    }
    if (!columnsOk)
    {
        std::fprintf(stderr, "self-check: columns are not interactive\n");
    }

    // 搜索高亮：命中 nestedPanel 的整行应有背景色，根节点不应有；清空后恢复。
    model.setSearchText(QStringLiteral("nested"));
    const bool searchOk =
        nestedPanel.data(Qt::BackgroundRole).isValid()
        && !root.data(Qt::BackgroundRole).isValid();
    if (!searchOk)
    {
        std::fprintf(stderr, "self-check: search highlight mismatch\n");
    }
    model.setSearchText(QString());
    const bool clearOk = !nestedPanel.data(Qt::BackgroundRole).isValid();
    if (!clearOk)
    {
        std::fprintf(stderr, "self-check: search highlight was not cleared\n");
    }

    // 手动落盘一次，验证阶段一接口在 Qt 进程内可用；日志内容格式不变。
    const int manualRows = memtrack::dumpLeaks();
    const memtrack::Stats stats = memtrack::stats();

    std::printf("nodes=%d max_depth=%d expand=%s scrollbars=%s columns=%s "
                "search=%s live_blocks=%llu live_bytes=%llu unknown_frees=%llu "
                "table_overflows=%llu manual_rows=%d\n",
                nodeCount,
                maxDepth,
                expandOk ? "ok" : "fail",
                scrollOk ? "ok" : "fail",
                columnsOk ? "ok" : "fail",
                (searchOk && clearOk) ? "ok" : "fail",
                static_cast<unsigned long long>(stats.liveBlocks),
                static_cast<unsigned long long>(stats.liveBytes),
                static_cast<unsigned long long>(stats.unknownFrees),
                static_cast<unsigned long long>(stats.tableOverflows),
                manualRows);

    const bool ok = namesOk && nestedOk && countOk && expandOk && scrollOk
                 && columnsOk && searchOk && clearOk && manualRows >= 0;
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    bool selfCheck = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--self-check") == 0)
        {
            selfCheck = true;
        }
    }

    // 自校验必须使用 offscreen 平台，避免 CI/无人值守环境需要桌面会话。
    // 用户若显式设置了平台插件，则尊重用户设置。
    if (selfCheck && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Phase2App"));

    QMainWindow window;
    ObjectTreeModel model;
    QTreeView* view = buildTestWindow(window, model);

    // 阶段名必须在首次 start 前设置；UI 已构建完成，避免 Qt 初始化噪声。
    if (!memtrack::setPhaseName("phase2"))
    {
        std::fprintf(stderr, "memtrack::setPhaseName failed\n");
        return 2;
    }
    if (!memtrack::start())
    {
        std::fprintf(stderr, "memtrack::start failed\n");
        return 2;
    }

    window.show();
    // 展开必须在 show() 之后：QTreeView 在可见后才完成完整布局。
    view->expandAll();

    if (!selfCheck)
    {
        // 普通模式：进入事件循环；退出时 atexit 自动写 run.log。
        return app.exec();
    }

    // 自校验：等事件循环首轮完成后再检查，确保窗口已完成构建、显示与展开。
    QTimer::singleShot(0, &window,
                       [&app, &model, view]()
                       {
                           view->expandAll();
                           const bool ok = runSelfCheck(*view, model);
                           app.exit(ok ? 0 : 1);
                       });
    return app.exec();
}
