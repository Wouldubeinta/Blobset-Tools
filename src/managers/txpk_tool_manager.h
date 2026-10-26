#ifndef TXPK_TOOL_MANAGER_H
#define TXPK_TOOL_MANAGER_H

#include "x360_texture.h"
#include "creator_support.h"
#include "gcm_texture.h"
#include "tree_model.h"
#include "xml_models.h"

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
#include <functional>
#include <memory>

class TxpkToolManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // --- Loaded TXPK -------------------------------------------------------------------------------------
    Q_PROPERTY(QStringList fileList READ fileList NOTIFY fileListChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY fileListChanged)
    Q_PROPERTY(int entryCount READ entryCount NOTIFY fileListChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    // --- Folder tree + file list (same model shape MainForm.qml uses) ------------------------------------
    Q_PROPERTY(TreeModel* treeModel READ treeModel CONSTANT)
    Q_PROPERTY(QStringList currentFolderFiles READ currentFolderFiles NOTIFY currentFolderFilesChanged)
    Q_PROPERTY(int maxFileItemWidth READ maxFileItemWidth NOTIFY currentFolderFilesChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY treeRebuilt)

    // Case-insensitive "contains" filter on the texture path. Rebuilds the folder tree.
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

    // --- Current selection -------------------------------------------------------------------------------
    Q_PROPERTY(int currentFileIndex READ currentFileIndex NOTIFY currentFileChanged) // Index inside currentFolderFiles.
    Q_PROPERTY(int currentEntry READ currentEntry NOTIFY currentFileChanged)         // Index inside the whole TXPK.
    Q_PROPERTY(QString previewUrl READ previewUrl NOTIFY currentFileChanged)         // image://dds_provider/mem-...
    Q_PROPERTY(QString infoLine READ infoLine NOTIFY currentFileChanged)

    // --- Preview view options (same as DDSViewer) --------------------------------------------------------
    Q_PROPERTY(bool flipped READ flipped NOTIFY viewChanged)
    Q_PROPERTY(bool alphaEnabled READ alphaEnabled NOTIFY viewChanged)

    // --- Status bar --------------------------------------------------------------------------------------
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)   // 0..100
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

public:
    explicit TxpkToolManager(QObject* parent = nullptr);
    ~TxpkToolManager() override;

    QStringList fileList() const { return m_fileList; }
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
    QString previewUrl() const { return m_previewUrl; }
    QString infoLine() const { return m_infoLine; }

    bool flipped() const { return m_flipped; }
    bool alphaEnabled() const { return m_alphaEnabled; }

    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }
    int progress() const { return m_progress; }
    QString appVersion() const;

    // Opens a standalone TXPK file (main section immediately followed by the vram section).
    Q_INVOKABLE void loadTxpk(const QString& txpkPath, bool isBigEndian);

    // Pulls a TXPK out of the loaded blobset (by path hash), decompresses it in memory and indexes it.
    Q_INVOKABLE void loadTxpkFromBlobset(const QString& hexHash);

    // --- Browsing (called by the folder tree / file list in TXPKViewer.qml) -------------------------------

    // Index of the top folder of the tree (invalid when nothing is shown).
    Q_INVOKABLE QModelIndex rootFolderIndex() const;

    // Fills currentFolderFiles with the textures stored directly in that folder.
    Q_INVOKABLE void selectFolder(const QModelIndex& index);

    // Selects the file `fileIndex` (index inside currentFolderFiles) and loads its preview + info. -1 clears.
    Q_INVOKABLE void selectFile(int fileIndex);

    // Arrow key navigation of the multi column file list (same rules as MainForm.qml). Returns true when the
    // key was an arrow key that this list handles.
    Q_INVOKABLE bool handleFileKey(int qtKey, int rowsPerColumn);

    // Decodes entry `index` into memory and returns the image://dds_provider URL that shows it with the
    // current view options. Nothing is written to disk. Empty (and an error is reported) when it can't be decoded.
    Q_INVOKABLE QString previewImageUrl(int index);

    // { path, width, height, mipMaps, format, size } for the info panel. Empty map for a bad index.
    Q_INVOKABLE QVariantMap entryInfo(int index) const;

    // --- View options ------------------------------------------------------------------------------------
    Q_INVOKABLE void flipImage();               // Vertical flip, on/off.
    Q_INVOKABLE void toggleAlpha(bool enable);  // Off = the preview is shown fully opaque.

    // --- Extraction (format: "dds" or "png") ---------------------------------------------------------------
    //   dds: the texture as a standard PC .dds (console data converted).
    //   png: the decoded texture with the current flip / alpha settings applied.
    // Folder structure inside the TXPK is kept.
    Q_INVOKABLE void extractAll(const QString& outDirPath, const QString& format = QStringLiteral("dds"));
    Q_INVOKABLE void extractSelected(const QString& outDirPath, const QString& format = QStringLiteral("dds"));
    Q_INVOKABLE void extractOne(int index, const QString& outDirPath, const QString& format = QStringLiteral("dds"));

    // --- TXPK Creator (TXPKCreator.qml) ------------------------------------------------------------------
    // Extract All (DDS) also writes TXPK_List.xml into the output folder: the texture order, the paths stored in
    // the TXPK, the layout and the blobset entry it came from. The creator rebuilds the TXPK from that list and
    // the (edited) .dds files, and writes <name>.xml next to it so Modify / Create can put it back in the game.

    // See CreatorSupport::inspectList(). sourceDir empty = the list's folder.
    Q_INVOKABLE QVariantMap inspectCreatorList(const QString& xmlPath, const QString& sourceDir = QString()) const;

    // The TXPK sections built from a creator list (also used by the BSB Creator, whose BSBs carry a TXPK).
    struct BuiltTxpk {
        QByteArray mainPart;
        QByteArray vram;
        QVector<ModifyFileEntry> entries;   // For the ModifyFileInfo xml.
        int platform = 0;
    };
    // Throws std::runtime_error with a readable message; false when cancelled. progress covers 0..80 %.
    static bool buildTxpk(const XmlExtractFileInfo& list, const QString& sourceDir, int fallbackPlatform,
                          BuiltTxpk& result, const std::function<void(int, const QString&)>& progress,
                          const std::function<void(const QString&, bool)>& log, const std::atomic<bool>* cancel);

    // Threaded. Reports through status / progress, creatorLog() and processComplete().
    Q_INVOKABLE void createTxpk(const QString& xmlPath, const QString& sourceDir, const QString& outTxpkPath);

