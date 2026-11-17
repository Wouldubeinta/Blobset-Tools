#ifndef TREE_MODEL_H
#define TREE_MODEL_H

#include <QAbstractItemModel>
#include <QList>
#include <vector>
#include <QVariant>
#include <memory>
#include <QtQml/qqmlregistration.h>

// One folder or file of the tree. The model only shows folders (the file lists are separate views), so every node
// keeps its sub folders in their own list and knows its row among them: index() / parent() / rowCount() are O(1)
// instead of a walk over every child (folders with thousands of files made the tree view crawl).
struct TreeNode {
    QString name;
    bool isFolder = false;
    int folderRow = -1;                        // Row among the parent's folders (folders only).
    TreeNode* parent = nullptr;
    std::vector<std::unique_ptr<TreeNode>> children;   // Folders and files, in the order they were added.
    std::vector<TreeNode*> folders;                    // The folders of `children`.
};

class TreeModel : public QAbstractItemModel {
    Q_OBJECT
    QML_ELEMENT

public:
    enum TreeRoles { NameRole = Qt::UserRole + 1, IsFolderRole };

    explicit TreeModel(QObject *parent = nullptr);

    // QAbstractItemModel Overrides for QML consumption
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex & = QModelIndex()) const override { return 1; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replaces the tree with the folders / files of `filePaths` ('/' or '\\' separated). With a `rootName` (the
    // blobset's file name, "data-0.blobset.pc") everything goes in one top folder of that name, so the files that
    // aren't in a folder have one to be listed in. It's only shown: paths (getPathFromIndex(), indexFromPath(),
    // addFile()) never include it, and its own path is "".
    void buildTree(const QList<QString>& filePaths, const QString& rootName = QString());

    // Adds one file (and any folders it needs) without resetting the tree. False when it's already there.
    bool addFile(const QString& filePath);

    Q_INVOKABLE void clear();
    Q_INVOKABLE QString getPathFromIndex(const QModelIndex &index) const;
    Q_INVOKABLE QModelIndex indexFromPath(const QString& path) const;
    // The folder whose path is "": the named top folder when there is one, otherwise the hidden root.
    TreeNode* rootNode() const { return m_topNode ? m_topNode : m_rootNode.get(); }

private:
    QModelIndex topIndex() const;

    std::unique_ptr<TreeNode> m_rootNode;
    TreeNode* m_topNode = nullptr;   // buildTree()'s rootName folder (a child of m_rootNode), or none.
};

#endif // TREE_MODEL_H
