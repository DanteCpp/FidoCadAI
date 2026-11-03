#include "../gui/canvas.h"
#include "../gui/props.h"
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>
#include <cstdio>
static int checks, failures;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; fprintf(stderr,"%d: %s\n",__LINE__,#c); } } while(0)
static void key(QWidget *w,int code,Qt::KeyboardModifiers mods=Qt::NoModifier) {
    QKeyEvent e(QEvent::KeyPress,code,mods);QApplication::sendEvent(w,&e);QApplication::processEvents();
}
static void run(const QString &mode,const QString &dir) {
    Settings settings(dir+"/"+mode+".conf");settings.set("show_grid","0");settings.set("paper","white");
    Theme theme=Theme::load(mode);theme.apply(*qApp,QString(),11);
    Document doc;Canvas canvas(&doc,&settings,&theme);
    doc.onChanged=[&]{canvas.docChanged();};canvas.resize(1000,700);canvas.show();
    QImage image(200,120,QImage::Format_ARGB32);image.fill(QColor(220,40,30,180));
    QString png=dir+"/image with spaces.png",jpg=dir+"/image.jpg";
    CHECK(image.save(png));CHECK(image.save(jpg));
    QString error;
    CHECK(canvas.importImageFile(png,{20,30},&error));CHECK(doc.count()==1 && canvas.selection()==QList<int>{0});
    CHECK(doc.drawing().prims[0].u.image.width==100 && doc.drawing().prims[0].u.image.height==60);
    CHECK(!doc.setText(0,1,"label"));
    QByteArray saved=doc.fcd();CHECK(saved.contains("FJC IMAGE 20 30 100 60"));
    CHECK(!canvas.importImageFile(dir+"/missing.png",{0,0},&error));CHECK(doc.fcd()==saved);
    doc.undo();CHECK(doc.count()==0 && !doc.canUndo());doc.redo();CHECK(doc.fcd()==saved);
    CHECK(doc.saveAs(dir+"/"+mode+".fcd",&error));QFile::remove(png);
    Document reopened;CHECK(reopened.open(doc.path(),&error));CHECK(reopened.fcd()==saved);
    /* Props expose geometry rather than encoded source; invalid edits stay open. */
    canvas.setSelection({0});key(&canvas,Qt::Key_P);auto *form=canvas.form();CHECK(form);
    if(form) {
        auto edits=form->findChildren<QLineEdit *>();CHECK(edits.size()==5);
        CHECK(edits.front()->text()=="20 30");edits[1]->setText("150");edits[2]->setText("90");
        edits[3]->setText("30");edits[4]->setText("2");key(edits[4],Qt::Key_Return);
        CHECK(canvas.editing() && doc.fcd()==saved);
        edits[4]->setText("0.4");key(edits[4],Qt::Key_Return);
        CHECK(!canvas.editing() && doc.drawing().prims[0].u.image.width==150);
        CHECK(doc.drawing().prims[0].u.image.rotation==30 && doc.drawing().prims[0].u.image.opacity==.4);
        doc.undo();CHECK(doc.fcd()==saved);
    }
    canvas.setSelection({0});canvas.copySelection();canvas.paste();CHECK(doc.count()==2);
    doc.undo();CHECK(doc.count()==1);doc.redo();CHECK(doc.count()==2);
    QApplication::clipboard()->setImage(image);canvas.paste();CHECK(doc.count()==3);
    doc.undo();CHECK(doc.count()==2);
    /* Real MIME drag/drop events import JPEG at the drop point as one undo step. */
    QMimeData mime;mime.setUrls({QUrl::fromLocalFile(jpg),QUrl::fromLocalFile(jpg)});
    QDragEnterEvent enter(QPointF(200,300),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&enter);CHECK(enter.isAccepted());
    QDropEvent drop(QPointF(200,300),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&drop);CHECK(drop.isAccepted() && doc.count()==4);
    CHECK(canvas.selection().size()==2);
    doc.undo();CHECK(doc.count()==2);doc.redo();CHECK(doc.count()==4);
    /* Shift+I accepts a path with spaces through the inline form. */
    CHECK(image.save(png));key(&canvas,Qt::Key_I,Qt::ShiftModifier);form=canvas.form();CHECK(form);
    if(form) {
        auto *entry=form->findChildren<QLineEdit *>().front();entry->setText(png);
        key(entry,Qt::Key_Return);CHECK(!canvas.editing() && doc.count()==5);
    }
    canvas.setSelection({0});canvas.rotateSelection();canvas.mirrorSelection();
    CHECK(doc.handles(0)==4);QPointF opposite=doc.handle(0,0),corner=doc.handle(0,2);
    doc.setHandle(0,2,corner.x()+5,corner.y()+5);CHECK(doc.handle(0,0)==opposite);
    canvas.fit();QApplication::processEvents();canvas.grab().save("/tmp/fcai-images-"+mode+".png");
    /* A failed bitmap paste leaves undo history and drawing unchanged. */
    QByteArray before=doc.fcd();CHECK(doc.addImage("bad bytes",{0,0},0,&error)==-1);CHECK(doc.fcd()==before);
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);QTemporaryDir dir;run("light",dir.path());run("dark",dir.path());
    printf("%d/%d image GUI checks passed\n",checks-failures,checks);return failures?1:0;
}
