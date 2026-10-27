#ifndef M3MP_TOOL_MANAGER_H
#define M3MP_TOOL_MANAGER_H

#include "creator_support.h"
#include "tree_model.h"

#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>

// ==========================================
// QML Bridge Manager
// ==========================================
// M3MPViewer.qml: same layout and behaviour as the TXPK viewer (folder tree | file list | preview, filter, status
// bar, threaded extraction). M3MPCreator.qml: createM3mp().
//
// An M3MP is a header (file table + path table + chunk table) followed by chunks (LZMA on V1/V2, ZSTD on V4,
// or stored). The decompressed chunks, one after the other, hold every file at its table offset.
class M3mpToolManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // --- Loaded M3MP -------------------------------------------------------------------------------------
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY fileListChanged)
    Q_PROPERTY(int entryCount READ entryCount NOTIFY fileListChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    // --- Folder tree + file list -------------------------------------------------------------------------
    Q_PROPERTY(TreeModel* treeModel READ treeModel CONSTANT)
    Q_PROPERTY(QStringList currentFolderFiles READ currentFolderFiles NOTIFY currentFolderFilesChanged)
    Q_PROPERTY(int maxFileItemWidth READ maxFileItemWidth NOTIFY currentFolderFilesChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY treeRebuilt)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

    // --- Current selection -------------------------------------------------------------------------------
    Q_PROPERTY(int currentFileIndex READ currentFileIndex NOTIFY currentFileChanged)
    Q_PROPERTY(int currentEntry READ currentEntry NOTIFY currentFileChanged)
    Q_PROPERTY(QString previewKind READ previewKind NOTIFY currentFileChanged)   // "", "image", "text", "hex"
    Q_PROPERTY(QString previewUrl READ previewUrl NOTIFY currentFileChanged)     // previewKind == "image"
    Q_PROPERTY(QString previewText READ previewText NOTIFY currentFileChanged)   // previewKind == "text" / "hex"
    Q_PROPERTY(QString infoLine READ infoLine NOTIFY currentFileChanged)

    // --- Image preview options (same as the DDS / TXPK viewers) ------------------------------------------
    Q_PROPERTY(bool flipped READ flipped NOTIFY viewChanged)
    Q_PROPERTY(bool alphaEnabled READ alphaEnabled NOTIFY viewChanged)

    // --- Status bar --------------------------------------------------------------------------------------
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

    // --- Edit (M3MPViewer.qml): replace files, then Save Changes rebuilds the M3MP into the mods folder -------
    Q_PROPERTY(int replacedCount READ replacedCount NOTIFY changesChanged)
    Q_PROPERTY(bool unsavedChanges READ unsavedChanges NOTIFY changesChanged)
    Q_PROPERTY(QString modsTarget READ modsTarget NOTIFY changesChanged)

public:
    explicit M3mpToolManager(QObject* parent = nullptr);
    ~M3mpToolManager() override;

    QString sourceName() const { return m_sourceName; }
    int entryCount() const { return static_cast<int>(m_entries.size()); }
    bool busy() const { return m_busy; }

    TreeModel* treeModel() const { return m_treeModel; }
    QStringList currentFolderFiles() const { return m_currentFolderFiles; }
    int maxFileItemWidth() const { return m_maxFileItemWidth; }
    int visibleCount() const { return m_visibleCount; }

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString& text);

    int currentFileIndex() const { return m_currentFileIndex; }
    int currentEntry() const { return m_currentEntry; }
    QString previewKind() const { return m_previewKind; }
    QString previewUrl() const { return m_previewUrl; }
    QString previewText() const { return m_previewText; }
    QString infoLine() const { return m_infoLine; }

    bool flipped() const { return m_flipped; }
    bool alphaEnabled() const { return m_alphaEnabled; }

    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }
    int progress() const { return m_progress; }
    QString appVersion() const;

    int replacedCount() const { return static_cast<int>(m_replacements.size()); }
    bool unsavedChanges() const { return m_unsavedChanges; }
    QString modsTarget() const;
    // fileIndex: inside currentFolderFiles. The replacement needs the same extension.
    Q_INVOKABLE bool replaceFileAt(int fileIndex, const QString& pathOrUrl);
    // Dropped files by name: the selected folder first, then a name that's unique in the M3MP. Returns how many.
    Q_INVOKABLE int replaceByName(const QStringList& pathsOrUrls);
    Q_INVOKABLE bool isReplacedAt(int fileIndex) const;
    Q_INVOKABLE void revertAt(int fileIndex);
    Q_INVOKABLE void discardChanges();
    // Threaded: extracts the M3MP, puts the replacements in and rebuilds it (M3MP Creator) as
    // games\<game>\<platform>\mods\<the M3MP's path> + its .xml.
    Q_INVOKABLE void saveChanges();

    // Opens a standalone .m3mp file from disk.
    Q_INVOKABLE void loadM3mp(const QString& m3mpPath, bool isBigEndian);

    // Pulls an M3MP out of the loaded blobset (by path hash) and indexes it.
    Q_INVOKABLE void loadM3mpFromBlobset(const QString& hexHash);

    // --- Browsing (same as TxpkToolManager) --------------------------------------------------------------
    Q_INVOKABLE QModelIndex rootFolderIndex() const;
    Q_INVOKABLE void selectFolder(const QModelIndex& index);
    Q_INVOKABLE void selectFile(int fileIndex);
    Q_INVOKABLE bool handleFileKey(int qtKey, int rowsPerColumn);

    // --- View options (images) ---------------------------------------------------------------------------
    Q_INVOKABLE void flipImage();
    Q_INVOKABLE void toggleAlpha(bool enable);

    // --- Extraction: the files exactly as stored, folder structure kept ------------------------------------
    Q_INVOKABLE void extractAll(const QString& outDirPath);
    Q_INVOKABLE void extractSelected(const QString& outDirPath);

    // --- M3MP Creator (M3MPCreator.qml) --------------------------------------------------------------------
    // Extract All also writes M3MP_List.xml into the output folder: the file order, the paths stored in the M3MP
    // and the blobset entry it came from. The creator rebuilds the M3MP from that list and the (edited) files,
    // compressed the way the game's blobset version expects, and writes <name>.xml next to it for Modify / Create.

    // See CreatorSupport::inspectList(). sourceDir empty = the list's folder.
    Q_INVOKABLE QVariantMap inspectCreatorList(const QString& xmlPath, const QString& sourceDir = QString()) const;

    // Threaded. Reports through status / progress, creatorLog() and processComplete().
    Q_INVOKABLE void createM3mp(const QString& xmlPath, const QString& sourceDir, const QString& outM3mpPath);

