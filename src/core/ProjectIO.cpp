#include "ProjectIO.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    img.save(&buf, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}
QImage imageFromBase64(const QString& s) {
    QImage img;
    img.loadFromData(QByteArray::fromBase64(s.toLatin1()), "PNG");
    return img;
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
        { "rotation", t.rotation }, { "flipH", t.flipH }, { "flipV", t.flipV },
    };
}
LayerTransform transformFromJson(const QJsonObject& o) {
    LayerTransform t;
    t.xPct     = float(o["xPct"].toDouble(t.xPct));
    t.yPct     = float(o["yPct"].toDouble(t.yPct));
    t.scalePct = float(o["scalePct"].toDouble(t.scalePct));
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
    };
}
ParentGroup parentFromJson(const QJsonObject& o) {
    ParentGroup p;
    p.mediaId      = o["mediaId"].toInt(p.mediaId);
    p.name         = o["name"].toString(p.name);
    p.collapsed    = o["collapsed"].toBool(p.collapsed);
    p.groupVisible = o["groupVisible"].toBool(p.groupVisible);
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

bool save(const QString& path, const ProjectData& data, QString* error)
{
    QJsonObject media;
    for (auto it = data.media.cbegin(); it != data.media.cend(); ++it) {
        media[QString::number(it.key())] = QJsonObject{
            { "name", it.value().name }, { "png", imageToBase64(it.value().image) },
        };
    }

    const QJsonObject root{
        { "formatVersion", 1 }, { "title", data.title },
        { "params", toJson(data.params) }, { "animation", toJson(data.anim) },
        { "media", media },
    };

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool load(const QString& path, ProjectData* out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = perr.errorString();
        return false;
    }

    const QJsonObject root = doc.object();
    out->title = root["title"].toString(QFileInfo(path).completeBaseName());
    out->params = sessionFromJson(root["params"].toObject());
    out->anim   = animFromJson(root["animation"].toObject());

    out->media.clear();
    const QJsonObject media = root["media"].toObject();
    for (auto it = media.constBegin(); it != media.constEnd(); ++it) {
        bool ok = false;
        const int mediaId = it.key().toInt(&ok);
        if (!ok) continue;
        const QJsonObject mo = it.value().toObject();
        out->media.insert(mediaId, { mo["name"].toString(), imageFromBase64(mo["png"].toString()) });
    }
    return true;
}

} // namespace ProjectIO
