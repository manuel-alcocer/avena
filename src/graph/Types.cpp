#include "graph/Types.h"

namespace ab {

QString mediaTypeId(MediaType type)
{
    switch (type) {
    case MediaType::Any:      return QStringLiteral("any");
    case MediaType::Video:    return QStringLiteral("video");
    case MediaType::Audio:    return QStringLiteral("audio");
    case MediaType::Subtitle: return QStringLiteral("subtitle");
    case MediaType::Data:     return QStringLiteral("data");
    case MediaType::Value:    return QStringLiteral("value");
    }
    return QStringLiteral("any");
}

MediaType mediaTypeFromId(const QString& id, MediaType fallback)
{
    if (id == QLatin1String("any"))      return MediaType::Any;
    if (id == QLatin1String("video"))    return MediaType::Video;
    if (id == QLatin1String("audio"))    return MediaType::Audio;
    if (id == QLatin1String("subtitle")) return MediaType::Subtitle;
    if (id == QLatin1String("data"))     return MediaType::Data;
    if (id == QLatin1String("value"))    return MediaType::Value;
    return fallback;
}

QColor mediaTypeColor(MediaType type)
{
    switch (type) {
    case MediaType::Any:      return QColor(0xB0, 0xBE, 0xC5);
    case MediaType::Video:    return QColor(0x4F, 0xC3, 0xF7);
    case MediaType::Audio:    return QColor(0x81, 0xC7, 0x84);
    case MediaType::Subtitle: return QColor(0xFF, 0xB7, 0x4D);
    case MediaType::Data:     return QColor(0xBA, 0x68, 0xC8);
    case MediaType::Value:    return QColor(0xFF, 0xD5, 0x4F);
    }
    return QColor(0xB0, 0xBE, 0xC5);
}

bool mediaTypesCompatible(MediaType out, MediaType in)
{
    if (out == MediaType::Any || in == MediaType::Any)
        return true;
    return out == in;
}

QString portDirectionId(PortDirection dir)
{
    return dir == PortDirection::Input ? QStringLiteral("in")
                                       : QStringLiteral("out");
}

PortDirection portDirectionFromId(const QString& id, PortDirection fallback)
{
    if (id == QLatin1String("in"))  return PortDirection::Input;
    if (id == QLatin1String("out")) return PortDirection::Output;
    return fallback;
}

} // namespace ab
