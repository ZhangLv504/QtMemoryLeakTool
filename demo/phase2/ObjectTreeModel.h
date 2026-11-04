/**
 * @file ObjectTreeModel.h
 * @brief 只读对象树模型的声明。
 * @details 模型持有某一时刻的 QObject 父子关系快照，以 4 列（对象/类型/子对象数/
 *          地址）展示，供 QTreeView 使用。
 * @note 只在 GUI 线程访问；不提供锁，不跨线程共享。
 * @note 不接管任何 QObject 的生命周期，只保存 QPointer 与字符串快照。
 * @note 只读：不实现编辑，flags() 不返回 Qt::ItemIsEditable。
 */
#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

/**
 * @brief 单个对象树节点的快照。
 * @details 容器在 refresh() 完成后不再修改，因此 QModelIndex::internalPointer
 *          指向的地址在本次模型重置周期内稳定。
 */
struct ObjectNode
{
    QPointer<QObject> object;      ///< 对应对象；对象被销毁后自动变空，模型不会解引用悬垂指针。
    QString className;             ///< 类型名（QObject::metaObject()->className() 的快照）。
    QString objectName;            ///< 对象名快照；为空时显示 <unnamed>。
    QString address;               ///< 对象地址快照，格式 0x%016llX。
    int childCount = 0;            ///< 子对象数量快照。
    int row = 0;                   ///< 自己在父节点 children 中的行号；根节点为 0。
    ObjectNode* parent = nullptr;  ///< 父节点；根节点为 nullptr。
    QVector<ObjectNode> children;  ///< 递归子节点。
};

/**
 * @brief 对象树只读模型。
 * @details 目标对象（setRootObject 传入，通常是主窗口）作为唯一顶层行出现，
 *          其子节点通过 QObject::children() 递归展开。
 */
class ObjectTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    /**
     * @brief 构造模型。
     * @param[in] parent Qt 父对象，可为 nullptr。
     */
    explicit ObjectTreeModel(QObject* parent = nullptr);

    ~ObjectTreeModel() override;

    /**
     * @brief 设置目标对象并立即重建快照。
     * @param[in] root 目标对象；为 nullptr 时清空模型。
     * @note 调用方保留对象所有权；模型只保存 QPointer。
     */
    void setRootObject(QObject* root);

    /**
     * @brief 返回目标对象。
     * @return 目标对象；对象已销毁或未设置时返回 nullptr。
     */
    QObject* rootObject() const;

    /**
     * @brief 设置搜索文本。
     * @param[in] text 搜索文本。非空时，对象名包含该文本（不区分大小写）的整行
     *                 以淡蓝色背景高亮；空串清除高亮。
     * @note 只发送 BackgroundRole 的 dataChanged，不重建快照，因此不会丢失
     *       展开状态。
     */
    void setSearchText(const QString& text);

    /**
     * @brief 返回当前搜索文本。
     * @return 搜索文本；空串表示未搜索。
     */
    QString searchText() const;

    /**
     * @brief 重建快照。
     * @details 先 beginResetModel()，递归读取 QObject::children()，再
     *          endResetModel()。可在对象树结构变化后由界面手动调用。
     */
    void refresh();

    //! QAbstractItemModel 只读实现。
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
    /**
     * @brief 递归构造快照节点。
     * @param[in] object 当前对象。
     * @param[in] row 在父节点 children 中的行号。
     * @return 该对象及其后代的快照节点。
     * @note 仅由 refresh() 在 GUI 线程调用。
     */
    ObjectNode buildNode(QObject* object, int row) const;

    /**
     * @brief 回填快照的 parent/row 指针。
     * @param[in,out] node 待处理的子树根。
     * @note 快照构建完成后调用；此时容器不再扩容，地址稳定。
     */
    static void fixParents(ObjectNode& node);

    /**
     * @brief 计算展示名。
     * @param[in] object 目标对象。
     * @return objectName 非空则用它，否则为 <unnamed>。
     */
    static QString displayName(const QObject* object);

    /**
     * @brief 计算对象地址文本。
     * @param[in] object 目标对象。
     * @return 固定 16 位十六进制的地址文本。
     */
    static QString addressText(const QObject* object);

    /**
     * @brief 判断当前搜索文本是否命中该节点。
     * @param[in] node 待检查节点。
     * @return true 表示命中（按 objectName 子串、不区分大小写）。
     */
    bool matchesSearch(const ObjectNode& node) const;

    /**
     * @brief 递归通知视图刷新 BackgroundRole。
     * @param[in] parent 子树根索引；默认从顶层开始。
     * @note 搜索文本变化时调用。
     */
    void notifyBackgroundChanged(const QModelIndex& parent = QModelIndex());

    /**
     * @brief 是否持有可用的目标对象。
     * @return true 表示已设置目标且对象仍存活。
     */
    bool hasRoot() const;

    QPointer<QObject> m_root;      ///< 目标对象，不拥有所有权。
    ObjectNode m_rootNode;         ///< 快照根节点；作为唯一顶层行。
    bool m_hasRoot = false;        ///< 是否曾设置过目标对象。
    QString m_searchText;          ///< 当前搜索文本；空串表示不高亮任何行。

    static constexpr int kColumnCount = 4; ///< 固定 4 列：对象 / 类型 / 子对象数 / 地址。
};
