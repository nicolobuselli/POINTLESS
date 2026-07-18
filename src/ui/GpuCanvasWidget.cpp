#include "GpuCanvasWidget.h"
#include "../core/DotGridRenderer.h"
#include "../core/GridGenerator.h"

#include <rhi/qrhi.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMatrix4x4>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Fullscreen quad: x, y, u, v (two triangles). V=0 at the top-left so QImage
// row 0 lands at uv y 0; the clip-space correction matrix in each vertex
// shader normalizes backend Y conventions.
const float kQuad[] = {
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 0.0f,

     1.0f, -1.0f, 1.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f,
};

// std140 layout of composite.vert/frag's `buf` (see the shaders).
struct CompUbo {
    float mvp[16];
    float invPlacement[16];
    float sizes[4];
    qint32 modeFlags[4];   // x = BlendMode, y = gpuAdjust
    float adjA[4];         // brightness, contrast, gamma, saturation
    float adjB[4];         // levelsBlack, levelsMid, levelsWhite, grainAmp
    float adjC[4];         // posterize, threshold, invert, unused
};

QShader loadShader(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll())
                                       : QShader();
}

const char* backendName(QRhi::Implementation impl)
{
    switch (impl) {
        case QRhi::D3D11:  return "D3D11";
        case QRhi::D3D12:  return "D3D12";
        case QRhi::OpenGLES2: return "OpenGL";
        case QRhi::Vulkan: return "Vulkan";
        case QRhi::Metal:  return "Metal";
        case QRhi::Null:   return "Null";
    }
    return "Unknown";
}

void writeMat(float* dst, const QMatrix4x4& m)
{
    std::memcpy(dst, m.constData(), 16 * sizeof(float));
}

// std140 mirror of dot.vert's `buf`: 16 mvp floats + parameter block.
// Float offsets: 16 dims · 20 p0 · 24 p1 · 28 p2 · 32 shapeIds[8] ·
// 40 toneLevel[8] · 48 toneColor[8×4] · 80 locFieldC[4×4] · 96 locScale ·
// 100 locOn · 104 maskPts[5×4] · 124 grid0 · 128 grid1 · 132 gridT ·
// 136 gridD → 140 floats.
constexpr quint32 kDotUboFloats = 140;
constexpr quint32 kDotUboBytes  = kDotUboFloats * sizeof(float);

