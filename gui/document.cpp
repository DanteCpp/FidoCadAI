#include "document.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

QString takeString(char *s) {
    QString out = s ? QString::fromUtf8(s) : QString();
    free(s);
    return out;
}

QRectF toRect(const FcRect &r) {
    return QRectF(QPointF(r.min_x, r.min_y), QPointF(r.max_x, r.max_y));
}

QList<int> sortedUnique(QList<int> idx, int n) {
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    idx.erase(std::remove_if(idx.begin(), idx.end(), [n](int i) { return i < 0 || i >= n; }),
              idx.end());
    return idx;
}

} // namespace

Document::Document() {
    fc_drawing_init(&d_);
    lib_ = fc_lib_new();
}

Document::~Document() {
    fc_drawing_free(&d_);
    fc_lib_free(lib_);
}

bool Document::loadLibraries(bool stdlib, const QStringList &libs, const QStringList &libdirs,
                             QString *err, QStringList *warnings) {
    stdlib_ = stdlib;
    libs_ = libs;
    libdirs_ = libdirs;
    return reloadLibraries(err, warnings);
}

bool Document::reloadLibraries(QString *err, QStringList *warnings) {
    struct Report {
        QStringList errors, warnings;
    } report;
    QList<QByteArray> dirs8, files8;
    std::vector<const char *> dirs, files;
    for (const QString &d : libdirs_) dirs8 << d.toUtf8();
    for (const QString &f : libs_) files8 << f.toUtf8();
    for (const QByteArray &d : dirs8) dirs.push_back(d.constData());
    for (const QByteArray &f : files8) files.push_back(f.constData());
    FcLib *next = fc_lib_new();
    if (!next) {
        if (err) *err = "out of memory loading libraries";
        return false;
    }
    int rc = fc_lib_load_all(next, stdlib_, dirs.data(), (int)dirs.size(), files.data(), nullptr,
                             (int)files.size(),
                             [](void *ctx, int error, const char *msg) {
                                 Report *r = static_cast<Report *>(ctx);
                                 (error ? r->errors : r->warnings) << QString::fromUtf8(msg);
                             },
                             &report);
    if (warnings) *warnings = report.warnings;
    if (rc != FC_OK) {
        fc_lib_free(next);
        if (err) *err = report.errors.join("; ");
        return false;
    }
    fc_lib_free(lib_);
    lib_ = next;
    return true;
}

bool Document::open(const QString &path, QString *err) {
    path_ = QFileInfo(path).absoluteFilePath();
    QFile f(path_);
    QByteArray text;
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly)) {
            if (err) *err = f.errorString();
            return false;
        }
        text = f.readAll();
    }
    reparse(text);
    original_ = saved_ = fcd();
    if (!f.exists()) saved_.clear(); /* unsaved new file */
    undo_.clear();
    redo_.clear();
    return true;
}

bool Document::openText(const QByteArray &text) {
    reparse(text);
    original_ = saved_ = fcd();
    undo_.clear();
    redo_.clear();
    return true;
}

bool Document::save(QString *err) {
    if (path_.isEmpty()) {
        if (err) *err = "no file name";
        return false;
    }
    QByteArray text = fcd();
    QFile f(path_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = f.errorString();
        return false;
    }
    if (f.write(text) != text.size()) {
        if (err) *err = f.errorString();
        return false;
    }
    saved_ = text;
    return true;
}

bool Document::saveAs(const QString &path, QString *err) {
    QString old = path_;
    path_ = QFileInfo(path).absoluteFilePath();
    if (save(err)) return true;
    path_ = old;
    return false;
}

bool Document::reloadFromDisk(QString *err) {
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    checkpoint();
    reparse(f.readAll());
    original_ = saved_ = fcd();
    changed();
    return true;
}

void Document::setBaseline() { original_ = fcd(); }

QByteArray Document::fcd() const {
    char *out = nullptr;
    FcEmitOpts o = {1, 1};
    size_t n = fc_emit(&d_, &out, &o);
    QByteArray b(out ? out : "", (qsizetype)n);
    free(out);
    return b;
}

QString Document::primFcd(int i) const {
    if (i < 0 || i >= d_.nprims) return QString();
    return takeString(fc_prim_fcd(&d_.prims[i])).trimmed();
}

