#include "props.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <algorithm>
#include <vector>

InlineForm::InlineForm(QWidget *parent, const QString &title) : QFrame(parent) {
    setObjectName("chrome");
    setFrameShape(QFrame::Box);
    setAutoFillBackground(true);
    form_ = new QFormLayout(this);
    form_->setContentsMargins(10, 8, 10, 8);
    form_->setVerticalSpacing(4);
    auto *t = new QLabel(title + "   ·   Enter apply · Esc cancel · Tab next", this);
    t->setObjectName("dim");
    form_->addRow(t);
    error_ = new QLabel(this);
    error_->setObjectName("dim");
    error_->hide();
}

void InlineForm::watch(QWidget *w) {
    w->installEventFilter(this);
    fields_ << w;
}

QLineEdit *InlineForm::addText(const QString &label, const QString &value, const QString &hint) {
    auto *e = new QLineEdit(value, this);
    e->setPlaceholderText(hint);
    e->setMinimumWidth(220);
    form_->addRow(label, e);
    watch(e);
    return e;
}

QComboBox *InlineForm::addChoice(const QString &label, const QStringList &items, int current) {
    auto *c = new QComboBox(this);
    c->addItems(items);
    c->setCurrentIndex(qBound(0, current, (int)items.size() - 1));
    form_->addRow(label, c);
    watch(c);
    return c;
}

QCheckBox *InlineForm::addCheck(const QString &label, bool on) {
    auto *c = new QCheckBox(this);
    c->setChecked(on);
    form_->addRow(label, c);
    watch(c);
    return c;
}

void InlineForm::setError(const QString &msg) {
    if (form_->indexOf(error_) < 0) form_->addRow(error_);
    error_->setText(msg);
    error_->setVisible(!msg.isEmpty());
    adjustSize();
}

void InlineForm::popup(QPoint at) {
    adjustSize();
    QWidget *p = parentWidget();
    at.setX(qBound(4, at.x(), qMax(4, p->width() - width() - 4)));
    at.setY(qBound(4, at.y(), qMax(4, p->height() - height() - 4)));
    move(at);
    show();
    raise();
    if (!fields_.isEmpty()) {
        fields_[0]->setFocus();
        if (auto *e = qobject_cast<QLineEdit *>(fields_[0])) e->selectAll();
    }
}

bool InlineForm::eventFilter(QObject *o, QEvent *e) {
    if (e->type() == QEvent::ShortcutOverride) {
        /* single-key canvas shortcuts must not fire while a field has focus */
        auto *k = static_cast<QKeyEvent *>(e);
        if (!(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            e->accept();
            return true;
        }
    }
    if (e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        auto *combo = qobject_cast<QComboBox *>(o);
        if (k->key() == Qt::Key_Escape) {
            cancel();
            return true;
        }
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && !(combo && combo->view()->isVisible())) {
            apply();
            return true;
        }
    }
    return QFrame::eventFilter(o, e);
}

void InlineForm::apply() {
    if (closed_) return;
    QString err = onApply ? onApply() : QString();
    if (!err.isEmpty()) {
        setError(err);
        return;
    }
    close_();
}

void InlineForm::cancel() { close_(); }

void InlineForm::close_() {
    if (closed_) return;
    closed_ = true;
    hide();
    if (parentWidget()) parentWidget()->setFocus();
    if (onClosed) onClosed();
    deleteLater();
}

/* ---------------- properties ---------------- */

namespace {

QString num(double v) { return QString::number(v, 'g', 10); }

QString pointsText(const FcPoint *p, int n) {
    QStringList s;
    for (int k = 0; k < n; k++) s << num(p[k].x) << num(p[k].y);
    return s.join(' ');
}

const QStringList kDash = {"solid", "dashed", "fine dotted", "dotted", "dash-dot"};
const QStringList kArrows = {"none", "at start", "at end", "both ends"};
const QStringList kArrowStyle = {"filled head", "filled head + bar", "open head", "open head + bar"};
const QStringList kOrient = {"0°", "90°", "180°", "270°"};
const QStringList kPad = {"round/oval", "rectangular", "rounded rectangle"};

bool vertexList(FcKind k) { return k == FC_POLY || k == FC_CURVE; }

/* Points of a primitive as editable coordinates. */
QString coordsOf(const FcPrim &p) {
    int n;
    const FcPoint *pts = fc_prim_points(&p, &n);
    return pointsText(pts, n);
}

/* "x y", "x1 y1 x2 y2", ... for n points; vertex lists take any count >= 2. */
QString coordsHint(const FcPrim &p) {
    int n;
    if (vertexList(p.kind)) return "x1 y1 x2 y2 ... (at least 2 points)";
    fc_prim_points(&p, &n);
    if (n == 1) return "x y";
    QStringList s;
    for (int k = 1; k <= n; k++) s << QString("x%1 y%1").arg(k);
    return s.join(' ');
}

bool parseNum(const QString &s, double *out) {
    bool ok;
    *out = s.trimmed().toDouble(&ok);
    return ok;
}

} // namespace

