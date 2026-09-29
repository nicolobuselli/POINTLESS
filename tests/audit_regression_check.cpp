#include <QtWidgets>
#include <QtConcurrent>
#include <QSvgRenderer>
#include "core/ProjectIO.h"
#include "core/VideoIO.h"
#include "core/FrameStore.h"
#include "ui/FilmstripWidget.h"
#include "ui/Theme.h"
#include "core/AsciiRenderer.h"
#define private public
#include "workers/RenderWorker.h"
#include "ui/ControlsPanel.h"
#include "ui/Widgets.h"
#include "ui/MainWindow.h"
#undef private
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x); return 1; } } while(false)
int main(int argc, char** argv) {
 QApplication app(argc,argv); QTemporaryDir dir; QString err;
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
 Animation anim; anim.fps=24;anim.stepFps=15;anim.frameStart=0;
 QSet<int> samples;for(int f=0;f<24;++f){auto sample=steppedFrame(anim,f);CHECK(sample<=f);samples.insert(sample);}CHECK(samples.size()==15);
 ControlsPanel panel; panel.setFrameSize(1080,1080);LayerTransform tf;tf.rotation=12.4f;tf.scalePct=33.33f;tf.xPct=.12345f;
 panel.setTransform(tf,QSize(301,199));LayerTransform emitted;
 QObject::connect(&panel,&ControlsPanel::transformChanged,[&](const LayerTransform& v){emitted=v;});panel.emitTransform();CHECK(tf==emitted);
 panel.m_tfX->setValue(400);panel.emitTransform(0);CHECK(emitted.rotation==tf.rotation);CHECK(emitted.scalePct==tf.scalePct);CHECK(emitted.aspectPct==tf.aspectPct);
 Layer keyed;setParam(keyed,ParamId::TfX,2.0);setParam(keyed,ParamId::TfScale,2000.0);CHECK(keyed.transform.xPct==2.0f);CHECK(keyed.transform.scalePct==2000.0f);
 QImage src(32,32,QImage::Format_ARGB32);src.fill(QColor(128,64,192));
 SessionParams p;p.frameW=p.frameH=32;p.layers.clear();p.background=QColor(128,128,128);p.backgroundOpacity=1;
 Layer layer;layer.id=1;layer.mediaId=1;layer.kind=LayerKind::Original;layer.blend=BlendMode::Multiply;p.layers.push_back(layer);
 const auto cpu=RenderWorker::renderDocument(src,p);
 const QString svg=dir.filePath("blend.svg");CHECK(RenderWorker::renderDocumentToSvg(svg,src,p));
 QSvgRenderer vector(svg);CHECK(vector.isValid());QImage raster(32,32,QImage::Format_ARGB32);raster.fill(Qt::transparent);{QPainter paint(&raster);vector.render(&paint);}
 CHECK(raster.pixelColor(16,16)==cpu.pixelColor(16,16));
 CHECK(!RenderWorker::renderDocumentToSvg(dir.filePath("missing/out.svg"),src,p));
 AsciiSettings ascii;ascii.cellSize=4096;CHECK(!AsciiRenderer::gpuRenderable(ascii));CHECK(AsciiRenderer::gpuAtlas(ascii).image.isNull());
 ProjectIO::ProjectData data;data.params=p;data.title="portable";data.media.insert(1,{"clip",src,{src,src},30.0});
 ParentGroup group;group.mediaId=1;group.trimIn=1;group.trimOut=8;group.timeOffset=5;data.params.parents.push_back(group);
 const QString custom=dir.filePath("shape.svg");{QFile f(custom);CHECK(f.open(QIODevice::WriteOnly));f.write("<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'><circle cx='4' cy='4' r='3'/></svg>");}
 data.params.layers[0].dotGrid.shapes={{DotGridShape::CustomSVG,custom}};
 const QString project=dir.filePath("video.less");CHECK(ProjectIO::save(project,data,&err));QFile::remove(custom);
 ProjectIO::ProjectData loaded;CHECK(ProjectIO::load(project,&loaded,&err));CHECK(loaded.media[1].frames.size()==2);CHECK(loaded.media[1].fps==30.0);CHECK(loaded.params.parents[0]==group);CHECK(QFile::exists(loaded.params.layers[0].dotGrid.shapes[0].svgPath));
 {QFile f(project);CHECK(f.open(QIODevice::ReadOnly));auto obj=QJsonDocument::fromJson(f.readAll()).object();f.close();auto params=obj["params"].toObject();params["frameW"]="wrong";obj["params"]=params;CHECK(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write(QJsonDocument(obj).toJson());}
 CHECK(!ProjectIO::load(project,&loaded,&err));CHECK(loaded.title=="portable");
 MainWindow::SessionImage document;document.state=p;document.anim=anim;document.state.parents={group};document.media.insert(1,{"clip",src,{src,src},30.0});
 auto snap=MainWindow::frameSnapshot(document,0);CHECK(!snap.params.layers[0].visible);
 UnsavedChangesDialog dialog("test");
 QStringList actions; for(auto* b:dialog.findChildren<QPushButton*>()) if(!b->text().isEmpty()) actions.append(b->text());
 CHECK(actions.contains("Save")); CHECK(actions.contains("Discard")); CHECK(!actions.contains("Cancel"));
 dialog.reject(); CHECK(dialog.choice()==UnsavedChangesDialog::Cancel);
 MainWindow window;const int mid=window.addImageToLibrary(src,"video");
 window.resize(1920,1080);window.show();app.processEvents();
 CHECK(!window.findChild<QLabel*>("documentStatus"));
 auto* split=window.findChild<QSplitter*>("mainSplit");CHECK(split);
 auto* mode=window.findChild<QPushButton*>("algoBoxAccent");CHECK(mode);
 const int modeWidth=mode->width();CHECK(modeWidth==Ui::px(Ui::kModePickerW));
 auto* row=window.findChild<QWidget*>("filmstripRow");CHECK(row);
 auto tiles=row->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly);CHECK(!tiles.isEmpty());
 const QSize tileSize=tiles.first()->size();
 split->setSizes({1,1900,1});app.processEvents();
 CHECK(split->sizes()[0]>=Ui::px(Ui::kLeftPanelMinW));CHECK(split->sizes()[2]>=Ui::px(Ui::kRightPanelMinW));
 split->setSizes({1900,1,1900});app.processEvents();
 CHECK(split->sizes()[0]<=Ui::px(Ui::kSidePanelMaxW));CHECK(split->sizes()[2]<=Ui::px(Ui::kSidePanelMaxW));
 CHECK(split->sizes()[0]<window.width()/2);CHECK(split->sizes()[2]<window.width()/2);
 CHECK(mode->width()==modeWidth);CHECK(tiles.first()->size()==tileSize);
 window.hide();
 window.m_images[0].media[mid].frames=QVector<QImage>(300,src);window.m_images[0].media[mid].fps=30;
 window.addLayerFromMedia(mid);CHECK(window.m_images[0].anim.frameEnd==299);CHECK(window.m_images[0].anim.fps==30);
 window.m_images[0].anim.frameEnd=150;window.addLayerFromMedia(mid);CHECK(window.m_images[0].anim.frameEnd==150);
 window.m_worker->invalidatePending();
 RenderWorker switching;
 SessionParams ditherDoc=p;ditherDoc.layers[0].kind=LayerKind::Dither;ditherDoc.layers[0].blend=BlendMode::Normal;
 switching.renderLayersPackage(src,ditherDoc,{});
 CHECK(switching.renderDocumentInteractive(src,ditherDoc,{})==RenderWorker::renderDocument(src,ditherDoc));
 RenderWorker worker;int stale=0,fresh=0;QImage blue=src;blue.fill(Qt::blue);p.layers[0].blend=BlendMode::Normal;
 QObject::connect(&worker,&RenderWorker::renderComplete,[&](const QImage& image,bool){if(image.pixelColor(16,16)==Qt::blue)++fresh;else ++stale;});
 worker.requestRender(src,p,false);worker.requestRender(blue,p,false);
 QElapsedTimer wait;wait.start();while(fresh==0 && wait.elapsed()<10000){app.processEvents();QThread::msleep(1);}CHECK(fresh>0);CHECK(stale==0);worker.invalidatePending();
 if(VideoIO::available()){
  std::atomic_bool cancel{false};std::atomic_int progress{0};const QString mp4=dir.filePath("test.mp4");
  CHECK(VideoIO::encodeFrames(src.size(),24,3,[&](int){return src;},mp4,err,cancel,progress));
  QVector<QImage> frames;double fps=0;CHECK(VideoIO::decode(mp4,frames,fps,err));CHECK(frames.size()==3);
  CHECK(!VideoIO::decode(mp4,frames,fps,err,1));CHECK(frames.isEmpty());
  CHECK(!VideoIO::decode(mp4,frames,fps,err,0,nullptr,1));CHECK(frames.isEmpty());
  // Small compressed clips must not fail at 512 MiB of decoded pixels.
  const QString largeClip = dir.filePath("small-file-full-hd.mp4");
  QImage hd(1920,1080,QImage::Format_ARGB32); hd.fill(QColor(31,97,151));
  CHECK(VideoIO::encodeFrames(hd.size(),30,70,[&](int i){QImage f=hd; if(i==69)f.fill(Qt::red); return f;},largeClip,err,cancel,progress));
  CHECK(QFileInfo(largeClip).size()<4*1024*1024);
  const auto cachedBefore = QDir(QDir::tempPath()).entryList({"pointless-frames-*"},QDir::Files);
  {
   QVector<QImage> diskFrames;
   CHECK(VideoIO::decode(largeClip,diskFrames,fps,err)); CHECK(diskFrames.size()==70);
   CHECK(qint64(diskFrames.size())*diskFrames.first().sizeInBytes()>512LL*1024*1024);
   CHECK(diskFrames.first().size()==hd.size()); CHECK(FrameStore::ownedBytes(diskFrames.last())==0);
   const QColor lastColor=diskFrames.last().pixelColor(0,0); CHECK(lastColor.red()>240 && lastColor.blue()<10);
   QImage retained=diskFrames.last(); QImage edited=retained; edited.fill(Qt::green);
   CHECK(retained.pixelColor(0,0)==lastColor); CHECK(FrameStore::ownedBytes(edited)>0);
   ProjectIO::ProjectData largeData; largeData.params=p;
   largeData.media.insert(1,{"full-hd",diskFrames.first(),diskFrames,fps});
   const QString saved=dir.filePath("full-hd.less"); CHECK(ProjectIO::save(saved,largeData,&err));
   ProjectIO::ProjectData reopened; CHECK(ProjectIO::load(saved,&reopened,&err));
   CHECK(reopened.media[1].frames.size()==70);
   CHECK(reopened.media[1].frames.last().pixelColor(0,0)==lastColor);
   CHECK(FrameStore::ownedBytes(reopened.media[1].frames.first())==0);
   std::printf("Full-HD import/save/reopen: %lld compressed bytes, %lld decoded bytes, 70 frames\n",
     static_cast<long long>(QFileInfo(largeClip).size()),static_cast<long long>(70*hd.sizeInBytes()));
   largeData.media.clear(); diskFrames.clear();
   CHECK(retained.pixelColor(0,0)==lastColor);
  }
  CHECK(QDir(QDir::tempPath()).entryList({"pointless-frames-*"},QDir::Files)==cachedBefore);
  cancel=true; CHECK(!VideoIO::decode(largeClip,frames,fps,err,0,&cancel)); CHECK(frames.isEmpty()); cancel=false;
  CHECK(QDir(QDir::tempPath()).entryList({"pointless-frames-*"},QDir::Files)==cachedBefore);
  QFile original(mp4);CHECK(original.open(QIODevice::ReadOnly));const auto before=original.readAll();original.close();cancel=true;
  CHECK(!VideoIO::encodeFrames(src.size(),24,3,[&](int){return src;},mp4,err,cancel,progress));CHECK(original.open(QIODevice::ReadOnly));CHECK(original.readAll()==before);
 } else {std::fprintf(stderr,"ffmpeg missing: video checks unavailable\n");return 1;}
 QThreadPool::globalInstance()->waitForDone();std::puts("audit regressions passed");return 0;
}
