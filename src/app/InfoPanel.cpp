#include "app/InfoPanel.h"

#include "graph/Node.h"
#include "graph/NodeCatalog.h"
#include "media/MediaInfo.h"

#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

namespace ab {

namespace {

QString formatDuration(qint64 ns)
{
    if (ns <= 0)
        return QStringLiteral("—");
    const qint64 totalMs = ns / 1'000'000;
    const qint64 ms = totalMs % 1000;
    const qint64 totalS = totalMs / 1000;
    const qint64 s = totalS % 60;
    const qint64 m = (totalS / 60) % 60;
    const qint64 h = totalS / 3600;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h)
            .arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2.%3")
        .arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
        .arg(ms, 3, 10, QLatin1Char('0'));
}

QString formatBitrate(int bitsPerSecond)
{
    if (bitsPerSecond <= 0)
        return {};
    return QStringLiteral(" · %1 kb/s").arg((bitsPerSecond + 500) / 1000);
}

QLabel* value(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

QString mediaTypeLabel(MediaType type)
{
    switch (type) {
    case MediaType::Video:    return QStringLiteral("video");
    case MediaType::Audio:    return QStringLiteral("audio");
    case MediaType::Subtitle: return QStringLiteral("subtitle");
    case MediaType::Data:     return QStringLiteral("data");
    case MediaType::Value:    return QStringLiteral("value");
    case MediaType::Any:      return QStringLiteral("any");
    }
    return {};
}

} // namespace

InfoPanel::InfoPanel(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);

    m_title = new QLabel(this);
    QFont tf = m_title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 1.0);
    m_title->setFont(tf);
    m_title->setWordWrap(true);
    outer->addWidget(m_title);

    m_formHost = new QWidget(this);
    m_form = new QFormLayout(m_formHost);
    m_form->setContentsMargins(0, 8, 0, 0);
    m_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    outer->addWidget(m_formHost);

    m_placeholder = new QLabel(tr("Select a node to see its details."), this);
    m_placeholder->setWordWrap(true);
    m_placeholder->setEnabled(false);   // theme-dimmed text
    outer->addWidget(m_placeholder);

    outer->addStretch(1);
    setNode(nullptr);
}

void InfoPanel::clearForm()
{
    while (m_form->count() > 0) {
        QLayoutItem* item = m_form->takeAt(0);
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

void InfoPanel::setNode(Node* node)
{
    m_node = node;
    rebuild();
}

void InfoPanel::onNodeModified(Node* node)
{
    if (node && node == m_node)
        rebuild();
}

void InfoPanel::rebuild()
{
    clearForm();

    const bool hasNode = m_node != nullptr;
    m_title->setVisible(hasNode);
    m_formHost->setVisible(hasNode);
    m_placeholder->setVisible(!hasNode);
    if (!hasNode)
        return;

    m_title->setText(m_node->title);

    // What this element is / does (from the GStreamer element metadata).
    if (const NodeTypeSpec* spec = NodeCatalog::instance().find(m_node->typeId)) {
        if (!spec->longName.isEmpty() && spec->longName != m_node->title)
            m_form->addRow(tr("Name"), value(spec->longName, m_formHost));
        if (!spec->description.isEmpty())
            m_form->addRow(tr("Purpose"), value(spec->description, m_formHost));
    }

    const QString location = m_node->properties.contains(QStringLiteral("location"))
        ? m_node->properties.value(QStringLiteral("location")).toString()
        : QString();

    if (!location.isEmpty())
        addMediaInfo(location);
    else
        addNodeInfo();
}

void InfoPanel::addNodeInfo()
{
    m_form->addRow(tr("Element"), value(m_node->typeId, m_formHost));
    m_form->addRow(tr("Category"), value(m_node->category, m_formHost));

    auto portSummary = [](const std::vector<Port>& ports) {
        if (ports.empty())
            return QStringLiteral("—");
        QStringList parts;
        for (const Port& p : ports)
            parts << QStringLiteral("%1 (%2)").arg(p.name, mediaTypeLabel(p.mediaType));
        return parts.join(QStringLiteral(", "));
    };

    m_form->addRow(tr("Inputs"), value(portSummary(m_node->inputs), m_formHost));
    m_form->addRow(tr("Outputs"), value(portSummary(m_node->outputs), m_formHost));
}

void InfoPanel::addMediaInfo(const QString& location)
{
    const MediaInfo info = MediaProbe::probe(location);

    m_form->addRow(tr("File"), value(location, m_formHost));
    m_form->addRow(tr("Size"), value(QLocale().formattedDataSize(info.fileSize), m_formHost));

    if (!info.valid) {
        auto* warn = value(info.error, m_formHost);
        warn->setStyleSheet(QStringLiteral("color:#E0A030;"));
        m_form->addRow(tr("Status"), warn);
        return;
    }

    m_form->addRow(tr("Duration"), value(formatDuration(info.durationNs), m_formHost));
    if (!info.container.isEmpty())
        m_form->addRow(tr("Container"), value(info.container, m_formHost));

    auto* line = new QFrame(m_formHost);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    m_form->addRow(line);

    int videoN = 0, audioN = 0, subN = 0;
    for (const MediaStreamInfo& s : info.streams) {
        switch (s.kind) {
        case MediaStreamInfo::Kind::Video: {
            QString v = QStringLiteral("%1×%2").arg(s.width).arg(s.height);
            if (s.framerate > 0.0)
                v += QStringLiteral(" · %1 fps").arg(s.framerate, 0, 'g', 4);
            if (!s.codec.isEmpty())
                v += QStringLiteral(" · %1").arg(s.codec);
            v += formatBitrate(s.bitrate);
            m_form->addRow(tr("Video %1").arg(videoN++), value(v, m_formHost));
            break;
        }
        case MediaStreamInfo::Kind::Audio: {
            QString a;
            if (s.channels > 0)
                a += s.channels == 1 ? tr("mono")
                   : s.channels == 2 ? tr("stereo")
                   : tr("%1 ch").arg(s.channels);
            if (s.sampleRate > 0)
                a += QStringLiteral(" · %1 kHz").arg(s.sampleRate / 1000.0, 0, 'g', 4);
            if (!s.codec.isEmpty())
                a += QStringLiteral(" · %1").arg(s.codec);
            a += formatBitrate(s.bitrate);
            m_form->addRow(tr("Audio %1").arg(audioN++), value(a.trimmed(), m_formHost));
            break;
        }
        case MediaStreamInfo::Kind::Subtitle: {
            const QString t = s.language.isEmpty() ? s.codec
                : QStringLiteral("%1 (%2)").arg(s.codec, s.language);
            m_form->addRow(tr("Subtitle %1").arg(subN++), value(t, m_formHost));
            break;
        }
        case MediaStreamInfo::Kind::Other:
            break;
        }
    }
}

} // namespace ab