// Returns the instance count to draw (GridGpuLayout::count).
int fillDotParams(float* u, const GpuLayer& l)
{
    const DotGridSettings& d = l.dotSettings;
    const float w  = float(l.contentSize.width());
    const float h  = float(l.contentSize.height());
    const float sp = qMax(2.0f, d.grid.spacing);

    u[16] = w; u[17] = h; u[18] = sp;
    u[19] = (sp * 0.5f) * qMax(0.01f, d.grid.diameter);          // baseR
    u[20] = d.gamma; u[21] = d.weight; u[22] = d.jitter; u[23] = d.opacity;

    const bool imageColors = (d.tonal.mode == ToneMode::ImageColors);
    const int  nShapes = int(qMin<size_t>(d.shapes.size(), 8));
    const int  nTones  = imageColors ? 0 : int(qMin<size_t>(d.tonal.tones.size(), 8));
    u[24] = d.cornerRadius;
    u[25] = float(d.multiThreshold - 128);
    u[26] = float(qMax(1, nShapes));
    u[27] = float(nTones);
    u[28] = imageColors ? 1.0f : 0.0f;

    for (int i = 0; i < nShapes; ++i)
        u[32 + i] = DotGridRenderer::gpuShapeId(d.shapes[i].shape);
    for (int i = 0; i < nTones; ++i) {
        const ToneEntry& te = d.tonal.tones[i];
        u[40 + i] = float(te.level);
        u[48 + i * 4 + 0] = float(te.color.redF());
        u[48 + i * 4 + 1] = float(te.color.greenF());
        u[48 + i * 4 + 2] = float(te.color.blueF());
        u[48 + i * 4 + 3] = qBound(0.0f, te.opacity, 1.0f);
    }

    const LocField lf[4] = {
        locField(d.loc, LocParam::DgDiameter, w, h),
        locField(d.loc, LocParam::DgGamma,    w, h),
        locField(d.loc, LocParam::DgWeight,   w, h),
        locField(d.loc, LocParam::DgJitter,   w, h),
    };
    for (int i = 0; i < 4; ++i) {
        u[80 + i * 4 + 0] = lf[i].cx;
        u[80 + i * 4 + 1] = lf[i].cy;
        u[80 + i * 4 + 2] = lf[i].rIn;
        u[80 + i * 4 + 3] = lf[i].rOut;
        u[96 + i]  = lf[i].scale;
        u[100 + i] = lf[i].on ? 1.0f : 0.0f;
    }
    const LocMask lm = locMask(d.loc, w, h);
    const int nMask = int(qMin<size_t>(lm.pts.size(), 5));
    u[29] = float(nMask);
    for (int i = 0; i < nMask; ++i) {
        u[104 + i * 4 + 0] = lm.pts[i].cx;
        u[104 + i * 4 + 1] = lm.pts[i].cy;
        u[104 + i * 4 + 2] = lm.pts[i].rIn;
        u[104 + i * 4 + 3] = lm.pts[i].rOut;
    }

    // Grid layout: dot.vert rebuilds every sample position from
    // gl_InstanceIndex with these (GridGenerator::computeGpuLayout).
    const GridGpuLayout gl = GridGenerator::computeGpuLayout(
        d.grid, l.contentSize.width(), l.contentSize.height());
    float margin = sp;                                    // GridGenerator per-type margin
    if (d.grid.type == GridType::Wave)        margin = sp * 1.9f;   // sp + amp
    if (d.grid.type == GridType::Phyllotaxis) margin = sp * 0.8f;   // seed scale c
    u[124] = float(gl.type); u[125] = float(gl.i0);
    u[126] = float(gl.j0);   u[127] = float(gl.cols);
    u[128] = float(gl.ringN);
    u[129] = std::log2(sp);                               // mip lod ≈ cell box average
    u[130] = margin;
    u[132] = gl.m11; u[133] = gl.m12; u[134] = gl.m21; u[135] = gl.m22;
    u[136] = gl.dx;  u[137] = gl.dy;  u[138] = w * 0.5f; u[139] = h * 0.5f;
    return gl.count;
}

// std140 mirror of halftone.vert/.frag's `buf`: 16 mvp floats + 16 dims/pA/
// pB/pC + 4 paper + 32 scrA + 32 scrB + 32 inks = 132 floats. Screen setup
// mirrors HalftoneRenderer::render's job construction (KEEP IN SYNC).
constexpr quint32 kHalfUboFloats = 132;
constexpr quint32 kHalfUboBytes  = kHalfUboFloats * sizeof(float);

void fillHalftoneParams(float* u, const GpuLayer& l)
{
    const HalftoneSettings& p = l.halftoneSettings;
    const float sp = qMax(2.0f, p.spacing);
    u[16] = float(l.contentSize.width());
    u[17] = float(l.contentSize.height());
    u[18] = sp;
    u[19] = std::log2(sp);                     // mip lod ≈ cell box average
    u[20] = p.gamma; u[21] = p.softness; u[22] = p.gridNoise; u[23] = p.grain;
    u[24] = p.opacity;
    u[26] = float(int(p.dotShape));

    const QColor paper = p.paper.isValid() ? p.paper : QColor(Qt::white);
    u[32] = float(paper.redF()); u[33] = float(paper.greenF());
    u[34] = float(paper.blueF()); u[35] = 1.0f;

    const bool cmyk = (p.tonal.mode == ToneMode::ImageColors) || p.tonal.tones.empty();
    u[27] = cmyk ? 1.0f : 0.0f;
    int n = 0;
    if (cmyk) {
        n = 4;
        const float  angles[4] = { p.angleC, p.angleM, p.angleY, p.angleK };
        const QColor inks[4]   = { p.inkC, p.inkM, p.inkY, p.inkK };
        const float  floods[4] = { p.floodC, p.floodM, p.floodY, p.floodK };
        const float  gains[4]  = { p.gainC, p.gainM, p.gainY, p.gainK };
        for (int i = 0; i < 4; ++i) {
            u[36 + i * 4 + 0] = angles[i];
            u[36 + i * 4 + 1] = qBound(-1.0f, floods[i], 1.0f);
            u[36 + i * 4 + 2] = qBound(-1.0f, gains[i], 1.0f);
            u[100 + i * 4 + 0] = float(inks[i].redF());
            u[100 + i * 4 + 1] = float(inks[i].greenF());
            u[100 + i * 4 + 2] = float(inks[i].blueF());
            u[100 + i * 4 + 3] = float(inks[i].alphaF());
        }
    } else {
        std::vector<ToneEntry> tones = p.tonal.tones;
        std::sort(tones.begin(), tones.end(),
                  [](const ToneEntry& a, const ToneEntry& b) { return a.level < b.level; });
        n = int(qMin<size_t>(tones.size(), 8));
        const float angles[4] = { p.angleK, p.angleC, p.angleM, p.angleY };
        for (int i = 0; i < n; ++i) {
            u[36 + i * 4 + 0] = (i < 4) ? angles[i]
                              : std::fmod(p.angleK + float(i) * 37.5f, 180.0f);
            u[36 + i * 4 + 3] = float(tones[size_t(i)].level);
            u[68 + i * 4 + 0] = (i > 0)     ? float(tones[size_t(i) - 1].level) : -1.0f;
            u[68 + i * 4 + 1] = (i < n - 1) ? float(tones[size_t(i) + 1].level) : 256.0f;
            const QColor c = tones[size_t(i)].color;
            u[100 + i * 4 + 0] = float(c.redF());
            u[100 + i * 4 + 1] = float(c.greenF());
            u[100 + i * 4 + 2] = float(c.blueF());
            u[100 + i * 4 + 3] = qBound(0.0f, tones[size_t(i)].opacity, 1.0f);
        }
        u[28] = (n == 1) ? 1.0f : 0.0f;        // single tone: classic screen
    }
    u[25] = float(n);
}

