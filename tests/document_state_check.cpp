#include <QtWidgets>
#include <QtConcurrent>
#include "core/ProjectIO.h"
#include "ui/Widgets.h"
#include "workers/RenderWorker.h"
#include "gpu/GpuFramePackage.h"
#define private public
#include "ui/MainWindow.h"
#undef private
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x); return 1; } } while(false)
static void answer(const QString& text) {
    auto* timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, [timer,text] {
        for (auto* w : QApplication::topLevelWidgets()) {
            if (!w->isVisible()) continue;
            if (text == "Cancel") {
                if (auto* d = dynamic_cast<UnsavedChangesDialog*>(w)) { d->reject(); timer->stop(); timer->deleteLater(); return; }
            } else if (dynamic_cast<StyledMessageBox*>(w)) {
                for (auto* b : w->findChildren<QPushButton*>()) if (b->text() == text) { b->click(); timer->stop(); timer->deleteLater(); return; }
            }
        }
    }); timer->start(10);
}
int main(int argc,char** argv) {
    QApplication app(argc,argv); QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    QImage source(32,32,QImage::Format_ARGB32);source.fill(Qt::red);
    ProjectIO::ProjectData data;data.params.layers.clear();data.title="saved";
    const auto file=dir.filePath("valid.less");QString error;
    CHECK(ProjectIO::save(file,data,&error));
    ProjectIO::ProjectData loaded;CHECK(ProjectIO::load(file,&loaded,&error));CHECK(loaded.title==data.title);
    const auto bad=dir.filePath("invalid.less");
    {QFile f(bad);CHECK(f.open(QIODevice::WriteOnly));f.write("{}");}
    CHECK(!ProjectIO::load(bad,&loaded,&error));CHECK(loaded.title==data.title);
    data.media.insert(1,{"broken",{}});CHECK(!ProjectIO::save(file,data,&error));
    CHECK(ProjectIO::load(file,&loaded,&error));CHECK(loaded.title=="saved");
    MainWindow w;CHECK(!w.isDirty());
    const int mid=w.addImageToLibrary(source,"source");CHECK(w.isDirty());
    w.m_projectPath=dir.filePath("still.less");CHECK(w.saveProject(false));
    CHECK(ProjectIO::load(w.m_projectPath,&loaded,&error));CHECK(loaded.media.size()==1);
    w.markSaved();w.m_images[0].title="renamed";CHECK(w.isDirty());
    answer("Cancel");w.openProjectFromPath(file);CHECK(w.m_images[0].title=="renamed");
    w.addLayerFromMedia(mid);w.pushUndoSnapshot();answer("Yes");w.onThumbCloseRequested(mid);w.pushUndoSnapshot();
    w.undo();CHECK(w.m_images[0].media.contains(mid));CHECK(w.m_images[0].state.layers.size()==1);
    w.redo();CHECK(!w.m_images[0].media.contains(mid));
    w.m_worker->invalidatePending();QThreadPool::globalInstance()->waitForDone();
    std::puts("document save, dirty state, replacement cancel and media undo passed");
    return 0;
}
