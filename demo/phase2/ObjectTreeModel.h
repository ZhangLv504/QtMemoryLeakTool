// =============================================================================
// 文件：demo/phase2/ObjectTreeModel.h
// 项目：QtMemoryLeakTool / QtMemTracker
// 阶段：阶段二 —— Qt 对象树监控界面
// 用途：只读对象树模型的声明。模型持有某一时刻的 QObject 父子关系快照，
//       以 4 列（对象/类型/子对象数/地址）展示，供 QTreeView 使用。
// 约束：
//   - 只在 GUI 线程访问；不提供锁，不跨线程共享。
//   - 不接管任何 QObject 的生命周期，只保存 QPointer 与字符串快照。
//   - 只读：不实现编辑，flags() 不返回 Qt::ItemIsEditable。
// =============================================================================
#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

// 单个对象树节点的快照。容器在 refresh() 完成后不再修改，
// 因此 QModelIndex::internalPointer 指向的地址在本次模型重置周期内稳定。
struct ObjectNode
{
    // 对应对象；若刷新后对象被销毁，QPointer 自动变空，模型不会解引用悬垂指针。
    QPointer<QObject> object;
    // 类型名（QObject::metaObject()->className() 的快照）。
    QString className;
    // 对象名快照；为空时显示 <unnamed>。
    QString objectName;
    // 对象地址快照，格式 0x%016llX。
    QString address;
    // 子对象数量快照。
    int childCount = 0;
    // 自己在父节点 children 中的行号；根节点为 0。
    int row = 0;
    // 父节点；根节点为 nullptr。
    ObjectNode* parent = nullptr;
    // 递归子节点。
    QVector<ObjectNode> children;
};

// 对象树只读模型。
// 目标对象（setRootObject 传入，通常是主窗口）作为唯一顶层行出现，
// 其子节点通过 QObject::children() 递归展开。
class ObjectTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit ObjectTreeModel(QObject* parent = nullptr);
    ~ObjectTreeModel() override;

    // 设置目标对象并立即重建快照。root 为 nullptr 时清空模型。
    // 调用方保留对象所有权；模型只保存 QPointer。
    void setRootObject(QObject* root);

    // 返回目标对象；对象已销毁或未设置时返回 nullptr。
    QObject* rootObject() const;

    // 设置搜索文本：非空时，对象名包含该文本（不区分大小写）的整行以淡蓝色
    // 背景高亮；空串清除高亮。只发送 BackgroundRole 的 dataChanged，
    // 不重建快照，因此不会丢失展开状态。
    void setSearchText(const QString& text);

    // 返回当前搜索文本；空串表示未搜索。
    QString searchText() const;

    // 重建快照：先 beginResetModel()，递归读取 QObject::children()，
    // 再 endResetModel()。可在对象树结构变化后由界面手动调用。
    void refresh();

    // QAbstractItemModel 只读实现。
    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    // 递归构造快照节点。仅由 refresh() 在 GUI 线程调用。
    ObjectNode buildNode(QObject* object, int row) const;

    // 快照构建完成后回填 parent/row 指针；此时容器不再扩容，地址稳定。
    static void fixParents(ObjectNode& node);

    // 展示名：objectName 非空则用它，否则为 <unnamed>。
    static QString displayName(const QObject* object);

    // 对象地址文本，固定 16 位十六进制。
    static QString addressText(const QObject* object);

    // 当前搜索文本是否命中该节点（按 objectName 子串、不区分大小写）。
    bool matchesSearch(const ObjectNode& node) const;

    // 递归通知视图刷新 BackgroundRole；搜索文本变化时调用。
    void notifyBackgroundChanged(const QModelIndex& parent = QModelIndex());

    // 是否持有可用的目标对象。
    bool hasRoot() const;

    // 目标对象，不拥有所有权。
    QPointer<QObject> m_root;
    // 快照根节点；作为唯一顶层行。
    ObjectNode m_rootNode;
    // 是否曾设置过目标对象。
    bool m_hasRoot = false;
    // 当前搜索文本；空串表示不高亮任何行。
    QString m_searchText;

    // 固定 4 列：对象 / 类型 / 子对象数 / 地址。
    static constexpr int kColumnCount = 4;
};