// (layer id, raster size) → texture-cache key. Id lives alone in the high
// 32 bits so cleanup can recover it; w/h are well under 16 bits each.
quint64 layerTexKey(int id, const QSize& s)
{
    return (quint64(quint32(id)) << 32)
         | (quint64(quint32(s.width()) & 0xFFFFu) << 16)
         |  quint64(quint32(s.height()) & 0xFFFFu);
}

} // namespace

GpuCanvasWidget::GpuCanvasWidget(QWidget* parent)
    : QRhiWidget(parent)
{
}

GpuCanvasWidget::~GpuCanvasWidget() = default;

void GpuCanvasWidget::showImage(const QImage& img)
{
    m_image = img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    m_imageDirty = true;
    m_packageMode = false;
    update();
}

void GpuCanvasWidget::showPackage(const GpuFramePackage& pkg)
{
    m_pkg = pkg;
    m_packageMode = pkg.valid && !pkg.frame.isEmpty();
    update();
}

void GpuCanvasWidget::setViewRect(const QRectF& rectWidgetCoords)
{
    if (m_viewRect == rectWidgetCoords) return;
    m_viewRect = rectWidgetCoords;
    update();
}

void GpuCanvasWidget::initialize(QRhiCommandBuffer* cb)
{
    Q_UNUSED(cb);
    if (m_rhi != rhi()) {
        // New QRhi (first init or backend restart): drop every resource.
        m_blitPipeline.reset();
        m_compPipeline.reset();
        m_dotPipeline.reset();
        m_dotRes.clear();
        m_halfPipeline.reset();
        m_halfRes.clear();
        m_mipSampler.reset();
        m_dotRp.reset();
        m_blitSrb.reset();
        m_compSrbs.clear();
        m_compUbufs.clear();
        m_accumRt[0].reset(); m_accumRt[1].reset();
        m_accumRp.reset();
        m_accum[0].reset();  m_accum[1].reset();
        m_accumSize = {};
        m_layerTex.clear();
        m_imageTex.reset();
        m_vbuf.reset();
        m_blitUbuf.reset();
        m_dummyTex.reset();
        m_sampler.reset();
        m_rhi = rhi();
        m_vbufDirty = true;
        m_imageDirty = !m_image.isNull();
        m_frameTex.clear();
        m_srbSig.clear();
        m_blitSrbTex = nullptr;
    }

    if (!m_initialized && m_rhi) {
        QFile log(QDir(QCoreApplication::applicationDirPath()).filePath("gpu_spike.log"));
        if (log.open(QIODevice::WriteOnly | QIODevice::Text)) {
            log.write("QRhi backend: ");
            log.write(backendName(m_rhi->backend()));
            log.write("\ndriver: ");
            log.write(m_rhi->driverInfo().deviceName.constData());
            log.write("\n");
        }
        m_initialized = true;
    }

    if (m_blitPipeline)
        return;

    m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuad)));
    m_vbuf->create();

    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                      QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();

    // Mip-aware sampler for the halftone source (textureLod cell averages).
    m_mipSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                         QRhiSampler::Linear,
                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_mipSampler->create();

    m_blitUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    m_blitUbuf->create();

    // 1×1 placeholder so layout-only SRBs never hold a null texture.
    m_dummyTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_dummyTex->create();

    // The blit SRB is (re)built in render() once the present texture exists.
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { 4 * sizeof(float) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_blitPipeline.reset(m_rhi->newGraphicsPipeline());
    m_blitPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/blit.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/blit.frag.qsb")) },
    });
    m_blitPipeline->setVertexInputLayout(inputLayout);
    // Placeholder SRB purely for the pipeline's binding LAYOUT; the real SRB
    // (with live textures) is swapped in per frame — layouts match. Must stay
    // alive through pipeline->create().
    std::unique_ptr<QRhiShaderResourceBindings> layoutSrb(m_rhi->newShaderResourceBindings());
    layoutSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage, m_blitUbuf.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, m_dummyTex.get(), m_sampler.get()),
    });
    layoutSrb->create();
    m_blitPipeline->setShaderResourceBindings(layoutSrb.get());
    m_blitPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_blitPipeline->create();
}

