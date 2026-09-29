#include "ProjectIO.h"
#include "FrameStore.h"

#include <QBuffer>
#include <QImageReader>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <algorithm>
#include <functional>

namespace {

// ── QColor ──────────────────────────────────────────────────
// Null JSON value for an invalid QColor (MosaicSettings::textColors uses
// invalid = "auto contrast"); HexArgb otherwise so alpha round-trips too.
QJsonValue colorToJson(const QColor& c) {
    return c.isValid() ? QJsonValue(c.name(QColor::HexArgb)) : QJsonValue();
}
QColor colorFromJson(const QJsonValue& v) {
    return v.isString() ? QColor(v.toString()) : QColor();
}

// ── Image ↔ base64 PNG ──────────────────────────────────────
QString imageToBase64(const QImage& img) {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "PNG")) return {};
    return QString::fromLatin1(bytes.toBase64());
}
QImage imageFromBase64(const QString& s, qint64 budget = 512LL * 1024 * 1024) {
    QByteArray bytes = QByteArray::fromBase64(s.toLatin1());
    QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    const QSize size = reader.size();
    if (size.isEmpty() || size.width() > 32768 || size.height() > 32768
        || qint64(size.width()) * size.height() * 4 > budget) return {};
    return reader.read();
}

// ── GridSettings ────────────────────────────────────────────
QJsonObject toJson(const GridSettings& g) {
    return {
        { "type", int(g.type) }, { "spacing", g.spacing },
        { "pointSpacing", g.pointSpacing }, { "rotation", g.rotation },
        { "diameter", g.diameter }, { "stretchFactor", g.stretchFactor },
        { "stretchAngle", g.stretchAngle },
        { "followGridRotation", g.followGridRotation },
    };
}
GridSettings gridFromJson(const QJsonObject& o) {
    GridSettings g;
    g.type               = GridType(o["type"].toInt(int(g.type)));
    g.spacing             = float(o["spacing"].toDouble(g.spacing));
    g.pointSpacing        = float(o["pointSpacing"].toDouble(g.pointSpacing));
    g.rotation            = float(o["rotation"].toDouble(g.rotation));
    g.diameter            = float(o["diameter"].toDouble(g.diameter));
    g.stretchFactor       = float(o["stretchFactor"].toDouble(g.stretchFactor));
    g.stretchAngle        = float(o["stretchAngle"].toDouble(g.stretchAngle));
    g.followGridRotation  = o["followGridRotation"].toBool(g.followGridRotation);
    return g;
}

// ── ToneEntry / TonalSettings ───────────────────────────────
QJsonObject toJson(const ToneEntry& t) {
    return { { "color", colorToJson(t.color) }, { "level", t.level }, { "opacity", t.opacity },
             { "flood", t.flood }, { "gain", t.gain } };
}
ToneEntry toneFromJson(const QJsonObject& o) {
    ToneEntry t;
    t.color   = colorFromJson(o["color"]);
    t.level   = o["level"].toInt(t.level);
    t.opacity = float(o["opacity"].toDouble(t.opacity));
    t.flood   = float(o["flood"].toDouble(t.flood));
    t.gain    = float(o["gain"].toDouble(t.gain));
    return t;
}
QJsonObject toJson(const TonalSettings& t) {
    QJsonArray tones;
    for (const ToneEntry& e : t.tones) tones.append(toJson(e));
    return { { "mode", int(t.mode) }, { "tones", tones }, { "enabled", t.enabled } };
}
TonalSettings tonalFromJson(const QJsonObject& o) {
    TonalSettings t;
    t.mode = ToneMode(o["mode"].toInt(int(t.mode)));
    t.tones.clear();
    for (const QJsonValue& v : o["tones"].toArray()) t.tones.push_back(toneFromJson(v.toObject()));
    t.enabled = o["enabled"].toBool(t.enabled);
    return t;
}

// ── LocMap (keyed by int(LocParam) as a string) ─────────────
QJsonObject toJson(const LocMap& m) {
    QJsonObject o;
    for (const auto& [p, pt] : m) {
        o[QString::number(int(p))] = QJsonObject{
            { "enabled", pt.enabled }, { "posX", pt.posX }, { "posY", pt.posY },
            { "rotation", pt.rotation }, { "scale", pt.scale },
            { "radius", pt.radius }, { "falloff", pt.falloff },
        };
    }
    return o;
}
LocMap locMapFromJson(const QJsonObject& o) {
    LocMap m;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        bool ok = false;
        const int pi = it.key().toInt(&ok);
        if (!ok || pi < 0 || pi >= int(LocParam::Count)) continue;
        const QJsonObject po = it.value().toObject();
        LocPoint pt;
        pt.enabled  = po["enabled"].toBool(pt.enabled);
        pt.posX     = float(po["posX"].toDouble(pt.posX));
        pt.posY     = float(po["posY"].toDouble(pt.posY));
        pt.rotation = float(po["rotation"].toDouble(pt.rotation));
        pt.scale    = float(po["scale"].toDouble(pt.scale));
        pt.radius   = float(po["radius"].toDouble(pt.radius));
        pt.falloff  = float(po["falloff"].toDouble(pt.falloff));
        m[LocParam(pi)] = pt;
    }
    return m;
}

