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

    // Replaces the tree with the folders / files of `filePaths` ('/' or '\\' separated).
    void buildTree(const QList<QString>& filePaths);

    Q_INVOKABLE void clear();
    Q_INVOKABLE QString getPathFromIndex(const QModelIndex &index) const;
    Q_INVOKABLE QModelIndex indexFromPath(const QString& path) const;
    TreeNode* rootNode() const { return m_rootNode.get(); }

private:
    std::unique_ptr<TreeNode> m_rootNode;
};

#endif // TREE_MODEL_H