QString Document::listing() const {
    QString out;
    for (int i = 0; i < d_.nprims; i++) {
        QString t = primFcd(i);
        if (t.isEmpty()) continue;
        out += QString("[%1] %2\n").arg(i).arg(t.replace("\n", "\n    "));
    }
    return out.isEmpty() ? "(empty drawing)\n" : out;
}

QString Document::context() const { return takeString(fc_context(&d_, lib_)); }

QString Document::diff(bool json) const {
    FcDrawing base;
    fc_drawing_init(&base);
    fc_parse(&base, original_.constData(), (size_t)original_.size());
    QString out = takeString(fc_diff(&base, &d_, json ? 1 : 0));
    fc_drawing_free(&base);
    return out;
}

QRectF Document::boundsAll() const {
    FcRect r;
    if (!fc_bounds(&d_, lib_, &r)) return QRectF();
    return toRect(r);
}

QRectF Document::boundsOf(const QList<int> &idx) const {
    std::vector<FcRect> box(d_.nprims);
    std::vector<int> has(d_.nprims);
    FcRect u = {0, 0, 0, 0};
    bool any = false;
    fc_prims_bounds(&d_, lib_, box.data(), has.data());
    for (int i : idx) {
        if (i < 0 || i >= d_.nprims || !has[i]) continue;
        const FcRect &r = box[i];
        if (!any) u = r;
        u.min_x = std::min(u.min_x, r.min_x);
        u.min_y = std::min(u.min_y, r.min_y);
        u.max_x = std::max(u.max_x, r.max_x);
        u.max_y = std::max(u.max_y, r.max_y);
        any = true;
    }
    return toRect(u);
}

int Document::hit(double x, double y, double tol) const { return fc_hit_test(&d_, lib_, x, y, tol); }

QList<int> Document::inRect(const QRectF &r) const {
    QList<int> out;
    QRectF n = r.normalized();
    std::vector<FcRect> box(d_.nprims);
    std::vector<int> has(d_.nprims);
    fc_prims_bounds(&d_, lib_, box.data(), has.data());
    for (int i = 0; i < d_.nprims; i++) {
        const FcRect &b = box[i];
        if (has[i] && b.min_x >= n.left() && b.max_x <= n.right() && b.min_y >= n.top() &&
            b.max_y <= n.bottom())
            out << i;
    }
    return out;
}

QPointF Document::pivot(const QList<int> &idx) const {
    QRectF b = boundsOf(idx);
    QPointF c = b.center();
    if (idx.size() == 1 && d_.prims[idx[0]].kind == FC_MACRO)
        c = QPointF(d_.prims[idx[0]].u.macro.p.x, d_.prims[idx[0]].u.macro.p.y);
    return QPointF(std::round(c.x() / grid_) * grid_, std::round(c.y() / grid_) * grid_);
}

/* ---- edits ---- */

void Document::reparse(const QByteArray &text) {
    fc_drawing_clear(&d_);
    fc_parse(&d_, text.constData(), (size_t)text.size());
}

void Document::checkpoint() {
    if (batch_ > 0) return;
    undo_.push_back(fcd());
    if (undo_.size() > 500) undo_.erase(undo_.begin());
    redo_.clear();
}

void Document::changed() {
    if (batch_ > 0) return;
    if (onChanged) onChanged();
}

void Document::beginChange() {
    checkpoint();
    batch_++;
}

void Document::endChange() {
    if (batch_ > 0) batch_--;
    if (batch_ == 0) {
        if (!undo_.empty() && undo_.back() == fcd()) undo_.pop_back(); /* nothing happened */
        changed();
    }
}

void Document::setFcd(const QByteArray &text) {
    checkpoint();
    reparse(text);
    changed();
}

QList<int> Document::append(const QString &text) {
    QList<int> out;
    QByteArray t = text.toUtf8();
    if (!t.endsWith('\n')) t += '\n';
    checkpoint();
    int n0 = d_.nprims;
    fc_append_fcd(&d_, t.constData(), (size_t)t.size());
    for (int i = n0; i < d_.nprims; i++) out << i;
    changed();
    return out;
}

void Document::remove(QList<int> idx) {
    idx = sortedUnique(idx, d_.nprims);
    if (idx.isEmpty()) return;
    checkpoint();
    for (int k = idx.size() - 1; k >= 0; k--) fc_drawing_remove(&d_, idx[k]);
    changed();
}