void GpuCanvasWidget::ensureAccumTargets(const QSize& size)
{
    if (m_accumSize == size && m_accum[0])
        return;

    m_compPipeline.reset();
    m_accumRt[0].reset(); m_accumRt[1].reset();
    m_accumRp.reset();

    for (int i = 0; i < 2; ++i) {
        m_accum[i].reset(m_rhi->newTexture(QRhiTexture::RGBA8, size, 1,
                                           QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        m_accum[i]->create();
        m_accumRt[i].reset(m_rhi->newTextureRenderTarget({ m_accum[i].get() }));
    }
    m_accumRp.reset(m_accumRt[0]->newCompatibleRenderPassDescriptor());
    for (int i = 0; i < 2; ++i) {
        m_accumRt[i]->setRenderPassDescriptor(m_accumRp.get());
        m_accumRt[i]->create();
    }
    m_accumSize = size;

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { 4 * sizeof(float) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_compPipeline.reset(m_rhi->newGraphicsPipeline());
    m_compPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/composite.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/composite.frag.qsb")) },
    });
    m_compPipeline->setVertexInputLayout(inputLayout);
    if (m_compUbufs.empty()) {
        auto* b = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(CompUbo));
        b->create();
        m_compUbufs.emplace_back(b);
    }
    std::unique_ptr<QRhiShaderResourceBindings> layoutSrb(m_rhi->newShaderResourceBindings());
    layoutSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_compUbufs[0].get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, m_dummyTex.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(
            2, QRhiShaderResourceBinding::FragmentStage, m_dummyTex.get(), m_sampler.get()),
    });
    layoutSrb->create();
    m_compPipeline->setShaderResourceBindings(layoutSrb.get());
    m_compPipeline->setRenderPassDescriptor(m_accumRp.get());
    m_compPipeline->create();
}