// ── DotGridSettings ─────────────────────────────────────────
QJsonObject toJson(const DotGridSettings& s) {
    QJsonArray shapes;
    for (const ShapeEntry& sh : s.shapes)
        shapes.append(QJsonObject{ { "shape", int(sh.shape) }, { "svgPath", sh.svgPath } });
    return {
        { "inputDpi", s.inputDpi }, { "shapes", shapes },
        { "multiThreshold", s.multiThreshold }, { "grid", toJson(s.grid) },
        { "gamma", s.gamma }, { "weight", s.weight }, { "jitter", s.jitter },
        { "opacity", s.opacity }, { "cornerRadius", s.cornerRadius },
        { "loc", toJson(s.loc) }, { "tonal", toJson(s.tonal) },
    };
}
DotGridSettings dotGridFromJson(const QJsonObject& o) {
    DotGridSettings s;
    s.inputDpi = o["inputDpi"].toInt(s.inputDpi);
    s.shapes.clear();
    for (const QJsonValue& v : o["shapes"].toArray()) {
        const QJsonObject so = v.toObject();
        s.shapes.push_back({ DotGridShape(so["shape"].toInt()), so["svgPath"].toString() });
    }
    if (s.shapes.empty()) s.shapes = { ShapeEntry{} };
    s.multiThreshold = o["multiThreshold"].toInt(s.multiThreshold);
    s.grid           = gridFromJson(o["grid"].toObject());
    s.gamma          = float(o["gamma"].toDouble(s.gamma));
    s.weight         = float(o["weight"].toDouble(s.weight));
    s.jitter         = float(o["jitter"].toDouble(s.jitter));
    s.opacity        = float(o["opacity"].toDouble(s.opacity));
    s.cornerRadius   = float(o["cornerRadius"].toDouble(s.cornerRadius));
    s.loc            = locMapFromJson(o["loc"].toObject());
    s.tonal          = tonalFromJson(o["tonal"].toObject());
    return s;
}

// ── DitherSettings ───────────────────────────────────────────
QJsonObject toJson(const DitherSettings& s) {
    return {
        { "algorithm", int(s.algorithm) }, { "bayerSize", s.bayerSize },
        { "pixelSize", s.pixelSize }, { "strength", s.strength },
        { "threshold", s.threshold }, { "opacity", s.opacity },
        { "cornerRadius", s.cornerRadius }, { "levels", s.levels },
        { "serpentine", s.serpentine }, { "lineAngle", s.lineAngle },
        { "lineSpacing", s.lineSpacing }, { "patternPath", s.patternPath },
        { "loc", toJson(s.loc) }, { "tonal", toJson(s.tonal) },
    };
}
DitherSettings ditherFromJson(const QJsonObject& o) {
    DitherSettings s;
    s.algorithm    = DitherAlgorithm(o["algorithm"].toInt(int(s.algorithm)));
    s.bayerSize    = o["bayerSize"].toInt(s.bayerSize);
    s.pixelSize    = o["pixelSize"].toInt(s.pixelSize);
    s.strength     = o["strength"].toInt(s.strength);
    s.threshold    = o["threshold"].toInt(s.threshold);
    s.opacity      = float(o["opacity"].toDouble(s.opacity));
    s.cornerRadius = float(o["cornerRadius"].toDouble(s.cornerRadius));
    s.levels       = o["levels"].toInt(s.levels);
    s.serpentine   = o["serpentine"].toBool(s.serpentine);
    s.lineAngle    = float(o["lineAngle"].toDouble(s.lineAngle));
    s.lineSpacing  = o["lineSpacing"].toInt(s.lineSpacing);
    s.patternPath  = o["patternPath"].toString(s.patternPath);
    s.loc          = locMapFromJson(o["loc"].toObject());
    s.tonal        = tonalFromJson(o["tonal"].toObject());
    return s;
}

