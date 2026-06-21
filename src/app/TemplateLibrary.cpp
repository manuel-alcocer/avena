#include "app/TemplateLibrary.h"

#include "graph/Graph.h"
#include "graph/GraphSerializer.h"
#include "graph/Node.h"
#include "graph/NodeCatalog.h"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

namespace ab {
namespace TemplateLibrary {

namespace {

/// A linear transcode recipe: a video and/or audio encoder feeding a muxer
/// (empty muxer = audio-only, encoder straight to the file sink).
struct Recipe {
    QString     id;
    QString     name;
    QString     description;
    QStringList videoEncoders;   // candidates, first installed one wins
    QStringList audioEncoders;
    QString     muxer;
    QString     requirement;     // hardware/plugin needed (empty for software)
};

const std::vector<Recipe>& recipes()
{
    static const std::vector<Recipe> all = {
        {QStringLiteral("h264.mp4"), QStringLiteral("H.264 → MP4"),
         QStringLiteral("Transcode video to H.264 and audio to AAC in an MP4 container."),
         {QStringLiteral("x264enc"), QStringLiteral("openh264enc")},
         {QStringLiteral("avenc_aac"), QStringLiteral("voaacenc"), QStringLiteral("fdkaacenc")},
         QStringLiteral("mp4mux")},

        {QStringLiteral("h265.mp4"), QStringLiteral("H.265 → MP4"),
         QStringLiteral("Transcode video to H.265/HEVC and audio to AAC in an MP4 container."),
         {QStringLiteral("x265enc"), QStringLiteral("nvh265enc"), QStringLiteral("vaapih265enc")},
         {QStringLiteral("avenc_aac"), QStringLiteral("voaacenc"), QStringLiteral("fdkaacenc")},
         QStringLiteral("mp4mux")},

        {QStringLiteral("h264.mkv"), QStringLiteral("H.264 → MKV"),
         QStringLiteral("Transcode video to H.264 and audio to AAC in a Matroska container."),
         {QStringLiteral("x264enc"), QStringLiteral("openh264enc")},
         {QStringLiteral("avenc_aac"), QStringLiteral("voaacenc"), QStringLiteral("fdkaacenc")},
         QStringLiteral("matroskamux")},

        {QStringLiteral("vp9.webm"), QStringLiteral("VP9 + Opus → WebM"),
         QStringLiteral("Transcode to royalty-free VP9 video and Opus audio in WebM."),
         {QStringLiteral("vp9enc")},
         {QStringLiteral("opusenc")},
         QStringLiteral("webmmux")},

        {QStringLiteral("mp3"), QStringLiteral("MP3 (audio only)"),
         QStringLiteral("Extract and encode the audio track to MP3."),
         {},
         {QStringLiteral("lamemp3enc"), QStringLiteral("avenc_mp3")},
         {}},

        // --- GPU: Intel / AMD via VA-API (gst-plugin-va) --------------------
        {QStringLiteral("va.h264.mp4"), QStringLiteral("H.264 · Intel/AMD GPU → MP4"),
         QStringLiteral("Hardware H.264 encoding on Intel/AMD GPUs (VA-API), audio to AAC."),
         {QStringLiteral("vah264enc"), QStringLiteral("vah264lpenc")},
         {QStringLiteral("avenc_aac"), QStringLiteral("voaacenc")},
         QStringLiteral("mp4mux"),
         QStringLiteral("Intel/AMD GPU with VA-API (gst-plugin-va)")},

        {QStringLiteral("va.h265.mp4"), QStringLiteral("H.265 · Intel/AMD GPU → MP4"),
         QStringLiteral("Hardware H.265/HEVC encoding on Intel/AMD GPUs (VA-API), audio to AAC."),
         {QStringLiteral("vah265enc"), QStringLiteral("vah265lpenc")},
         {QStringLiteral("avenc_aac"), QStringLiteral("voaacenc")},
         QStringLiteral("mp4mux"),
         QStringLiteral("Intel/AMD GPU with VA-API (gst-plugin-va)")},

        {QStringLiteral("va.av1.mkv"), QStringLiteral("AV1 · Intel/AMD GPU → MKV"),
         QStringLiteral("Hardware AV1 encoding on Intel/AMD GPUs (VA-API), audio to Opus."),
         {QStringLiteral("vaav1enc"), QStringLiteral("vaav1lpenc")},
         {QStringLiteral("opusenc")},
         QStringLiteral("matroskamux"),
         QStringLiteral("Intel/AMD GPU with AV1 encode (gst-plugin-va)")},

        // --- GPU: NVIDIA via NVENC (nvcodec plugin) ------------------------
        {QStringLiteral("nv.h264.mp4"), QStringLiteral("H.264 · NVIDIA GPU → MP4"),
         QStringLiteral("Hardware H.264 encoding on NVIDIA GPUs (NVENC), audio to AAC."),
         {QStringLiteral("nvh264enc"), QStringLiteral("nvautogpuh264enc")},
         {QStringLiteral("avenc_aac"), QStringLiteral("voaacenc")},
         QStringLiteral("mp4mux"),
         QStringLiteral("NVIDIA GPU with NVENC (gst-plugin-bad nvcodec)")},

        {QStringLiteral("nv.h265.mp4"), QStringLiteral("H.265 · NVIDIA GPU → MP4"),
         QStringLiteral("Hardware H.265/HEVC encoding on NVIDIA GPUs (NVENC), audio to AAC."),
         {QStringLiteral("nvh265enc"), QStringLiteral("nvautogpuh265enc")},
         {QStringLiteral("avenc_aac"), QStringLiteral("voaacenc")},
         QStringLiteral("mp4mux"),
         QStringLiteral("NVIDIA GPU with NVENC (gst-plugin-bad nvcodec)")},

        {QStringLiteral("nv.av1.mkv"), QStringLiteral("AV1 · NVIDIA GPU → MKV"),
         QStringLiteral("Hardware AV1 encoding on NVIDIA GPUs (NVENC, 40-series+), audio to Opus."),
         {QStringLiteral("nvav1enc")},
         {QStringLiteral("opusenc")},
         QStringLiteral("matroskamux"),
         QStringLiteral("NVIDIA GPU with AV1 NVENC (RTX 40-series or newer)")},
    };
    return all;
}

QString firstInstalled(const QStringList& candidates)
{
    const NodeCatalog& catalog = NodeCatalog::instance();
    for (const QString& c : candidates)
        if (catalog.find(c))
            return c;
    return {};
}

/// Picks a source output of `type` and a free destination input accepting it,
/// then connects them.
bool connectByType(Graph& graph, Node* from, MediaType type, Node* to)
{
    int outIndex = -1;
    for (int i = 0; i < static_cast<int>(from->outputs.size()); ++i) {
        if (mediaTypesCompatible(from->outputs[static_cast<std::size_t>(i)].mediaType, type)) {
            outIndex = i;
            break;
        }
    }
    int inIndex = -1;
    for (int i = 0; i < static_cast<int>(to->inputs.size()); ++i) {
        const PortRef in{to->id, PortDirection::Input, i};
        if (mediaTypesCompatible(type, to->inputs[static_cast<std::size_t>(i)].mediaType) &&
            !graph.isInputOccupied(in)) {
            inIndex = i;
            break;
        }
    }
    if (outIndex < 0 || inIndex < 0)
        return false;
    return graph.addConnection({from->id, PortDirection::Output, outIndex},
                               {to->id, PortDirection::Input, inIndex}) != nullptr;
}

bool buildRecipe(const Recipe& r, Graph& graph, QString* error)
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };

    Node* source = graph.addNode(QStringLiteral("source.file"), QPointF(0, 0));
    if (!source)
        return fail(QStringLiteral("File Source unavailable."));

    // Audio-only: source → audio encoder → file sink.
    if (r.muxer.isEmpty()) {
        const QString aenc = firstInstalled(r.audioEncoders);
        if (aenc.isEmpty())
            return fail(QStringLiteral("No suitable audio encoder is installed."));
        Node* a = graph.addNode(aenc, QPointF(280, 0));
        Node* sink = graph.addNode(QStringLiteral("filesink"), QPointF(560, 0));
        if (!a || !sink)
            return fail(QStringLiteral("Could not create encoder/sink."));
        connectByType(graph, source, MediaType::Audio, a);
        connectByType(graph, a, MediaType::Audio, sink);
        return true;
    }

    Node* mux = graph.addNode(r.muxer, QPointF(560, 0));
    Node* sink = graph.addNode(QStringLiteral("filesink"), QPointF(820, 0));
    if (!mux || !sink)
        return fail(QStringLiteral("Could not create muxer/sink (%1).").arg(r.muxer));
    connectByType(graph, mux, MediaType::Data, sink);

    if (!r.videoEncoders.isEmpty()) {
        const QString venc = firstInstalled(r.videoEncoders);
        if (venc.isEmpty())
            return fail(QStringLiteral("No suitable video encoder is installed."));
        Node* v = graph.addNode(venc, QPointF(280, -100));
        if (!v) return fail(QStringLiteral("Could not create video encoder."));
        connectByType(graph, source, MediaType::Video, v);
        connectByType(graph, v, MediaType::Video, mux);
    }
    if (!r.audioEncoders.isEmpty()) {
        const QString aenc = firstInstalled(r.audioEncoders);
        if (aenc.isEmpty())
            return fail(QStringLiteral("No suitable audio encoder is installed."));
        Node* a = graph.addNode(aenc, QPointF(280, 100));
        if (!a) return fail(QStringLiteral("Could not create audio encoder."));
        connectByType(graph, source, MediaType::Audio, a);
        connectByType(graph, a, MediaType::Audio, mux);
    }
    return true;
}

/// A recipe is offered only if its required encoders exist on this system.
bool recipeAvailable(const Recipe& r)
{
    if (!r.videoEncoders.isEmpty() && firstInstalled(r.videoEncoders).isEmpty())
        return false;
    if (!r.audioEncoders.isEmpty() && firstInstalled(r.audioEncoders).isEmpty())
        return false;
    if (!r.muxer.isEmpty() && !NodeCatalog::instance().find(r.muxer))
        return false;
    return true;
}

} // namespace