void GpuCanvasWidget::ensureLayerTextures(QRhiResourceUpdateBatch* rub)
{
    // Upload changed/new layer rasters; textures live per (id, size) so the
    // fast and full-res variants of a layer coexist. Conversion to the upload
    // format only happens when an upload is actually due — the worker already
    // delivers RGBA8888_Premultiplied on the package path, so this is rare.
    QSet<int> aliveIds;
    m_frameTex.clear();
    for (const GpuLayer& l : m_pkg.layers) {
        aliveIds.insert(l.id);

        if (l.halftoneScreen) {
            // Uniform-driven halftone: fp16 linear source (with mips) in,
            // content texture out. Param drags only rewrite the UBO.
            HalfRes& hr = m_halfRes[l.id];
            if (!hr.tex || hr.size != l.contentSize) {
                hr.rt.reset();
                hr.tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, l.contentSize, 1,
                                               QRhiTexture::RenderTarget));
                hr.tex->create();
                hr.rt.reset(m_rhi->newTextureRenderTarget({ hr.tex.get() }));
                if (!m_dotRp) m_dotRp.reset(hr.rt->newCompatibleRenderPassDescriptor());
                hr.rt->setRenderPassDescriptor(m_dotRp.get());
                hr.rt->create();
                hr.size = l.contentSize;
            }
            bool srbDirty = false;
            if (!hr.srcTex || hr.srcTex->pixelSize() != l.image.size()) {
                hr.srcTex.reset(m_rhi->newTexture(QRhiTexture::RGBA16F, l.image.size(), 1,
                                                  QRhiTexture::MipMapped
                                                | QRhiTexture::UsedWithGenerateMips));
                hr.srcTex->create();
                hr.srcKey = -1;
                srbDirty  = true;
            }
            if (hr.srcKey != l.image.cacheKey()) {
                QRhiTextureSubresourceUploadDescription sd(
                    l.image.constBits(), quint32(l.image.sizeInBytes()));
                sd.setDataStride(quint32(l.image.bytesPerLine()));
                rub->uploadTexture(hr.srcTex.get(),
                                   QRhiTextureUploadDescription({ 0, 0, sd }));
                rub->generateMips(hr.srcTex.get());
                hr.srcKey = l.image.cacheKey();
            }
            if (!hr.ubo) {
                hr.ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
                                              QRhiBuffer::UniformBuffer, kHalfUboBytes));
                hr.ubo->create();
                srbDirty = true;
            }
            if (srbDirty || !hr.srb) {
                hr.srb.reset(m_rhi->newShaderResourceBindings());
                hr.srb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0, QRhiShaderResourceBinding::VertexStage
                         | QRhiShaderResourceBinding::FragmentStage, hr.ubo.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::FragmentStage,
                        hr.srcTex.get(), m_mipSampler.get()),
                });
                hr.srb->create();
            }
            float ubo[kHalfUboFloats] = {};
            writeMat(ubo, m_rhi->clipSpaceCorrMatrix());
            fillHalftoneParams(ubo, l);
            rub->updateDynamicBuffer(hr.ubo.get(), 0, kHalfUboBytes, ubo);
            m_frameTex.push_back(hr.tex.get());
            continue;
        }

        if (l.dotScreen) {
            // Uniform-driven Dot Grid: fp16 linear source (with mips) in,
            // content texture out. dot.vert reconstructs the lattice from
            // gl_InstanceIndex, so EVERY control drag only rewrites the UBO.
            DotRes& dr = m_dotRes[l.id];
            if (!dr.tex || dr.size != l.contentSize) {
                dr.rt.reset();
                dr.tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, l.contentSize, 1,
                                               QRhiTexture::RenderTarget));
                dr.tex->create();
                dr.rt.reset(m_rhi->newTextureRenderTarget({ dr.tex.get() }));
                if (!m_dotRp) m_dotRp.reset(dr.rt->newCompatibleRenderPassDescriptor());
                dr.rt->setRenderPassDescriptor(m_dotRp.get());
                dr.rt->create();
                dr.size = l.contentSize;
            }
            bool srbDirty = false;
            if (!dr.srcTex || dr.srcTex->pixelSize() != l.image.size()) {
                dr.srcTex.reset(m_rhi->newTexture(QRhiTexture::RGBA16F, l.image.size(), 1,
                                                  QRhiTexture::MipMapped
                                                | QRhiTexture::UsedWithGenerateMips));
                dr.srcTex->create();
                dr.srcKey = -1;
                srbDirty  = true;
            }
            if (dr.srcKey != l.image.cacheKey()) {
                QRhiTextureSubresourceUploadDescription sd(
                    l.image.constBits(), quint32(l.image.sizeInBytes()));
                sd.setDataStride(quint32(l.image.bytesPerLine()));
                rub->uploadTexture(dr.srcTex.get(),
                                   QRhiTextureUploadDescription({ 0, 0, sd }));
                rub->generateMips(dr.srcTex.get());
                dr.srcKey = l.image.cacheKey();
            }
            if (!dr.ubo) {
                dr.ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
                                              QRhiBuffer::UniformBuffer, kDotUboBytes));
                dr.ubo->create();
                srbDirty = true;
            }
            if (srbDirty || !dr.srb) {
                dr.srb.reset(m_rhi->newShaderResourceBindings());
                dr.srb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0, QRhiShaderResourceBinding::VertexStage, dr.ubo.get()),
                    QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::VertexStage,
                        dr.srcTex.get(), m_mipSampler.get()),
                });
                dr.srb->create();
            }
            QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix();
            mvp.ortho(0.0f, float(l.contentSize.width()),
                      float(l.contentSize.height()), 0.0f, -1.0f, 1.0f);
            float ubo[kDotUboFloats] = {};
            writeMat(ubo, mvp);
            dr.dotCount = fillDotParams(ubo, l);
            rub->updateDynamicBuffer(dr.ubo.get(), 0, kDotUboBytes, ubo);
            m_frameTex.push_back(dr.tex.get());
            continue;
        }

        LayerTex& lt = m_layerTex[layerTexKey(l.id, l.image.size())];
        if (!lt.tex) {
            lt.tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, l.image.size()));
            lt.tex->create();
            lt.key = -1;
        }
        if (lt.key != l.image.cacheKey()) {
            const QImage img = l.image.format() == QImage::Format_RGBA8888_Premultiplied
                             ? l.image
                             : l.image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            rub->uploadTexture(lt.tex.get(), img);
            lt.key = l.image.cacheKey();
        }
        m_frameTex.push_back(lt.tex.get());
    }
    for (auto it = m_layerTex.begin(); it != m_layerTex.end();) {
        if (!aliveIds.contains(int(it->first >> 32))) it = m_layerTex.erase(it);
        else ++it;
    }
    for (auto it = m_dotRes.begin(); it != m_dotRes.end();) {
        if (!aliveIds.contains(it->first)) it = m_dotRes.erase(it);
        else ++it;
    }
    for (auto it = m_halfRes.begin(); it != m_halfRes.end();) {
        if (!aliveIds.contains(it->first)) it = m_halfRes.erase(it);
        else ++it;
    }
}