void Document::translate(const QList<int> &idx, double dx, double dy) {
    QList<int> s = sortedUnique(idx, d_.nprims);
    if (s.isEmpty() || (dx == 0 && dy == 0)) return;
    checkpoint();
    for (int i : s) fc_prim_translate(&d_.prims[i], dx, dy);
    changed();
}

void Document::translateLive(const QList<int> &idx, double dx, double dy) {
    for (int i : sortedUnique(idx, d_.nprims)) fc_prim_translate(&d_.prims[i], dx, dy);
    if (onChanged) onChanged();
}

void Document::rotate(const QList<int> &idx) {
    QList<int> s = sortedUnique(idx, d_.nprims);
    if (s.isEmpty()) return;
    QPointF c = pivot(s);
    checkpoint();
    for (int i : s) fc_prim_rotate(&d_.prims[i], c.x(), c.y());
    changed();
}

void Document::mirror(const QList<int> &idx) {
    QList<int> s = sortedUnique(idx, d_.nprims);
    if (s.isEmpty()) return;
    QPointF c = pivot(s);
    checkpoint();
    for (int i : s) fc_prim_mirror(&d_.prims[i], c.x());
    changed();
}

bool Document::replace(int i, const QString &text) {
    if (i < 0 || i >= d_.nprims) return false;
    QByteArray t = text.toUtf8();
    if (!t.endsWith('\n')) t += '\n';
    beginChange();
    int n0 = d_.nprims;
    fc_append_fcd(&d_, t.constData(), (size_t)t.size());
    bool ok = d_.nprims == n0 + 1;
    if (ok) {
        fc_drawing_move(&d_, d_.nprims - 1, i + 1);
        fc_drawing_remove(&d_, i);
    } else {
        d_.nprims = n0; /* parsed to nothing or to several primitives: reject */
    }
    endChange();
    return ok;
}

bool Document::setText(int i, int which, const QString &text) {
    if (i < 0 || i >= d_.nprims || which < 0 || which > 2) return false;
    if ((which == 0) != (d_.prims[i].kind == FC_TEXT)) return false;
    checkpoint();
    QByteArray t = text.toUtf8();
    fc_prim_set_text(&d_, i, which, t.constData());
    changed();
    return true;
}

int Document::handles(int i) const {
    if (i < 0 || i >= d_.nprims) return 0;
    return fc_prim_handles(&d_.prims[i]);
}

QPointF Document::handle(int i, int h) const {
    FcPoint p;
    if (i < 0 || i >= d_.nprims || !fc_prim_handle(&d_.prims[i], h, &p)) return QPointF();
    return QPointF(p.x, p.y);
}

void Document::setHandle(int i, int h, double x, double y) {
    if (i < 0 || i >= d_.nprims) return;
    if (batch_ == 0) {
        checkpoint();
        fc_prim_set_handle(&d_.prims[i], h, x, y);
        changed();
    } else {
        fc_prim_set_handle(&d_.prims[i], h, x, y);
        if (onChanged) onChanged(); /* live feedback inside a drag */
    }
}

void Document::setLayer(const QList<int> &idx, int layer) {
    QList<int> s = sortedUnique(idx, d_.nprims);
    if (s.isEmpty()) return;
    checkpoint();
    for (int i : s) {
        FcPrim *p = &d_.prims[i];
        p->layer = layer;
        if (p->kind == FC_TEXT) p->u.text.layer = layer;
    }
    changed();
}

QList<int> Document::duplicate(const QList<int> &idx, double dx, double dy) {
    QList<int> s = sortedUnique(idx, d_.nprims);
    QString text;
    for (int i : s) text += primFcd(i) + "\n";
    if (text.isEmpty()) return {};
    beginChange();
    QList<int> out = append(text);
    for (int i : out) fc_prim_translate(&d_.prims[i], dx, dy);
    endChange();
    return out;
}

void Document::undo() {
    if (undo_.empty()) return;
    redo_.push_back(fcd());
    QByteArray t = undo_.back();
    undo_.pop_back();
    reparse(t);
    if (onChanged) onChanged();
}

void Document::redo() {
    if (redo_.empty()) return;
    undo_.push_back(fcd());
    QByteArray t = redo_.back();
    redo_.pop_back();
    reparse(t);
    if (onChanged) onChanged();
}
