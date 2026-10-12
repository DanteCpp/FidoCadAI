#pragma once

#include "document.h"
#include "settings.h"
#include "theme.h"

#include <QDateTime>
#include <QImage>
#include <QPointF>
#include <QWidget>
#include <functional>

/* Point-and-click editor. The drawing itself is painted by the C renderer
 * (fc_render_rgb) so it matches `fcai render`; selection and tool previews are
 * drawn on top with QPainter. */
class Canvas : public QWidget {
public:
    enum Tool { Select, Line, Rect, Oval, Dot, Text, Macro };

    Canvas(Document *doc, Settings *settings, const Theme *theme, QWidget *parent = nullptr);

    Tool tool() const { return tool_; }
    void setTool(Tool t);
    void setMacro(const QString &key); /* switches to the Macro tool */
    QString macro() const { return macro_; }
    static QString toolName(Tool t);

    QList<int> selection() const { return sel_; }
    void setSelection(const QList<int> &sel);
    void selectAll();

    void fit();
    void fitIfOutside(); /* keep agent updates visible without moving an already-fitting view */
    bool autoFit() const { return auto_fit_; } /* no manual pan or zoom since the last fit */
    void zoomBy(double factor, QPointF at);
    void refresh(); /* settings/theme changed */
    void docChanged();

    void rotateSelection();
    void mirrorSelection();
    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void paste();
    /* Properties form drawn inside the canvas (P, F2, double-click). No
     * separate window: Hyprland can bury a modal dialog behind the drawing. */
    void editSelection();
    bool editing() const { return form_ != nullptr; }
    class InlineForm *form() const { return form_; }
    void toggleCheatsheet(); /* K: keybindings, type to search */
    bool cheatsheetVisible() const { return cheats_; }
    QString cheatsheetQuery() const { return cheats_query_; }
    int cheatsheetScroll() const { return cheats_scroll_; }
    bool rulerVisible() const { return ruler_active_; }
    double rulerLength() const; /* logical units, 0 when no measurement */
    void cycleLayer(int step); /* [ and ]: layer for new elements, and the selection */
    void nudge(int dx, int dy);

    QImage renderImage(int w, int h) const; /* whole drawing, for vision models */
    QPointF toPixel(QPointF lg) const;     /* logical -> widget coordinates */
    QColor paper() const;                  /* canvas background (palette preview uses it) */
    /* Tracing picture (bg_image): view only, never part of the drawing. */
    QRectF backgroundRect() const; /* logical units; empty when unset or hidden */
    QString backgroundError() const { return bg_error_; }

    std::function<void()> onSelectionChanged;
    std::function<void()> onToolChanged;
    std::function<void(const QString &)> onStatus;
    std::function<void()> onSend;                          /* Return: tell the agent */
    std::function<void()> onToggleBar;                     /* I: bottom bar */
    std::function<void(const QString &)> onSettingChanged; /* [ ] F G changed a setting */

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    bool event(QEvent *) override; /* touchpad pinch / pan gestures */
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QPointF toLogical(QPointF px) const;
    QPointF snap(QPointF lg) const;
    int grid() const;
    int layer() const;
    QColor layerColor(int l) const; /* layer ink, adapted for dark paper */
    void renderCache();
    void loadBackground(); /* re-reads bg_image when its path or file changed */
    void finishLine();
    void cancelOp();
    void status();
    void place(QPointF at);
    int handleAt(QPointF px, int *handle_out) const; /* selection control points */
    void snapshotHandles();
    void moveHandles(QPointF delta); /* one shared, bounded translation */
    void sketch(class QPainter &p, const FcDrawing &flat) const;
    void sketchIndex(class QPainter &p, int i) const;
    void sketchMacro(class QPainter &p, QPointF at) const;
    void drawRuler(class QPainter &p) const;
    QString num(double v) const;

    Document *doc_;
    Settings *settings_;
    const Theme *theme_;

    Tool tool_ = Select;
    QString macro_;
    int macro_orient_ = 0, macro_mirror_ = 0;
    QList<int> sel_;
    QList<QPair<int, int>> selected_handles_; /* primitive index, handle index */
    QList<QPointF> handle_starts_;

    double ox_ = 40, oy_ = 40, scale_ = 4; /* widget px = logical * scale + origin */
    void panBy(QPointF d);
    bool auto_fit_ = true; /* follow canvas resizes until the user pans or zooms */
    QImage cache_;
    bool dirty_ = true;
    QImage bg_img_;
    QString bg_path_, bg_error_;
    QDateTime bg_mtime_;

    enum Op { None, Panning, Moving, Resizing, Banding, Drawing, RulerArmed } op_ = None;
    QPointF press_px_, press_lg_, cursor_lg_, moved_;
    bool has_cursor_ = false;
    bool editing_handles_ = false;
    int drag_prim_ = -1, drag_handle_ = -1; /* handle being dragged */
    bool have_start_ = false; /* line/rect/oval anchor set */
    QPointF start_;
    class InlineForm *form_ = nullptr;
    bool cheats_ = false;
    QString cheats_query_;
    int cheats_scroll_ = 0;
    QRect cheats_box_;
    class QScrollBar *cheats_bar_ = nullptr;
    bool ruler_active_ = false, ruler_dragged_ = false;
    QPointF ruler_a_, ruler_b_; /* logical units */
    void paintCard(class QPainter &p, const QList<QPair<QString, QString>> &rows,
                   const QString *search, int *scroll);
    void flipSetting(const QString &key);
};