void GpuCanvasWidget::ensureHalfPipeline()
{
    if (m_halfPipeline || !m_dotRp) return;
    QRhiShaderResourceBindings* layoutSrb = nullptr;
    for (auto& kv : m_halfRes)
        if (kv.second.srb) { layoutSrb = kv.second.srb.get(); break; }
    if (!layoutSrb) return;

    QRhiVertexInputLayout il;
    il.setBindings({ { 4 * sizeof(float) } });
    il.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_halfPipeline.reset(m_rhi->newGraphicsPipeline());
    m_halfPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/halftone.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/halftone.frag.qsb")) },
    });
    m_halfPipeline->setVertexInputLayout(il);   // opaque fullscreen write, no blend
    m_halfPipeline->setShaderResourceBindings(layoutSrb);
    m_halfPipeline->setRenderPassDescriptor(m_dotRp.get());
    m_halfPipeline->create();
}

void GpuCanvasWidget::ensureDotPipeline()
{
    if (m_dotPipeline || !m_dotRp) return;
    // Layout SRB: any per-layer dot SRB has the identical UBO+texture layout;
    // the map entries outlive create().
    QRhiShaderResourceBindings* layoutSrb = nullptr;
    for (auto& kv : m_dotRes)
        if (kv.second.srb) { layoutSrb = kv.second.srb.get(); break; }
    if (!layoutSrb) return;

    QRhiVertexInputLayout il;
    il.setBindings({ { 4 * sizeof(float) } });   // quad verts; instances are procedural
    il.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
    });

    m_dotPipeline.reset(m_rhi->newGraphicsPipeline());
    m_dotPipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(QStringLiteral(":/shaders/dot.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/dot.frag.qsb")) },
    });
    m_dotPipeline->setVertexInputLayout(il);
    QRhiGraphicsPipeline::TargetBlend tb;   // premultiplied over
    tb.enable   = true;
    tb.srcColor = QRhiGraphicsPipeline::One;
    tb.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    tb.srcAlpha = QRhiGraphicsPipeline::One;
    tb.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_dotPipeline->setTargetBlends({ tb });
    m_dotPipeline->setShaderResourceBindings(layoutSrb);
    m_dotPipeline->setRenderPassDescriptor(m_dotRp.get());
    m_dotPipeline->create();
}