std::vector<TemplateInfo> builtins()
{
    std::vector<TemplateInfo> result;
    for (const Recipe& r : recipes())
        result.push_back({r.id, r.name, r.description, r.requirement,
                          recipeAvailable(r)});
    return result;
}

bool buildBuiltin(const QString& id, Graph& graph, QString* error)
{
    for (const Recipe& r : recipes())
        if (r.id == id)
            return buildRecipe(r, graph, error);
    if (error) *error = QStringLiteral("Unknown template: %1").arg(id);
    return false;
}

QString userTemplateDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/templates");
}

std::vector<QString> userTemplateFiles()
{
    std::vector<QString> files;
    QDir dir(userTemplateDir());
    if (!dir.exists())
        return files;
    const QStringList names = dir.entryList({QStringLiteral("*.abk")}, QDir::Files, QDir::Name);
    for (const QString& n : names)
        files.push_back(dir.absoluteFilePath(n));
    return files;
}

QString saveUserTemplate(const Graph& graph, const QString& name, QString* error)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = QStringLiteral("Template name is empty.");
        return {};
    }

    QDir dir(userTemplateDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("Could not create the templates folder.");
        return {};
    }

    // Sanitize the name into a file-system-safe base.
    QString safe = trimmed;
    safe.replace(QRegularExpression(QStringLiteral("[^\\w\\- ]")), QStringLiteral("_"));
    const QString path = dir.absoluteFilePath(safe + QStringLiteral(".abk"));

    if (!GraphSerializer::saveToFile(graph, path, error))
        return {};
    return path;
}

} // namespace TemplateLibrary
} // namespace ab