// ── AsciiSettings ────────────────────────────────────────────
QJsonObject toJson(const AsciiSettings& s) {
    return {
        { "charsetPreset", s.charsetPreset }, { "customCharset", s.customCharset },
        { "cellSize", s.cellSize }, { "gridShape", int(s.gridShape) },
        { "gamma", s.gamma }, { "fontFamily", s.fontFamily },
        { "fontWeight", s.fontWeight }, { "edges", s.edges },
        { "stipple", s.stipple }, { "orderedDither", s.orderedDither },
        { "contour", s.contour }, { "hatching", s.hatching },
        { "opacity", s.opacity }, { "loc", toJson(s.loc) }, { "tonal", toJson(s.tonal) },
    };
}
AsciiSettings asciiFromJson(const QJsonObject& o) {
    AsciiSettings s;
    s.charsetPreset  = o["charsetPreset"].toInt(s.charsetPreset);
    s.customCharset  = o["customCharset"].toString(s.customCharset);
    s.cellSize       = o["cellSize"].toInt(s.cellSize);
    s.gridShape      = GridType(o["gridShape"].toInt(int(s.gridShape)));
    s.gamma          = float(o["gamma"].toDouble(s.gamma));
    s.fontFamily     = o["fontFamily"].toString(s.fontFamily);
    s.fontWeight     = o["fontWeight"].toInt(s.fontWeight);
    s.edges          = o["edges"].toInt(s.edges);
    s.stipple        = o["stipple"].toInt(s.stipple);
    s.orderedDither  = o["orderedDither"].toBool(s.orderedDither);
    s.contour        = o["contour"].toInt(s.contour);
    s.hatching       = o["hatching"].toInt(s.hatching);
    s.opacity        = float(o["opacity"].toDouble(s.opacity));
    s.loc            = locMapFromJson(o["loc"].toObject());
    s.tonal          = tonalFromJson(o["tonal"].toObject());
    return s;
}

// ── MosaicSettings ───────────────────────────────────────────
QJsonObject toJson(const MosaicSettings& s) {
    QJsonArray texts;
    for (const QString& t : s.texts) texts.append(t);
    QJsonArray textColors;
    for (const QColor& c : s.textColors) textColors.append(colorToJson(c));
    return {
        { "spacing", s.spacing }, { "widthPct", s.widthPct }, { "heightPct", s.heightPct },
        { "gridShape", int(s.gridShape) }, { "gridRotation", s.gridRotation },
        { "textPadding", s.textPadding }, { "fontFamily", s.fontFamily },
        { "fontWeight", s.fontWeight }, { "opacity", s.opacity },
        { "cornerRadius", s.cornerRadius }, { "texts", texts }, { "textColors", textColors },
        { "loc", toJson(s.loc) }, { "tonal", toJson(s.tonal) },
    };
}
MosaicSettings mosaicFromJson(const QJsonObject& o) {
    MosaicSettings s;
    s.spacing      = float(o["spacing"].toDouble(s.spacing));
    s.widthPct     = float(o["widthPct"].toDouble(s.widthPct));
    s.heightPct    = float(o["heightPct"].toDouble(s.heightPct));
    s.gridShape    = GridType(o["gridShape"].toInt(int(s.gridShape)));
    s.gridRotation = float(o["gridRotation"].toDouble(s.gridRotation));
    s.textPadding  = o["textPadding"].toInt(s.textPadding);
    s.fontFamily   = o["fontFamily"].toString(s.fontFamily);
    s.fontWeight   = o["fontWeight"].toInt(s.fontWeight);
    s.opacity      = float(o["opacity"].toDouble(s.opacity));
    s.cornerRadius = float(o["cornerRadius"].toDouble(s.cornerRadius));
    s.texts.clear();
    for (const QJsonValue& v : o["texts"].toArray()) s.texts.push_back(v.toString());
    s.textColors.clear();
    for (const QJsonValue& v : o["textColors"].toArray()) s.textColors.push_back(colorFromJson(v));
    s.loc   = locMapFromJson(o["loc"].toObject());
    s.tonal = tonalFromJson(o["tonal"].toObject());
    return s;
}

// ── HalftoneSettings (canonical CMYK screen) ─────────────────
QJsonObject toJson(const HalftoneSettings& s) {
    return {
        { "spacing", s.spacing },
        { "angleC", s.angleC }, { "angleM", s.angleM },
        { "angleY", s.angleY }, { "angleK", s.angleK },
        { "dotShape", int(s.dotShape) },
        { "gamma", s.gamma }, { "opacity", s.opacity },
        { "softness", s.softness }, { "gridNoise", s.gridNoise },
        { "grain", s.grain },
        { "tonal", toJson(s.tonal) },
        { "inkC", colorToJson(s.inkC) }, { "inkM", colorToJson(s.inkM) },
        { "inkY", colorToJson(s.inkY) }, { "inkK", colorToJson(s.inkK) },
        { "paper", colorToJson(s.paper) },
        { "floodC", s.floodC }, { "floodM", s.floodM },
        { "floodY", s.floodY }, { "floodK", s.floodK },
        { "gainC", s.gainC }, { "gainM", s.gainM },
        { "gainY", s.gainY }, { "gainK", s.gainK },
    };
}
HalftoneSettings halftoneFromJson(const QJsonObject& o) {
    HalftoneSettings s;
    s.spacing  = float(o["spacing"].toDouble(s.spacing));
    s.angleC   = float(o["angleC"].toDouble(s.angleC));
    s.angleM   = float(o["angleM"].toDouble(s.angleM));
    s.angleY   = float(o["angleY"].toDouble(s.angleY));
    s.angleK   = float(o["angleK"].toDouble(s.angleK));
    s.dotShape = ScreenDotShape(o["dotShape"].toInt(int(s.dotShape)));
    s.gamma    = float(o["gamma"].toDouble(s.gamma));
    s.opacity  = float(o["opacity"].toDouble(s.opacity));
    s.softness = float(o["softness"].toDouble(s.softness));
    s.gridNoise = float(o["gridNoise"].toDouble(s.gridNoise));
    s.grain     = float(o["grain"].toDouble(s.grain));
    // Guarded: older halftoneAm objects have no "tonal" — keep the
    // ImageColors (CMYK) default instead of a zero-tone FixedTones.
    if (o.contains("tonal")) s.tonal = tonalFromJson(o["tonal"].toObject());
    if (o.contains("inkC"))  s.inkC  = colorFromJson(o["inkC"]);
    if (o.contains("inkM"))  s.inkM  = colorFromJson(o["inkM"]);
    if (o.contains("inkY"))  s.inkY  = colorFromJson(o["inkY"]);
    if (o.contains("inkK"))  s.inkK  = colorFromJson(o["inkK"]);
    if (o.contains("paper")) s.paper = colorFromJson(o["paper"]);
    s.floodC = float(o["floodC"].toDouble(s.floodC));
    s.floodM = float(o["floodM"].toDouble(s.floodM));
    s.floodY = float(o["floodY"].toDouble(s.floodY));
    s.floodK = float(o["floodK"].toDouble(s.floodK));
    s.gainC  = float(o["gainC"].toDouble(s.gainC));
    s.gainM  = float(o["gainM"].toDouble(s.gainM));
    s.gainY  = float(o["gainY"].toDouble(s.gainY));
    s.gainK  = float(o["gainK"].toDouble(s.gainK));
    return s;
}