void GpuCanvasWidget::rebuildCompositeSrbs()
{
    // Only rebuild when the resources the SRBs point at actually changed
    // (per-frame rebuild was pure churn — UBO pointers are stable, blend and
    // adjustments travel in the UBO, not the SRB).
    const int n = m_pkg.layers.size();
    std::vector<void*> sig;
    sig.reserve(size_t(n) + 2);
    sig.push_back(m_accum[0].get());
    sig.push_back(m_accum[1].get());
    for (QRhiTexture* t : m_frameTex) sig.push_back(t);
    if (sig == m_srbSig && int(m_compSrbs.size()) == n)
        return;

    while (int(m_compUbufs.size()) < n) {
        auto* b = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(CompUbo));
        b->create();
        m_compUbufs.emplace_back(b);
    }

    m_compSrbs.clear();
    for (int i = 0; i < n; ++i) {
        QRhiTexture* dst = m_accum[i & 1].get();
        QRhiTexture* src = m_frameTex[size_t(i)];
        auto* srb = m_rhi->newShaderResourceBindings();
        srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_compUbufs[size_t(i)].get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, dst, m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage, src, m_sampler.get()),
        });
        srb->create();
        m_compSrbs.emplace_back(srb);
    }
    m_srbSig = std::move(sig);
}

QMatrix4x4 GpuCanvasWidget::presentMatrix() const
{
    // Map the unit quad onto m_viewRect (logical widget coords → NDC).
    const double w = qMax(1, width());
    const double h = qMax(1, height());
    QRectF r = m_viewRect.isEmpty() ? QRectF(0, 0, w, h) : m_viewRect;

    const double sx = r.width()  / w;
    const double sy = r.height() / h;
    const double tx = 2.0 * r.center().x() / w - 1.0;
    const double ty = 1.0 - 2.0 * r.center().y() / h;   // NDC y up

    QMatrix4x4 m;
    m.translate(float(tx), float(ty));
    m.scale(float(sx), float(sy));
    return m_rhi->clipSpaceCorrMatrix() * m;
}

