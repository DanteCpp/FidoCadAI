#pragma once

#include "fidocad.h"

#include <QByteArray>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

/* The one drawing model: an FcDrawing plus undo. Mouse tools and chat tools
 * both edit through here, so the diff reported to the agent covers both. */
class Document {
public:
    Document();
    ~Document();
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

    /* libraries, in fc_lib_load_all order; on failure the previous set stays */
    bool loadLibraries(bool stdlib, const QStringList &libs, const QStringList &libdirs,
                       QString *err = nullptr, QStringList *warnings = nullptr);
    bool reloadLibraries(QString *err = nullptr, QStringList *warnings = nullptr);
    const FcLib *lib() const { return lib_; }

    /* file */
    bool open(const QString &path, QString *err); /* missing file = empty drawing */
    bool openText(const QByteArray &fcd);          /* e.g. from stdin */
    bool save(QString *err);
    bool saveAs(const QString &path, QString *err);
    bool reloadFromDisk(QString *err); /* also resets the diff baseline */
    QString path() const { return path_; }
    bool modified() const { return fcd() != saved_; }
    QByteArray savedFcd() const { return saved_; }
    bool changedSinceOpen() const { return fcd() != original_; }
    void setBaseline(); /* what fc_diff compares against */

    /* read */
    const FcDrawing &drawing() const { return d_; }
    int count() const { return d_.nprims; }
    QByteArray fcd() const;
    QString primFcd(int i) const;
    QString listing() const; /* numbered primitives, for chat tools */
    QString context() const;
    QString diff(bool json) const;
    QString diffJson() const { return diff(true); }
    QRectF boundsAll() const;
    QRectF boundsOf(const QList<int> &idx) const;
    int hit(double x, double y, double tol) const;
    QList<int> inRect(const QRectF &r) const;

    /* edit: each call is one undo step and fires onChanged */
    void setFcd(const QByteArray &fcd);
    QList<int> append(const QString &fcd); /* returns new indices */
    void remove(QList<int> idx);
    void translate(const QList<int> &idx, double dx, double dy);
    void rotate(const QList<int> &idx);  /* 90 deg clockwise about the selection centre */
    void mirror(const QList<int> &idx);  /* flip horizontally about the selection centre */
    bool replace(int i, const QString &fcd);
    bool setText(int i, int which, const QString &text);
    /* FidoCadJS-style control points (handles) for resize/edit dragging. */
    int handles(int i) const;
    QPointF handle(int i, int h) const;
    void setHandle(int i, int h, double x, double y);
    void setLayer(const QList<int> &idx, int layer);
    QList<int> duplicate(const QList<int> &idx, double dx, double dy);

    /* Group several low-level changes (e.g. a drag) into one undo step. */
    void beginChange();
    void endChange();
    void translateLive(const QList<int> &idx, double dx, double dy); /* inside begin/end */

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void undo();
    void redo();

    void setGrid(int g) { grid_ = g > 0 ? g : 1; }
    QPointF pivot(const QList<int> &idx) const; /* selection centre, snapped to the grid */

    std::function<void()> onChanged;

private:
    void checkpoint();
    void changed();
    void reparse(const QByteArray &fcd);

    FcDrawing d_;
    FcLib *lib_ = nullptr;
    QString path_;
    QByteArray original_, saved_;
    std::vector<QByteArray> undo_, redo_;
    int batch_ = 0;
    int grid_ = 5;
    bool stdlib_ = true;
    QStringList libs_, libdirs_;
};