// ── Adjustments / LayerTransform ─────────────────────────────
QJsonObject toJson(const Adjustments& a) {
    return {
        { "brightness", a.brightness }, { "contrast", a.contrast }, { "gamma", a.gamma },
        { "levelsBlack", a.levelsBlack }, { "levelsMid", a.levelsMid }, { "levelsWhite", a.levelsWhite },
        { "saturation", a.saturation }, { "sizePct", a.sizePct },
        { "sharpenStrength", a.sharpenStrength }, { "sharpenRadius", a.sharpenRadius },
        { "edgeEnhancement", a.edgeEnhancement }, { "invert", a.invert },
        { "blur", a.blur }, { "grain", a.grain }, { "posterize", a.posterize },
        { "threshold", a.threshold },
    };
}
Adjustments adjFromJson(const QJsonObject& o) {
    Adjustments a;
    a.brightness      = o["brightness"].toInt(a.brightness);
    a.contrast        = o["contrast"].toInt(a.contrast);
    a.gamma           = o["gamma"].toInt(a.gamma);
    a.levelsBlack     = o["levelsBlack"].toInt(a.levelsBlack);
    a.levelsMid       = o["levelsMid"].toInt(a.levelsMid);
    a.levelsWhite     = o["levelsWhite"].toInt(a.levelsWhite);
    a.saturation      = o["saturation"].toInt(a.saturation);
    a.sizePct         = o["sizePct"].toInt(a.sizePct);
    a.sharpenStrength = o["sharpenStrength"].toInt(a.sharpenStrength);
    a.sharpenRadius   = o["sharpenRadius"].toInt(a.sharpenRadius);
    a.edgeEnhancement = o["edgeEnhancement"].toInt(a.edgeEnhancement);
    a.invert          = o["invert"].toBool(a.invert);
    a.blur            = o["blur"].toInt(a.blur);
    a.grain           = o["grain"].toInt(a.grain);
    a.posterize       = o["posterize"].toInt(a.posterize);
    a.threshold       = o["threshold"].toInt(a.threshold);
    return a;
}
QJsonObject toJson(const LayerTransform& t) {
    return {
        { "xPct", t.xPct }, { "yPct", t.yPct }, { "scalePct", t.scalePct },
        { "aspectPct", t.aspectPct },
        { "rotation", t.rotation }, { "flipH", t.flipH }, { "flipV", t.flipV },
    };
}
LayerTransform transformFromJson(const QJsonObject& o) {
    LayerTransform t;
    t.xPct     = float(o["xPct"].toDouble(t.xPct));
    t.yPct     = float(o["yPct"].toDouble(t.yPct));
    t.scalePct = float(o["scalePct"].toDouble(t.scalePct));
    t.aspectPct = float(o["aspectPct"].toDouble(t.aspectPct));
    t.rotation = float(o["rotation"].toDouble(t.rotation));
    t.flipH    = o["flipH"].toBool(t.flipH);
    t.flipV    = o["flipV"].toBool(t.flipV);
    return t;
}

