// =============================================================================
// 文件：demo/phase2/ObjectTreeModel.cpp
// 项目：QtMemoryLeakTool / QtMemTracker
// 阶段：阶段二 —— Qt 对象树监控界面
// 用途：只读对象树模型的实现。refresh() 在 GUI 线程递归读取 QObject::children()
//       并构建值语义快照；视图通过标准 QAbstractItemModel 接口访问快照。
// 线程安全：无锁；所有方法必须在 GUI 线程调用（与 QWidget/QTreeView 一致）。
// 资源释放：模型不拥有 QObject，也不持有裸指针所有权；快照随模型析构释放。
// =============================================================================
#include "ObjectTreeModel.h"

#include <QColor>
#include <QLatin1Char>
#include <QStringLiteral>
#include <QtGlobal>

namespace {

// 列的展示名，顺序与 data() 的分支一致。
const char* const kColumnTitles[] = {"对象", "类型", "子对象数", "地址"};

} // namespace

ObjectTreeModel::ObjectTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

ObjectTreeModel::~ObjectTreeModel() = default;

void ObjectTreeModel::setRootObject(QObject* root)
{
    // 只保存 QPointer：目标对象销毁后 hasRoot() 自动为 false。
    m_root = root;
    m_hasRoot = (root != nullptr);
    refresh();
}

QObject* ObjectTreeModel::rootObject() const
{
    return m_root.data();
}

void ObjectTreeModel::setSearchText(const QString& text)
{
    if (m_searchText == text)
    {
        return;
    }
    m_searchText = text;
    // 只刷新背景角色，不重建快照，展开状态与选中项保持不变。
    notifyBackgroundChanged();
}

QString ObjectTreeModel::searchText() const
{
    return m_searchText;
}

void ObjectTreeModel::refresh()
{
    // 重置期间视图不持有旧索引；重建完成后一次性通知新快照。
    beginResetModel();
    if (hasRoot())
    {
        m_rootNode = buildNode(m_root.data(), 0);
    }
    else
    {
        m_rootNode = ObjectNode();
    }
    // 容器已不再变化，此时回填父指针与行号，地址保持稳定。
    fixParents(m_rootNode);
    endResetModel();
}

QModelIndex ObjectTreeModel::index(int row, int column,
                                   const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
    {
        return QModelIndex();
    }

    if (!parent.isValid())
    {
        // 目标对象是唯一顶层行；未设置目标时模型为空。
        return hasRoot()
            ? createIndex(row, column, const_cast<ObjectNode*>(&m_rootNode))
            : QModelIndex();
    }

    const ObjectNode* parentNode =
        static_cast<const ObjectNode*>(parent.internalPointer());
    if (parentNode == nullptr)
    {
        return QModelIndex();
    }

    // internalPointer 指向快照中的节点；refresh() 后容器才可能变化。
    return createIndex(row, column,
                       const_cast<ObjectNode*>(&parentNode->children.at(row)));
}

QModelIndex ObjectTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
    {
        return QModelIndex();
    }

    const ObjectNode* node = static_cast<const ObjectNode*>(child.internalPointer());
    if (node == nullptr || node == &m_rootNode || node->parent == nullptr)
    {
        return QModelIndex();
    }

    const ObjectNode* parentNode = node->parent;
    return createIndex(parentNode->row, 0,
                       const_cast<ObjectNode*>(parentNode));
}

int ObjectTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
    {
        return 0;
    }

    if (!parent.isValid())
    {
        return hasRoot() ? 1 : 0;
    }

    const ObjectNode* node = static_cast<const ObjectNode*>(parent.internalPointer());
    return node != nullptr ? node->children.size() : 0;
}

int ObjectTreeModel::columnCount(const QModelIndex& parent) const
{
    // 树是统一列结构，子索引的列数同样为 4。
    return parent.column() > 0 ? 0 : kColumnCount;
}

QVariant ObjectTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    const ObjectNode* node = static_cast<const ObjectNode*>(index.internalPointer());
    if (node == nullptr)
    {
        return QVariant();
    }

    // 搜索命中时整行（4 列）使用淡蓝色背景；未命中不返回背景，
    // 由视图继续使用默认交替行颜色。
    if (role == Qt::BackgroundRole)
    {
        if (matchesSearch(*node))
        {
            return QColor(204, 229, 255);
        }
        return QVariant();
    }

    if (role != Qt::DisplayRole)
    {
        return QVariant();
    }

    switch (index.column())
    {
    case 0:
        return node->objectName;
    case 1:
        return node->className;
    case 2:
        return node->childCount;
    case 3:
        return node->address;
    default:
        return QVariant();
    }
}

QVariant ObjectTreeModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QVariant();
    }
    if (section < 0 || section >= kColumnCount)
    {
        return QVariant();
    }
    // 列名固定且为 UTF-8 中文，使用 QString::fromUtf8 避免本地代码页影响。
    return QString::fromUtf8(kColumnTitles[section]);
}

Qt::ItemFlags ObjectTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }
    // 只读：不提供 ItemIsEditable，也不实现 setData。
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

ObjectNode ObjectTreeModel::buildNode(QObject* object, int row) const
{
    ObjectNode node;
    node.object = object;
    node.className = QString::fromLatin1(object->metaObject()->className());
    node.objectName = displayName(object);
    node.address = addressText(object);
    node.row = row;

    // 先复制 children 列表再递归：构建期间不修改被测对象。
    const QObjectList children = object->children();
    node.childCount = children.size();
    node.children.reserve(children.size());
    for (int i = 0; i < children.size(); ++i)
    {
        node.children.append(buildNode(children.at(i), i));
    }
    return node;
}

void ObjectTreeModel::fixParents(ObjectNode& node)
{
    for (int i = 0; i < node.children.size(); ++i)
    {
        node.children[i].parent = &node;
        node.children[i].row = i;
        fixParents(node.children[i]);
    }
}

QString ObjectTreeModel::displayName(const QObject* object)
{
    const QString name = object->objectName();
    return name.isEmpty() ? QStringLiteral("<unnamed>") : name;
}

QString ObjectTreeModel::addressText(const QObject* object)
{
    // 固定 16 位十六进制，与阶段一日志的地址格式保持一致。
    return QStringLiteral("0x%1").arg(
        reinterpret_cast<quintptr>(object), 16, 16, QLatin1Char('0'));
}

bool ObjectTreeModel::matchesSearch(const ObjectNode& node) const
{
    // 子串匹配且不区分大小写；空搜索文本视为不匹配任何节点。
    return !m_searchText.isEmpty()
        && node.objectName.contains(m_searchText, Qt::CaseInsensitive);
}

void ObjectTreeModel::notifyBackgroundChanged(const QModelIndex& parent)
{
    const int rows = rowCount(parent);
    for (int row = 0; row < rows; ++row)
    {
        const QModelIndex left = index(row, 0, parent);
        const QModelIndex right = index(row, kColumnCount - 1, parent);
        if (!left.isValid() || !right.isValid())
        {
            continue;
        }
        emit dataChanged(left, right, QVector<int>{Qt::BackgroundRole});
        notifyBackgroundChanged(left);
    }
}

bool ObjectTreeModel::hasRoot() const
{
    return m_hasRoot && !m_root.isNull();
}
