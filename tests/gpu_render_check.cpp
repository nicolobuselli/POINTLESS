#include <QtWidgets>
#include <QtConcurrent>
#include "ui/GpuCanvasWidget.h"
#include "core/AsciiRenderer.h"
#include "core/DitherRenderer.h"
#include "core/MosaicRenderer.h"
#define private public
#include "workers/RenderWorker.h"
#undef private
#include <cstdio>
int main(int argc,char**argv) {
 qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& message){std::fprintf(stderr,"%s\n",qPrintable(message));});
 QApplication app(argc,argv);
 if (!QFile::exists(":/shaders/blit.vert.qsb")) { std::fprintf(stderr,"Shader resource missing\n"); return 1; }
 QWidget host;host.setAttribute(Qt::WA_ShowWithoutActivating);host.resize(64,64);
 GpuCanvasWidget canvas(&host);canvas.setGeometry(0,0,64,64);canvas.setFixedColorBufferSize(64,64);canvas.setViewRect(QRectF(0,0,64,64));
 QImage source(64,64,QImage::Format_ARGB32);source.fill(QColor(128,64,192));
 SessionParams params;params.frameW=params.frameH=64;params.layers.clear();params.background=QColor(128,128,128);params.backgroundOpacity=1;
 Layer layer;layer.id=1;layer.kind=LayerKind::Original;layer.blend=BlendMode::Multiply;params.layers.push_back(layer);
 RenderWorker renderer;canvas.showPackage(renderer.renderLayersPackage(source,params,{}));
 bool failed=false;QObject::connect(&canvas,&QRhiWidget::renderFailed,[&]{failed=true;});host.show();
 QElapsedTimer timer;timer.start();while(!canvas.isInitialized()&&!failed&&timer.elapsed()<10000){app.processEvents();QThread::msleep(10);}
 if(failed||!canvas.isInitialized()){std::puts("GPU backend unavailable; CPU tests remain mandatory");return 77;}
 const QImage gpu=canvas.grabFramebuffer();const auto cpu=RenderWorker::renderDocument(source,params);
 if(gpu.isNull()){std::puts("GPU framebuffer missing");return 1;}
 const auto actual=gpu.pixelColor(gpu.width()/2,gpu.height()/2),expected=cpu.pixelColor(32,32);
 const int error=qMax(qMax(qAbs(actual.red()-expected.red()),qAbs(actual.green()-expected.green())),qAbs(actual.blue()-expected.blue()));
 std::printf("GPU/CPU multiply RGB: %d,%d,%d / %d,%d,%d; max error=%d\n",actual.red(),actual.green(),actual.blue(),expected.red(),expected.green(),expected.blue(),error);
 if(error>3)return 1;
 params.layers[0].kind=LayerKind::Ascii;params.layers[0].blend=BlendMode::Normal;
 auto& ascii=params.layers[0].ascii;ascii.cellSize=12;
 ascii.tonal.mode=ToneMode::Palette;
 ascii.tonal.tones={{QColor(220,160,180),0},{QColor(80,110,25),85},{QColor(145,110,190),170},{QColor(255,40,132),255}};
 params.background=Qt::black;
 auto brightest=[](const QImage& image) {
  QColor best(Qt::black);int score=-1;
  for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
   QColor c=image.pixelColor(x,y);int v=c.red()+c.green()+c.blue();
   if(v>score){score=v;best=c;}
  }return best;
 };
 for(auto shape:{GridType::Square,GridType::Hexagonal}) for(auto color:{QColor(75,70,110),QColor(50,90,20),QColor(140,20,70)}) {
  ascii.gridShape=shape;source.fill(color);
  if(!AsciiRenderer::gpuRenderable(ascii))return 1;
  auto package=renderer.renderLayersPackage(source,params,{});
  if(package.layers.isEmpty() || !(package.layers[0].asciiScreen || package.layers[0].asciiInstanced))return 1;
  canvas.showPackage(package);
  app.processEvents();QImage actualFrame=canvas.grabFramebuffer();
  QColor a=brightest(actualFrame),b=brightest(RenderWorker::renderDocument(source,params));
  int delta=qMax(qMax(qAbs(a.red()-b.red()),qAbs(a.green()-b.green())),qAbs(a.blue()-b.blue()));
  std::printf("ASCII palette GPU/CPU grid=%d RGB %d,%d,%d / %d,%d,%d max error=%d\n",int(shape),a.red(),a.green(),a.blue(),b.red(),b.green(),b.blue(),delta);
  if(delta>4 || a==QColor(Qt::black))return 1;
 }
 ascii.tonal.tones.resize(9);if(AsciiRenderer::gpuRenderable(ascii))return 1;

 params.layers[0].kind=LayerKind::Dither;
 auto& dither=params.layers[0].dither;dither.algorithm=DitherAlgorithm::Bayer;dither.pixelSize=2;
 dither.tonal.mode=ToneMode::Palette;
 dither.tonal.tones={{QColor(18,40,90),0},{QColor(80,170,65),128},{QColor(230,75,145),255}};
 source.fill(QColor(80,170,65));
 if(!DitherRenderer::gpuRenderable(dither))return 1;
 auto ditherPackage=renderer.renderLayersPackage(source,params,{});
 if(ditherPackage.layers.isEmpty()||!ditherPackage.layers[0].ditherScreen)return 1;
 canvas.showPackage(ditherPackage);app.processEvents();
 QColor ditherGpu=canvas.grabFramebuffer().pixelColor(32,32);
 QColor ditherCpu=RenderWorker::renderDocument(source,params).pixelColor(32,32);
 int ditherDelta=qMax(qMax(qAbs(ditherGpu.red()-ditherCpu.red()),qAbs(ditherGpu.green()-ditherCpu.green())),qAbs(ditherGpu.blue()-ditherCpu.blue()));
 std::printf("Dither palette GPU/CPU max error=%d\n",ditherDelta);if(ditherDelta>4)return 1;
 dither.tonal.tones.resize(9);if(DitherRenderer::gpuRenderable(dither))return 1;

 params.layers[0].kind=LayerKind::Mosaic;
 auto& mosaic=params.layers[0].mosaic;mosaic.spacing=16;mosaic.widthPct=mosaic.heightPct=100;
 mosaic.tonal.mode=ToneMode::Palette;
 mosaic.tonal.tones={{QColor(18,40,90),0},{QColor(80,170,65),128},{QColor(230,75,145),255}};
 source.fill(QColor(80,170,65));
 if(!MosaicRenderer::gpuRenderable(mosaic))return 1;
 auto mosaicPackage=renderer.renderLayersPackage(source,params,{});
 if(mosaicPackage.layers.isEmpty()||!mosaicPackage.layers[0].mosaicScreen)return 1;
 canvas.showPackage(mosaicPackage);app.processEvents();
 QColor mosaicGpu=canvas.grabFramebuffer().pixelColor(32,32);
 QColor mosaicCpu=RenderWorker::renderDocument(source,params).pixelColor(32,32);
 int mosaicDelta=qMax(qMax(qAbs(mosaicGpu.red()-mosaicCpu.red()),qAbs(mosaicGpu.green()-mosaicCpu.green())),qAbs(mosaicGpu.blue()-mosaicCpu.blue()));
 std::printf("Mosaic palette GPU/CPU max error=%d\n",mosaicDelta);if(mosaicDelta>4)return 1;
 mosaic.tonal.tones.resize(9);if(MosaicRenderer::gpuRenderable(mosaic))return 1;
 return 0;
}