// ── Layer ────────────────────────────────────────────────────
QJsonObject toJson(const Layer& l) {
    return {
        { "id", l.id }, { "kind", int(l.kind) }, { "name", l.name },
        { "visible", l.visible }, { "pinned", l.pinned }, { "locked", l.locked },
        { "blend", int(l.blend) }, { "opacity", l.opacity }, { "mediaId", l.mediaId },
        { "transform", toJson(l.transform) }, { "adjustments", toJson(l.adjustments) },
        // "halftone" is the FROZEN legacy JSON key for the Dot Grid mode
        // (pre-rename .ultra files; no migration mechanism exists).
        { "halftone", toJson(l.dotGrid) }, { "dither", toJson(l.dither) },
        { "ascii", toJson(l.ascii) }, { "mosaic", toJson(l.mosaic) },
        { "halftoneAm", toJson(l.halftone) },
    };
}
Layer layerFromJson(const QJsonObject& o) {
    Layer l;
    l.id      = o["id"].toInt(l.id);
    l.kind    = LayerKind(o["kind"].toInt(int(l.kind)));
    l.name    = o["name"].toString(l.name);
    l.visible = o["visible"].toBool(l.visible);
    l.pinned  = o["pinned"].toBool(l.pinned);
    l.locked  = o["locked"].toBool(l.locked);
    l.blend   = BlendMode(o["blend"].toInt(int(l.blend)));
    l.opacity = float(o["opacity"].toDouble(l.opacity));
    l.mediaId = o["mediaId"].toInt(l.mediaId);
    l.transform   = transformFromJson(o["transform"].toObject());
    l.adjustments = adjFromJson(o["adjustments"].toObject());
    l.dotGrid    = dotGridFromJson(o["halftone"].toObject());
    l.halftone    = halftoneFromJson(o["halftoneAm"].toObject());
    l.dither      = ditherFromJson(o["dither"].toObject());
    l.ascii       = asciiFromJson(o["ascii"].toObject());
    l.mosaic      = mosaicFromJson(o["mosaic"].toObject());
    return l;
}

// ── ParentGroup ──────────────────────────────────────────────
QJsonObject toJson(const ParentGroup& p) {
    return {
        { "mediaId", p.mediaId }, { "name", p.name },
        { "collapsed", p.collapsed }, { "groupVisible", p.groupVisible },
        { "trimIn", p.trimIn }, { "trimOut", p.trimOut }, { "timeOffset", p.timeOffset },
    };
}
ParentGroup parentFromJson(const QJsonObject& o) {
    ParentGroup p;
    p.mediaId      = o["mediaId"].toInt(p.mediaId);
    p.name         = o["name"].toString(p.name);
    p.collapsed    = o["collapsed"].toBool(p.collapsed);
    p.groupVisible = o["groupVisible"].toBool(p.groupVisible);
    p.trimIn = o["trimIn"].toInt(p.trimIn);
    p.trimOut = o["trimOut"].toInt(p.trimOut);
    p.timeOffset = o["timeOffset"].toInt(p.timeOffset);
    return p;
}

// ── SessionParams ────────────────────────────────────────────
QJsonObject toJson(const SessionParams& p) {
    QJsonArray layers;
    for (const Layer& l : p.layers) layers.append(toJson(l));
    QJsonArray parents;
    for (const ParentGroup& g : p.parents) parents.append(toJson(g));
    return {
        { "layers", layers }, { "parents", parents },
        { "activeLayerId", p.activeLayerId }, { "nextLayerId", p.nextLayerId },
        { "background", colorToJson(p.background) }, { "backgroundOpacity", p.backgroundOpacity },
        { "frameW", p.frameW }, { "frameH", p.frameH },
    };
}
SessionParams sessionFromJson(const QJsonObject& o) {
    SessionParams p;
    p.layers.clear();
    for (const QJsonValue& v : o["layers"].toArray()) p.layers.push_back(layerFromJson(v.toObject()));
    p.parents.clear();
    for (const QJsonValue& v : o["parents"].toArray()) p.parents.push_back(parentFromJson(v.toObject()));
    p.activeLayerId = o["activeLayerId"].toInt(p.activeLayerId);
    p.nextLayerId   = o["nextLayerId"].toInt(p.nextLayerId);
    p.background    = colorFromJson(o["background"]);
    if (!p.background.isValid()) p.background = QColor(0x0A, 0x0A, 0x0A);
    p.backgroundOpacity = float(o["backgroundOpacity"].toDouble(p.backgroundOpacity));
    p.frameW = o["frameW"].toInt(p.frameW);
    p.frameH = o["frameH"].toInt(p.frameH);
    return p;
}

