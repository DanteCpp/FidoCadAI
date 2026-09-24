#pragma once

#include "document.h"

#include <QFrame>
#include <functional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLineEdit;

/* A small form drawn inside the canvas instead of a dialog window: a tiling
 * WM (Hyprland) can put a separate modal window behind the drawing and leave
 * the editor unusable. Tab moves between fields, Enter applies, Esc cancels. */
class InlineForm : public QFrame {
public:
    InlineForm(QWidget *parent, const QString &title);

    QLineEdit *addText(const QString &label, const QString &value, const QString &hint = QString());
    QComboBox *addChoice(const QString &label, const QStringList &items, int current);
    QCheckBox *addCheck(const QString &label, bool on);
    void setError(const QString &msg);

    /* Place near at (widget coordinates, clamped inside the parent) and focus the first field. */
    void popup(QPoint at);
    void apply();  /* runs onApply; stays open if it returns an error */
    void cancel();

    /* Returns an error to show, or empty to close. */
    std::function<QString()> onApply;
    std::function<void()> onClosed;

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void watch(QWidget *w);
    void close_();

    QFormLayout *form_;
    class QLabel *error_;
    QList<QWidget *> fields_;
    bool closed_ = false;
};

/* Every property FidoCad stores for primitive i (layer, coordinates, arrows,
 * dash, fill, text style, pad shape, orientation, name/value, ...), applied
 * as one undo step. */
InlineForm *editProperties(QWidget *parent, Document *doc, int i);
