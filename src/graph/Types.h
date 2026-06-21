#pragma once

#include <QColor>
#include <QString>

namespace ab {

/// Direction of a port relative to its owning node.
enum class PortDirection { Input, Output };

/// The kind of media that flows through a port. Used for connection validation.
/// `Value` is special: it carries a scalar parameter (e.g. a computed bitrate)
/// into a node property rather than a media stream.
enum class MediaType { Any, Video, Audio, Subtitle, Data, Value };

/// Human-readable, stable identifier for a media type (used in serialization).
QString mediaTypeId(MediaType type);
MediaType mediaTypeFromId(const QString& id, MediaType fallback = MediaType::Any);

/// Display colour associated with a media type (used to paint ports/edges).
QColor mediaTypeColor(MediaType type);

/// Returns true if an output of type `out` may feed an input of type `in`.
/// `Any` acts as a wildcard on either side.
bool mediaTypesCompatible(MediaType out, MediaType in);

/// Stable string form for a port direction (serialization).
QString portDirectionId(PortDirection dir);
PortDirection portDirectionFromId(const QString& id, PortDirection fallback = PortDirection::Input);

} // namespace ab