// ── Animation ────────────────────────────────────────────────
QJsonObject toJson(const Keyframe& k) {
    return { { "frame", k.frame }, { "value", k.value }, { "easing", int(k.easing) } };
}
Keyframe keyFromJson(const QJsonObject& o) {
    Keyframe k;
    k.frame  = o["frame"].toInt(k.frame);
    k.value  = o["value"].toDouble(k.value);
    k.easing = Easing(o["easing"].toInt(int(k.easing)));
    return k;
}
QJsonObject toJson(const Track& t) {
    QJsonArray keys;
    for (const Keyframe& k : t.keys) keys.append(toJson(k));
    return { { "layerId", t.layerId }, { "param", int(t.param) }, { "keys", keys } };
}
Track trackFromJson(const QJsonObject& o) {
    Track t;
    t.layerId = o["layerId"].toInt(t.layerId);
    t.param   = ParamId(o["param"].toInt(int(t.param)));
    t.keys.clear();
    for (const QJsonValue& v : o["keys"].toArray()) t.keys.push_back(keyFromJson(v.toObject()));
    return t;
}
QJsonObject toJson(const Animation& a) {
    QJsonArray tracks;
    for (const Track& t : a.tracks) tracks.append(toJson(t));
    return {
        { "tracks", tracks }, { "frameStart", a.frameStart },
        { "frameEnd", a.frameEnd }, { "fps", a.fps }, { "stepFps", a.stepFps },
    };
}
Animation animFromJson(const QJsonObject& o) {
    Animation a;
    a.tracks.clear();
    for (const QJsonValue& v : o["tracks"].toArray()) a.tracks.push_back(trackFromJson(v.toObject()));
    a.frameStart = o["frameStart"].toInt(a.frameStart);
    a.frameEnd   = o["frameEnd"].toInt(a.frameEnd);
    a.fps        = o["fps"].toInt(a.fps);
    a.stepFps    = o["stepFps"].toInt(a.stepFps);
    return a;
}

} // namespace

