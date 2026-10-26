#include "tree_model.h"

#include <algorithm>

using namespace Qt::StringLiterals;

TreeModel::TreeModel(QObject *parent) : QAbstractItemModel(parent) {
    m_rootNode = std::make_unique<TreeNode>();
    m_rootNode->name = u"Root"_s;
    m_rootNode->isFolder = true;
}

QModelIndex TreeModel::index(int row, int column, const QModelIndex &parent) const {
    if (!hasIndex(row, column, parent)) return QModelIndex();
    const TreeNode *parentNode = parent.isValid() ? static_cast<TreeNode*>(parent.internalPointer()) : m_rootNode.get();
    return createIndex(row, column, parentNode->folders[size_t(row)]);
}

QModelIndex TreeModel::parent(const QModelIndex &child) const {
    if (!child.isValid()) return QModelIndex();
    const TreeNode *parentNode = static_cast<TreeNode*>(child.internalPointer())->parent;
    if (!parentNode || parentNode == m_rootNode.get()) return QModelIndex();
    return createIndex(parentNode->folderRow, 0, const_cast<TreeNode*>(parentNode));
}

int TreeModel::rowCount(const QModelIndex &parent) const {
    if (parent.column() > 0) return 0;
    const TreeNode *parentNode = parent.isValid() ? static_cast<TreeNode*>(parent.internalPointer()) : m_rootNode.get();
    return static_cast<int>(parentNode->folders.size());
}

QVariant TreeModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return QVariant();
    const TreeNode *node = static_cast<TreeNode*>(index.internalPointer());
    if (role == NameRole) return node->name;
    if (role == IsFolderRole) return node->isFolder;
    return QVariant();
}

QHash<int, QByteArray> TreeModel::roleNames() const {
    return { {NameRole, "fileName"}, {IsFolderRole, "isFolder"} };
}

void TreeModel::buildTree(const QList<QString>& filePaths) {
    beginResetModel();

    m_rootNode->children.clear();
    m_rootNode->folders.clear();

    // Folder path -> node, so every path only walks the folders it doesn't share with the paths before it.
    QHash<QString, TreeNode*> folderCache;
    folderCache.reserve(filePaths.size() / 8 + 16);
    folderCache.insert(QString(), m_rootNode.get());

    QString cleanPath;
    for (const QString& path : filePaths) {
        cleanPath = path;
        cleanPath.replace(u'\\', u'/');

        // The folder part is looked up in one go; only a new folder walks its components.
        const qsizetype slash = cleanPath.lastIndexOf(u'/');
        const QString folderPath = slash > 0 ? cleanPath.left(slash) : QString();
        TreeNode* folder = folderCache.value(folderPath, nullptr);
        if (!folder) {
            folder = m_rootNode.get();
            QString accumulated;
            for (const QString& part : folderPath.split(u'/', Qt::SkipEmptyParts)) {
                if (!accumulated.isEmpty()) accumulated += u'/';
                accumulated += part;
                if (TreeNode* known = folderCache.value(accumulated, nullptr)) {
                    folder = known;
                    continue;
                }
                auto node = std::make_unique<TreeNode>();
                node->name = part;
                node->isFolder = true;
                node->parent = folder;
                node->folderRow = static_cast<int>(folder->folders.size());
                TreeNode* raw = node.get();
                folder->folders.push_back(raw);
                folder->children.push_back(std::move(node));
                folderCache.insert(accumulated, raw);
                folder = raw;
            }
        }

        const QString fileName = cleanPath.mid(slash + 1);
        if (fileName.isEmpty())
            continue;
        auto file = std::make_unique<TreeNode>();
        file->name = fileName;
        file->parent = folder;
        folder->children.push_back(std::move(file));
    }

    endResetModel();
}

QString TreeModel::getPathFromIndex(const QModelIndex &index) const {
    if (!index.isValid()) return QString();

    // Folder names from this node up to (not including) the root.
    QStringList pathParts;
    for (const TreeNode *node = static_cast<TreeNode*>(index.internalPointer());
         node && node != m_rootNode.get(); node = node->parent)
        pathParts.prepend(node->name);
    return pathParts.join(u'/');
}

QModelIndex TreeModel::indexFromPath(const QString& path) const {
    const QStringList parts = QString(path).replace(u'\\', u'/').split(u'/', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return QModelIndex();

    const TreeNode* node = m_rootNode.get();
    QModelIndex result;
    for (const QString& part : parts) {
        const auto it = std::find_if(node->folders.cbegin(), node->folders.cend(),
                                     [&part](const TreeNode* folder) { return folder->name == part; });
        if (it == node->folders.cend()) return QModelIndex();
        node = *it;
        result = createIndex(node->folderRow, 0, const_cast<TreeNode*>(node));
    }
    return result;
}

void TreeModel::clear() {
    beginResetModel();
    m_rootNode->children.clear();
    m_rootNode->folders.clear();
    endResetModel();
}