signals:
    void fileListChanged();
    void busyChanged();
    void processComplete(bool success, const QString& message);
    void creatorLog(const QString& message, bool ok);   // One line for the creator's log.

    void treeRebuilt();                 // The folder tree was rebuilt (load or filter change).
    void currentFolderFilesChanged();
    void currentFileChanged();
    void filterTextChanged();
    void viewChanged();
    void statusChanged();
    void progressChanged();

protected:
    // Protected so the BSB viewer (BsbToolManager) can reuse the TXPK browsing / preview / extraction.
    struct TxpkEntry {
        qint64 dataOffset = 0;      // Absolute offset inside m_data.
        quint32 dataSize = 0;
        quint32 width = 0;
        quint32 height = 0;
        Gcm::CellGcmTexture gcm;    // PS3 only: RSX texture descriptor.
        bool isXbox360 = false;     // Xbox 360: the D3D texture header below instead of `gcm`.
        X360::TextureHeader x360;
        quint8 mipMaps = 0;         // Console only.
        QString ddsPath;            // '/' separated, always has an extension.
        QString storedPath;         // The path exactly as the TXPK stores it (written to TXPK_List.xml).
        quint32 imageType = 1;
        qint64 recordOffset = 0;    // Info record inside the main section.
        qint64 recordSize = 0;
        qint64 rawDataOffset = 0;   // As stored: relative to the vram section.
    };

    // Where the loaded TXPK came from (for TXPK_List.xml) and how it is laid out.
    CreatorSupport::SourceInfo m_source;
    quint32 m_recordAlignment = 0;
    quint32 m_dataAlignment = 0;

    QString m_sourceName;
    bool m_isBigEndian = false;
    int m_platform = 0;             // CoreDefs::Platforms
    QByteArray m_data;              // Whole TXPK: main section followed by the vram section.
    QList<TxpkEntry> m_entries;
    QStringList m_fileList;

    // Folder tree / file list state.
    TreeModel* m_treeModel = nullptr;
    QString m_rootName;                     // Name of the single top folder of the tree.
    QString m_filterText;                   // Trimmed, as typed.
    QHash<QString, int> m_pathToEntry;      // "<root>/<folders>/<file>" -> index in m_entries (visible entries only).
    int m_visibleCount = 0;
    QStringList m_currentFolderFiles;       // File names of the selected folder.
    QList<int> m_folderEntries;             // Parallel to m_currentFolderFiles: index in m_entries.
    int m_maxFileItemWidth = 180;
    int m_currentFileIndex = -1;
    int m_currentEntry = -1;
    QString m_previewUrl;
    QString m_infoLine;

    // Preview: the decoded texture lives in the image provider's memory store under m_previewKey.
    QString m_previewKey;
    int m_previewEntry = -1;
    bool m_flipped = false;
    bool m_alphaEnabled = true;

    // Status bar state.
    QString m_statusText;
    bool m_statusOk = true;
    int m_progress = 0;

    bool m_busy = false;
    std::atomic<bool> m_cancel{false};
    QFuture<void> m_extractFuture;


    bool loadFromBytes(const QByteArray& data, qint64 mainSize, bool isBigEndian, const QString& sourceName);
    QByteArray ddsBytesFor(int index) const;     // A complete, standard .dds file.
    QString outputPath(int index, bool asPng) const;
    // Returns an error text, empty on success. Safe to call from the extraction thread.
    QString writeEntryTo(int index, const QString& outDir, bool asPng, bool flipped, bool alphaEnabled) const;
    // Writes TXPK_List.xml for the TXPK Creator into outDir. Error text, empty on success.
    QString writeCreatorList(const QString& outDir) const;
    void clearEntries();
    void releasePreview();
    void refreshPreview();
    void setBusy(bool busy);

    void rebuildTree();                          // Applies the filter and refills the folder tree.
    void resetBrowser();                         // Empties tree, file list and selection.
    void setStatus(bool ok, const QString& message);
    void finish(bool ok, const QString& message); // setStatus() + processComplete().
    void setProgress(int percent);
    void reportProgress(int percent, const QString& message);
    static QString buildInfoLine(const QVariantMap& info);

};

#endif // TXPK_TOOL_MANAGER_H