namespace ProjectIO {

bool save(const QString& path, const ProjectData& data, QString* error, std::atomic_bool* cancel)
{
    if (data.media.size() > 256 || data.params.layers.size() > 256) { if (error) *error = "Projects support at most 256 sources and 256 layers."; return false; }
    QJsonObject media;
    qint64 sourceBytes = 0;
    qint64 encodedBytes = 0;
    for (auto it = data.media.cbegin(); it != data.media.cend(); ++it) {
        if (cancel && cancel->load()) { if (error) *error = "Canceled."; return false; }
        sourceBytes += it.value().frames.isEmpty() ? it.value().image.sizeInBytes() : 0;
        QJsonArray frames;
        for (const auto& frame : it.value().frames) {
            sourceBytes += FrameStore::ownedBytes(frame);
            if (sourceBytes > 512LL * 1024 * 1024 || (cancel && cancel->load())) { if (error) *error = "Canceled or sources exceed 512 MiB."; return false; }
            const QString encoded = imageToBase64(frame);
            if (encoded.isEmpty()) { if (error) *error = "Could not encode a video frame."; return false; }
            encodedBytes += encoded.size();
            if (encodedBytes > 768LL * 1024 * 1024) { if (error) *error = "Project exceeds 768 MiB."; return false; }
            frames.append(encoded);
        }
        if (sourceBytes > 512LL * 1024 * 1024) { if (error) *error = "Sources exceed 512 MiB."; return false; }
        const QString png = imageToBase64(it.value().image);
        if (png.isEmpty()) {
            if (error) *error = "Could not encode a source image.";
            return false;
        }
        encodedBytes += png.size();
        if (encodedBytes > 768LL * 1024 * 1024) { if (error) *error = "Project exceeds 768 MiB."; return false; }
        media[QString::number(it.key())] = QJsonObject{
            { "name", it.value().name }, { "png", png },
            { "frames", frames }, { "fps", it.value().fps },
        };
    }

    QJsonObject root{
        { "formatVersion", 2 }, { "title", data.title },
        { "params", toJson(data.params) }, { "animation", toJson(data.anim) },
        { "media", media },
    };

    QJsonObject assets;
    std::function<bool(const QJsonValue&)> collect;
    collect = [&](const QJsonValue& value) {
        if (value.isArray()) { for (const auto& v : value.toArray()) if (!collect(v)) return false; }
        if (value.isObject()) {
            const auto object = value.toObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                if ((it.key() == "svgPath" || it.key() == "patternPath") && !it.value().toString().isEmpty()) {
                    QFile asset(it.value().toString());
                    if (!asset.open(QIODevice::ReadOnly) || asset.size() > 16 * 1024 * 1024) return false;
                    assets[it.value().toString()] = QString::fromLatin1(asset.readAll().toBase64());
                } else if (!collect(it.value())) return false;
            }
        }
        return true;
    };
    if (!collect(root["params"])) { if (error) *error = "A custom SVG or pattern is missing or exceeds 16 MiB."; return false; }
    root["embeddedAssets"] = assets;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > 768LL * 1024 * 1024 || (cancel && cancel->load())) { if (error) *error = "Canceled or project exceeds 768 MiB."; return false; }
    if (f.write(bytes) != bytes.size() || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool load(const QString& path, ProjectData* out, QString* error, std::atomic_bool* cancel)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    if (f.size() > 768LL * 1024 * 1024) {
        if (error) *error = "Project file exceeds the 768 MiB limit.";
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = perr.errorString();
        return false;
    }

    auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (!out) return fail("No destination document.");
    QJsonObject root = doc.object();
    if (root["formatVersion"].toInt(-1) != 1 && root["formatVersion"].toInt(-1) != 2)
        return fail("Unsupported or missing project version.");
    if (!root["params"].isObject() || !root["animation"].isObject() || !root["media"].isObject())
        return fail("Incomplete project: params, animation and media are required.");
    // Resolve embedded paths into a process-owned temporary cache. Filenames
    // come from content hashes, never from paths supplied by the project.
    static QTemporaryDir assetCache;
    const auto embedded = root["embeddedAssets"].toObject();
    if (embedded.size() > 256) return fail("Too many embedded assets.");
    QHash<QString, QString> resolved;
    for (auto it = embedded.begin(); it != embedded.end(); ++it) {
        const QByteArray bytes = QByteArray::fromBase64(it.value().toString().toLatin1());
        if (bytes.isEmpty() || bytes.size() > 16 * 1024 * 1024 || !assetCache.isValid()) return fail("Invalid embedded asset.");
        const QString name = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
            + (it.key().endsWith(".svg", Qt::CaseInsensitive) ? ".svg" : ".img");
        const QString assetPath = assetCache.filePath(name);
        QSaveFile asset(assetPath);
        if (!asset.open(QIODevice::WriteOnly) || asset.write(bytes) != bytes.size() || !asset.commit()) return fail("Cannot restore embedded asset.");
        resolved.insert(it.key(), assetPath);
    }
    std::function<QJsonValue(QJsonValue)> resolve;
    resolve = [&](QJsonValue value) -> QJsonValue {
        if (value.isArray()) { auto array = value.toArray(); for (int i = 0; i < array.size(); ++i) array[i] = resolve(array[i]); return array; }
        if (value.isObject()) {
            auto object = value.toObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                if ((it.key() == "svgPath" || it.key() == "patternPath") && resolved.contains(it.value().toString())) it.value() = resolved.value(it.value().toString());
                else it.value() = resolve(it.value());
            }
            return object;
        }
        return value;
    };
    root["params"] = resolve(root["params"]);
    const auto po = root["params"].toObject();
    if (!po["layers"].isArray() || !po["parents"].isArray())
        return fail("Invalid layer structure.");
    // Reject invalid numeric/type data before it can reach render allocations.
    std::function<bool(const QJsonValue&, const QString&)> validValue;
    validValue = [&](const QJsonValue& v, const QString& key) {
        if (v.isDouble()) {
            const double n = v.toDouble();
            if (!std::isfinite(n) || std::abs(n) > 1000000) return false;
            if ((key == "frameW" || key == "frameH") && (n < 16 || n > 8192 || n != std::floor(n))) return false;
            if ((key == "opacity" || key == "backgroundOpacity") && (n < 0 || n > 1)) return false;
            if ((key == "scalePct" || key == "aspectPct" || key == "spacing" || key == "cellSize" || key == "pixelSize") && n <= 0) return false;
        }
        if (v.isArray()) {
            const auto array = v.toArray();
            if (array.size() > 100000) return false;
            for (const auto& x : array) if (!validValue(x, key)) return false;
        }
        if (v.isObject()) {
            const auto object = v.toObject();
            for (auto it = object.begin(); it != object.end(); ++it)
                if (!validValue(it.value(), it.key())) return false;
        }
        return true;
    };
    if (!validValue(root, {})) return fail("A project value is outside the supported limits.");
    std::function<bool(const QJsonObject&, const QJsonObject&)> typesMatch;
    typesMatch = [&](const QJsonObject& object, const QJsonObject& model) {
        for (auto it = model.begin(); it != model.end(); ++it) {
            if (!object.contains(it.key())) continue; // older optional fields retain defaults
            const auto value = object[it.key()];
            if (value.type() != it.value().type()) return false;
            if (value.isObject() && !typesMatch(value.toObject(), it.value().toObject())) return false;
        }
        return true;
    };
    if (!typesMatch(po, toJson(SessionParams{})) || !typesMatch(root["animation"].toObject(), toJson(Animation{}))) return fail("Incorrect project field type.");
    for (const auto& value : po["layers"].toArray())
        if (!value.isObject() || !typesMatch(value.toObject(), toJson(Layer{}))) return fail("Incorrect layer field type.");
    for (const auto& value : po["parents"].toArray())
        if (!value.isObject() || !typesMatch(value.toObject(), toJson(ParentGroup{}))) return fail("Incorrect parent field type.");
    ProjectData candidate;
    candidate.title = root["title"].toString(QFileInfo(path).completeBaseName());
    candidate.params = sessionFromJson(po);
    candidate.anim = animFromJson(root["animation"].toObject());
    if (candidate.params.frameW < 16 || candidate.params.frameW > 8192
        || candidate.params.frameH < 16 || candidate.params.frameH > 8192
        || candidate.params.layers.size() > 256 || candidate.anim.frameStart < 0
        || candidate.anim.frameEnd < candidate.anim.frameStart
        || candidate.anim.frameEnd > 100000 || candidate.anim.fps < 1 || candidate.anim.fps > 240
        || candidate.anim.stepFps < 1 || candidate.anim.stepFps > 240)
        return fail("Invalid frame dimensions or timeline range.");
    const QJsonObject media = root["media"].toObject();
    if (media.size() > 256) return fail("Too many sources (maximum 256).");
    qint64 decodedBytes = 0;
    for (auto it = media.constBegin(); it != media.constEnd(); ++it) {
        if (cancel && cancel->load()) return fail("Canceled.");
        bool ok = false;
        const int mediaId = it.key().toInt(&ok);
        if (!ok || mediaId < 1 || !it.value().isObject()) return fail("Invalid source ID.");
        const QJsonObject mo = it.value().toObject();
        QImage image = imageFromBase64(mo["png"].toString(), mo["frames"].toArray().isEmpty()
            ? 512LL * 1024 * 1024 - decodedBytes : 512LL * 1024 * 1024);
        if (image.isNull()) return fail("A source image is missing or invalid.");
        if (mo["frames"].toArray().isEmpty()) decodedBytes += image.sizeInBytes();
        if (decodedBytes > 512LL * 1024 * 1024) return fail("Project sources exceed the 512 MiB memory budget.");
        MediaEntry entry; entry.name = mo["name"].toString(); entry.image = image;
        entry.fps = mo["fps"].toDouble(24.0);
        if (!std::isfinite(entry.fps) || entry.fps < 0 || entry.fps > 240 || (!mo["frames"].toArray().isEmpty() && entry.fps == 0)) return fail("Invalid source frame rate.");
        if (mo.contains("frames") && !mo["frames"].isArray()) return fail("Invalid video frame list.");
        FrameStore videoFrames; QString cacheError;
        for (const auto& value : mo["frames"].toArray()) {
            if (cancel && cancel->load()) return fail("Canceled.");
            QImage frame = imageFromBase64(value.toString());
            if (frame.isNull() || frame.size() != image.size()) return fail("Invalid video frame.");
            if (!videoFrames.append(frame, cacheError)) return fail(cacheError);
        }
        if (videoFrames.count()) {
            entry.frames = videoFrames.finish(cacheError);
            if (entry.frames.isEmpty()) return fail(cacheError);
            entry.image = entry.frames.first();
        }
        candidate.media.insert(mediaId, std::move(entry));
    }
    QSet<int> ids, parents;
    int maxId = 0;
    for (const auto& l : candidate.params.layers) {
        if (l.id < 1 || ids.contains(l.id) || int(l.kind) < 0 || int(l.kind) > int(LayerKind::Halftone)
            || int(l.blend) < 0 || int(l.blend) > int(BlendMode::Luminosity)
            || !candidate.media.contains(l.mediaId)) return fail("Invalid layer or missing source reference.");
        auto enumIn = [](int value, int maximum) { return value >= 0 && value <= maximum; };
        if (!enumIn(int(l.dither.algorithm), int(DitherAlgorithm::CustomPattern))
            || !enumIn(int(l.halftone.dotShape), int(ScreenDotShape::Ink))) return fail("Unknown rendering option.");
        for (const auto& shape : l.dotGrid.shapes) {
            if (!enumIn(int(shape.shape), int(DotGridShape::CustomSVG))) return fail("Unknown dot shape.");
            if (shape.shape == DotGridShape::CustomSVG && !QFileInfo::exists(shape.svgPath)) return fail("A custom SVG is missing. Restore its original path and reopen the project.");
        }
        if (l.dither.algorithm == DitherAlgorithm::CustomPattern && !QFileInfo::exists(l.dither.patternPath)) return fail("A custom pattern is missing.");
        ids.insert(l.id); maxId = qMax(maxId, l.id);
    }
    for (const auto& g : candidate.params.parents) {
        if (!candidate.media.contains(g.mediaId) || parents.contains(g.mediaId))
            return fail("Invalid parent source reference.");
        parents.insert(g.mediaId);
    }
    candidate.params.nextLayerId = qMax(candidate.params.nextLayerId, maxId + 1);
    if (!ids.contains(candidate.params.activeLayerId))
        candidate.params.activeLayerId = candidate.params.layers.empty() ? -1 : candidate.params.layers.front().id;
    for (auto& track : candidate.anim.tracks) {
        if ((track.layerId != -1 && !ids.contains(track.layerId)) || int(track.param) < 0
            || int(track.param) >= int(ParamId::Count)) return fail("Invalid animation track.");
        std::sort(track.keys.begin(), track.keys.end(), [](const auto& x, const auto& y){ return x.frame < y.frame; });
        int last = -1;
        for (const auto& key : track.keys) {
            if (key.frame < 0 || key.frame > 100000 || key.frame == last
                || int(key.easing) < 0 || int(key.easing) > int(Easing::EaseInOut)) return fail("Invalid keyframe.");
            last = key.frame;
        }
    }
    *out = std::move(candidate);
    return true;
}

} // namespace ProjectIO