void GpuCanvasWidget::render(QRhiCommandBuffer* cb)
{
    if (!m_rhi || !m_blitPipeline)
        return;

    QRhiResourceUpdateBatch* rub = m_rhi->nextResourceUpdateBatch();
    if (m_vbufDirty) {
        rub->uploadStaticBuffer(m_vbuf.get(), kQuad);
        m_vbufDirty = false;
    }

    const QRhiCommandBuffer::VertexInput vbufBinding(m_vbuf.get(), 0);
    m_presentTex = nullptr;

    if (m_packageMode) {
        ensureAccumTargets(m_pkg.frame);
        ensureLayerTextures(rub);
        ensureDotPipeline();
        ensureHalfPipeline();
        rebuildCompositeSrbs();   // no-op unless textures/targets changed

        // All per-pass UBOs written up front in the same batch.
        const int n = m_pkg.layers.size();
        const QMatrix4x4 corr = m_rhi->clipSpaceCorrMatrix();
        for (int i = 0; i < n; ++i) {
            const GpuLayer& l = m_pkg.layers[i];
            CompUbo u;
            writeMat(u.mvp, corr);
            QMatrix4x4 inv(l.placement.inverted());   // frame px → layer px (3x3 → 4x4)
            writeMat(u.invPlacement, inv);
            u.sizes[0] = float(m_pkg.frame.width());
            u.sizes[1] = float(m_pkg.frame.height());
            u.sizes[2] = float(l.contentSize.width());
            u.sizes[3] = float(l.contentSize.height());
            u.modeFlags[0] = int(l.blend);
            u.modeFlags[1] = l.gpuAdjust ? 1 : 0;
            u.modeFlags[2] = u.modeFlags[3] = 0;
            const Adjustments& a = l.adj;   // shader expects "actual" values (see composite.frag)
            u.adjA[0] = float(a.brightness);
            u.adjA[1] = float(a.contrast);
            u.adjA[2] = float(a.gamma) / 100.0f;
            u.adjA[3] = float(qBound(0, 100 + a.saturation, 200));
            u.adjB[0] = float(a.levelsBlack);
            u.adjB[1] = float(a.levelsMid) / 100.0f;
            u.adjB[2] = float(a.levelsWhite);
            u.adjB[3] = float(a.grain) * 1.6f / 255.0f;
            u.adjC[0] = float(a.posterize);
            u.adjC[1] = float(a.threshold);
            u.adjC[2] = a.invert ? 1.0f : 0.0f;
            u.adjC[3] = 0.0f;
            rub->updateDynamicBuffer(m_compUbufs[size_t(i)].get(), 0, sizeof(CompUbo), &u);
        }

        // Content passes: rasterize instanced Dot Grid and halftone-screen
        // layers into their offscreen targets before compositing samples them.
        for (int i = 0; i < n; ++i) {
            const GpuLayer& l = m_pkg.layers[i];
            if (l.halftoneScreen) {
                auto hrIt = m_halfRes.find(l.id);
                if (hrIt == m_halfRes.end() || !hrIt->second.rt) continue;
                HalfRes& hr = hrIt->second;
                cb->beginPass(hr.rt.get(), Qt::transparent, { 1.0f, 0 }, rub);
                rub = nullptr;
                if (m_halfPipeline) {
                    cb->setGraphicsPipeline(m_halfPipeline.get());
                    cb->setViewport({ 0, 0, float(hr.size.width()), float(hr.size.height()) });
                    cb->setShaderResources(hr.srb.get());
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6);
                }
                cb->endPass();
                continue;
            }
            if (!l.dotScreen) continue;
            auto drIt = m_dotRes.find(l.id);
            if (drIt == m_dotRes.end() || !drIt->second.rt) continue;
            DotRes& dr = drIt->second;
            cb->beginPass(dr.rt.get(), Qt::transparent, { 1.0f, 0 }, rub);
            rub = nullptr;   // first pass consumes the shared update batch
            if (m_dotPipeline && dr.dotCount > 0) {
                cb->setGraphicsPipeline(m_dotPipeline.get());
                cb->setViewport({ 0, 0, float(dr.size.width()), float(dr.size.height()) });
                cb->setShaderResources(dr.srb.get());
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6, dr.dotCount);
            }
            cb->endPass();
        }

        // Pass 0: clear accum A to the (premultiplied) background colour.
        QColor bg = m_pkg.bg.isValid() ? m_pkg.bg : QColor(0, 0, 0, 0);
        const float ba = float(bg.alphaF());
        const QColor bgPremul = QColor::fromRgbF(float(bg.redF()) * ba,
                                                 float(bg.greenF()) * ba,
                                                 float(bg.blueF()) * ba, ba);
        cb->beginPass(m_accumRt[0].get(), bgPremul, { 1.0f, 0 }, rub);
        cb->endPass();
        rub = nullptr;

        // One fullscreen blend pass per layer, ping-ponging A↔B.
        for (int i = 0; i < n; ++i) {
            QRhiTextureRenderTarget* target = m_accumRt[(i + 1) & 1].get();
            cb->beginPass(target, Qt::transparent, { 1.0f, 0 });
            cb->setGraphicsPipeline(m_compPipeline.get());
            const QSize ts = target->pixelSize();
            cb->setViewport({ 0, 0, float(ts.width()), float(ts.height()) });
            cb->setShaderResources(m_compSrbs[size_t(i)].get());
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(6);
            cb->endPass();
        }

        m_presentTex = m_accum[n & 1].get();
    } else {
        if (m_imageDirty && !m_image.isNull()) {
            if (!m_imageTex || m_imageTex->pixelSize() != m_image.size()) {
                m_imageTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, m_image.size()));
                m_imageTex->create();
            }
            rub->uploadTexture(m_imageTex.get(), m_image);
            m_imageDirty = false;
        }
        m_presentTex = m_imageTex.get();
    }

    // Present pass: blit the result into the widget at viewRect.
    QMatrix4x4 mvp = presentMatrix();
    QRhiResourceUpdateBatch* rubP = rub ? rub : m_rhi->nextResourceUpdateBatch();
    rubP->updateDynamicBuffer(m_blitUbuf.get(), 0, 64, mvp.constData());

    if (m_presentTex && (m_presentTex != m_blitSrbTex || !m_blitSrb)) {
        m_blitSrb.reset(m_rhi->newShaderResourceBindings());
        m_blitSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, m_blitUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                m_presentTex, m_sampler.get()),
        });
        m_blitSrb->create();
        m_blitSrbTex = m_presentTex;
    }

    const QColor clear(0x1E, 0x1E, 0x1E);
    cb->beginPass(renderTarget(), clear, { 1.0f, 0 }, rubP);
    if (m_presentTex) {
        cb->setGraphicsPipeline(m_blitPipeline.get());
        const QSize out = renderTarget()->pixelSize();
        cb->setViewport({ 0, 0, float(out.width()), float(out.height()) });
        cb->setShaderResources(m_blitSrb.get());
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(6);
    }
    cb->endPass();
}