signals:
    void fileListChanged();
    void busyChanged();
    void treeRebuilt();
    void currentFolderFilesChanged();
    void currentFileChanged();
    void filterTextChanged();
    void viewChanged();
    void statusChanged();
    void progressChanged();
    void changesChanged();
    void changesSaved(const QString& target);
    void processComplete(bool success, const QString& message);

    void creatorLog(const QString& message, bool ok);   // One line for the creator's log.

private:
    struct Entry {
        qint64 offset = 0;      // Inside m_payload.
        qint64 size = 0;
        bool valid = false;     // offset + size lies inside m_payload.
        QString path;           // '/' separated.
        QString storedPath;     // Exactly as the M3MP stores it (written to M3MP_List.xml).
    };

    CreatorSupport::SourceInfo m_source;   // Where the loaded M3MP came from (for M3MP_List.xml).

    // Edit session: entry index -> replacement file.
    QHash<int, QString> m_replacements;
    bool m_unsavedChanges = false;
    QString m_workspacePath;        // The M3MP's path in the blobset (opened from the game), else empty.
    QString m_editTemp;
    bool m_savingChanges = false;
    int entryForFile(int fileIndex) const { return fileIndex >= 0 && fileIndex < m_folderEntries.size() ? m_folderEntries[fileIndex] : -1; }
    bool replaceEntry(int entry, const QString& file);
    QByteArray previewBytes(int index) const;   // The replacement when there is one, else the M3MP's own bytes.

    QString m_sourceName;
    QByteArray m_payload;       // All chunks decompressed, one after the other.
    QList<Entry> m_entries;

    TreeModel* m_treeModel = nullptr;
    QString m_rootName;
    QString m_filterText;
    QHash<QString, int> m_pathToEntry;
    int m_visibleCount = 0;
    QStringList m_currentFolderFiles;
    QList<int> m_folderEntries;
    int m_maxFileItemWidth = 180;
    int m_currentFileIndex = -1;
    int m_currentEntry = -1;

    QString m_previewKind;
    QString m_previewUrl;
    QString m_previewText;
    QString m_infoLine;
    QString m_previewKey;       // DdsImageProvider memory key of the decoded image.
    int m_previewEntry = -1;
    bool m_flipped = false;
    bool m_alphaEnabled = true;

    QString m_statusText;
    bool m_statusOk = true;
    int m_progress = 0;

    bool m_busy = false;
    std::atomic<bool> m_cancel{false};
    QFuture<void> m_extractFuture;

    bool loadFromBytes(const QByteArray& data, bool isBigEndian, int version, const QString& sourceName);
    QByteArray entryBytes(int index) const;
    QString writeEntryTo(int index, const QString& outDir) const;   // Error text, empty on success. Thread safe.
    QString writeCreatorList(const QString& outDir) const;          // M3MP_List.xml. Error text, empty on success.
    void reportProgress(int percent, const QString& message);
    void loadPreview(int index);
    void clearPreview();
    void clearEntries();
    void resetBrowser();
    void rebuildTree();
    void setBusy(bool busy);
    void setStatus(bool ok, const QString& message);
    void finish(bool ok, const QString& message);
    void setProgress(int percent);
};

#endif // M3MP_TOOL_MANAGER_H