InlineForm *editProperties(QWidget *parent, Document *doc, int i) {
    if (i < 0 || i >= doc->count()) return nullptr;
    const FcPrim p0 = doc->drawing().prims[i];
    const FcKind k = p0.kind;
    auto *f = new InlineForm(parent, QString("%1 · %2 [%3]").arg(fc_kind_code(&p0), fc_kind_name(k)).arg(i));

    QStringList layers;
    for (int l = 0; l < FC_MAX_LAYERS; l++)
        layers << QString("%1  %2").arg(l).arg(QString::fromUtf8(doc->drawing().layers[l].name));

    QLineEdit *text = nullptr, *font = nullptr, *sizeY = nullptr, *sizeX = nullptr, *rot = nullptr;
    QCheckBox *bold = nullptr, *italic = nullptr, *mirrorTxt = nullptr;
    QLineEdit *key = nullptr;
    QComboBox *orient = nullptr;
    QCheckBox *mirror = nullptr;
    QLineEdit *width = nullptr, *padX = nullptr, *padY = nullptr, *hole = nullptr;
    QComboBox *padStyle = nullptr;
    QComboBox *arrows = nullptr, *arrowStyle = nullptr, *dash = nullptr;
    QLineEdit *arrowLen = nullptr, *arrowW = nullptr, *stroke = nullptr;
    QCheckBox *filled = nullptr, *closed = nullptr;
    QLineEdit *name = nullptr, *value = nullptr;
    /* copied now: the drawing's strings do not outlive an undo */
    const QString oldName = QString::fromUtf8(p0.has_name && p0.name.text ? p0.name.text : "");
    const QString oldValue = QString::fromUtf8(p0.has_value && p0.value.text ? p0.value.text : "");

    if (k == FC_TEXT) {
        const FcLabel &t = p0.u.text;
        text = f->addText("Text", QString::fromUtf8(t.text ? t.text : ""), "$...$ for math");
    }
    if (k == FC_MACRO) {
        key = f->addText("Symbol", QString::fromUtf8(p0.u.macro.key ? p0.u.macro.key : ""), "library key, e.g. 080");
        orient = f->addChoice("Rotation", kOrient, p0.u.macro.orient & 3);
        mirror = f->addCheck("Mirrored", p0.u.macro.mirror);
    }
    if (k != FC_TEXT) {
        name = f->addText("Name", oldName, "e.g. R1");
        value = f->addText("Value", oldValue, "e.g. 10k");
    }
    QComboBox *layer = f->addChoice("Layer", layers, k == FC_TEXT ? p0.u.text.layer : p0.layer);
    const int caps = fc_kind_caps(k);
    QLineEdit *coords = f->addText("Coordinates", coordsOf(p0), coordsHint(p0));
    if (caps & FC_KIND_FILL) filled = f->addCheck("Filled", p0.filled);
    if (k == FC_CURVE) closed = f->addCheck("Closed", p0.u.poly.closed);
    if (caps & FC_KIND_DASH) {
        dash = f->addChoice("Dash", kDash, p0.ext.dash);
        stroke = f->addText("Line width", p0.ext.width > 0 ? num(p0.ext.width) : QString(),
                            "empty = drawing default");
    }
    if (caps & FC_KIND_ARROW) {
        arrows = f->addChoice("Arrows", kArrows, p0.ext.arrows & 3);
        arrowStyle = f->addChoice("Arrow style", kArrowStyle, p0.ext.arrow_style & 3);
        arrowLen = f->addText("Arrow length", num(p0.ext.arrow_len > 0 ? p0.ext.arrow_len : 3));
        arrowW = f->addText("Arrow half width", num(p0.ext.arrow_half_w > 0 ? p0.ext.arrow_half_w : 1));
    }
    if (k == FC_TEXT) {
        const FcLabel &t = p0.u.text;
        sizeY = f->addText("Height", num(t.size_y));
        sizeX = f->addText("Width", num(t.size_x));
        rot = f->addText("Rotation (deg)", QString::number(t.rotation));
        bold = f->addCheck("Bold", t.style & 1);
        italic = f->addCheck("Italic", t.style & 2);
        mirrorTxt = f->addCheck("Mirrored", t.style & 4);
        font = f->addText("Font", QString::fromUtf8(t.font && *t.font ? t.font : "*"),
                          "* (default), Sans-serif, Courier New, Times New Roman, ...");
    }
    if (k == FC_PCB_LINE) width = f->addText("Width", num(p0.u.pcb_line.width));
    if (k == FC_PCB_PAD) {
        padX = f->addText("Size x", num(p0.u.pad.rx));
        padY = f->addText("Size y", num(p0.u.pad.ry));
        hole = f->addText("Hole", num(p0.u.pad.hole));
        padStyle = f->addChoice("Shape", kPad, p0.u.pad.style);
    }

    f->onApply = [=]() -> QString {
        if (i >= doc->count() || doc->drawing().prims[i].kind != k) return "the element changed; press Esc";
        FcPrim p = doc->drawing().prims[i]; /* shallow copy; strings/points stay valid until replace */
        std::vector<FcPoint> pts;
        QByteArray keyUtf8, textUtf8, fontUtf8;

        /* coordinates */
        QStringList toks = coords->text().split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
        if (toks.size() % 2) return "coordinates need x y pairs";
        for (int t = 0; t < toks.size(); t += 2) {
            FcPoint q;
            if (!parseNum(toks[t], &q.x) || !parseNum(toks[t + 1], &q.y)) return "coordinates must be numbers";
            pts.push_back(q);
        }
        int n = (int)pts.size(), need;
        FcPoint *dst = fc_prim_points(&p, &need);
        if (vertexList(k)) {
            if (n < 2) return "at least 2 points";
            p.u.poly.pts = pts.data();
            p.u.poly.n = n;
        } else if (n != need) {
            return QString("expected %1 coordinate pair%2 (%3)").arg(need).arg(need > 1 ? "s" : "").arg(coordsHint(p));
        } else {
            std::copy(pts.begin(), pts.end(), dst);
        }

        p.layer = layer->currentIndex();
        if (filled) p.filled = filled->isChecked();
        if (closed) p.u.poly.closed = closed->isChecked();
        if (dash) p.ext.dash = dash->currentIndex();
        if (stroke) {
            QString s = stroke->text().trimmed();
            p.ext.width = 0;
            if (!s.isEmpty() && (!parseNum(s, &p.ext.width) || p.ext.width < 0))
                return "line width must be a non-negative number (empty = drawing default)";
        }
        if (arrows) {
            p.ext.arrows = arrows->currentIndex();
            p.ext.arrow_style = arrowStyle->currentIndex();
            if (!parseNum(arrowLen->text(), &p.ext.arrow_len) || p.ext.arrow_len <= 0)
                return "arrow length must be a positive number";
            if (!parseNum(arrowW->text(), &p.ext.arrow_half_w) || p.ext.arrow_half_w <= 0)
                return "arrow half width must be a positive number";
        }
        if (k == FC_MACRO) {
            keyUtf8 = key->text().trimmed().toLower().toUtf8();
            if (keyUtf8.isEmpty()) return "the symbol key cannot be empty";
            p.u.macro.key = keyUtf8.constData();
            p.u.macro.orient = orient->currentIndex();
            p.u.macro.mirror = mirror->isChecked();
        }
        if (k == FC_TEXT) {
            FcLabel &t = p.u.text;
            textUtf8 = text->text().toUtf8();
            if (text->text().trimmed().isEmpty()) return "text cannot be empty (use Del to remove it)";
            t.text = textUtf8.constData();
            if (!parseNum(sizeY->text(), &t.size_y) || t.size_y <= 0) return "height must be a positive number";
            if (!parseNum(sizeX->text(), &t.size_x) || t.size_x <= 0) return "width must be a positive number";
            bool ok;
            t.rotation = rot->text().trimmed().toInt(&ok);
            if (!ok) return "rotation must be whole degrees";
            t.style = (bold->isChecked() ? 1 : 0) | (italic->isChecked() ? 2 : 0) | (mirrorTxt->isChecked() ? 4 : 0);
            QString fn = font->text().trimmed();
            fontUtf8 = (fn.isEmpty() || fn == "*" ? QString("*") : fn).toUtf8();
            t.font = fontUtf8.constData();
            t.layer = p.layer;
        }
        if (k == FC_PCB_LINE && (!parseNum(width->text(), &p.u.pcb_line.width) || p.u.pcb_line.width < 0))
            return "width must be a number";
        if (k == FC_PCB_PAD) {
            if (!parseNum(padX->text(), &p.u.pad.rx) || !parseNum(padY->text(), &p.u.pad.ry) ||
                !parseNum(hole->text(), &p.u.pad.hole))
                return "pad sizes must be numbers";
            p.u.pad.style = padStyle->currentIndex();
        }
        /* labels move with the layer */
        p.name.layer = p.value.layer = p.layer;

        char *fcd = fc_prim_fcd(&p);
        QString line = QString::fromUtf8(fcd ? fcd : "");
        free(fcd);
        doc->beginChange();
        bool ok = doc->replace(i, line);
        if (ok && name) {
            if (name->text() != oldName) doc->setText(i, 1, name->text());
            if (value->text() != oldValue) doc->setText(i, 2, value->text());
        }
        doc->endChange();
        return ok ? QString() : "could not apply these values";
    };
    return f;
}
