#include "media/ElementProperties.h"

#include <QHash>

#include <algorithm>
#include <climits>

#ifdef AVENA_HAVE_GSTREAMER
#include <gst/gst.h>
#endif

namespace ab {
namespace ElementProperties {

bool available()
{
#ifdef AVENA_HAVE_GSTREAMER
    return true;
#else
    return false;
#endif
}

#ifdef AVENA_HAVE_GSTREAMER

namespace {

void ensureGstInit()
{
    static bool done = false;
    if (!done) {
        gst_init(nullptr, nullptr);
        done = true;
    }
}

bool rangeFitsInt(double lo, double hi)
{
    return lo >= static_cast<double>(INT_MIN) && hi <= static_cast<double>(INT_MAX);
}

bool describeProperty(GParamSpec* spec, ElementProperty& out)
{
    const GType valueType = G_PARAM_SPEC_VALUE_TYPE(spec);
    const GType fundamental = G_TYPE_FUNDAMENTAL(valueType);

    switch (fundamental) {
    case G_TYPE_BOOLEAN:
        out.kind = ElementProperty::Kind::Bool;
        out.defaultValue = static_cast<bool>(G_PARAM_SPEC_BOOLEAN(spec)->default_value);
        return true;
    case G_TYPE_INT: {
        auto* s = G_PARAM_SPEC_INT(spec);
        out.kind = ElementProperty::Kind::Int;
        out.minimum = s->minimum; out.maximum = s->maximum;
        out.fitsInInt = rangeFitsInt(s->minimum, s->maximum);
        out.defaultValue = static_cast<qlonglong>(s->default_value);
        return true;
    }
    case G_TYPE_UINT: {
        auto* s = G_PARAM_SPEC_UINT(spec);
        out.kind = ElementProperty::Kind::Int;
        out.minimum = s->minimum; out.maximum = s->maximum;
        out.fitsInInt = rangeFitsInt(s->minimum, s->maximum);
        out.defaultValue = static_cast<qlonglong>(s->default_value);
        return true;
    }
    case G_TYPE_INT64: {
        auto* s = G_PARAM_SPEC_INT64(spec);
        out.kind = ElementProperty::Kind::Int;
        out.minimum = static_cast<double>(s->minimum);
        out.maximum = static_cast<double>(s->maximum);
        out.fitsInInt = rangeFitsInt(out.minimum, out.maximum);
        out.defaultValue = static_cast<qlonglong>(s->default_value);
        return true;
    }
    case G_TYPE_UINT64: {
        auto* s = G_PARAM_SPEC_UINT64(spec);
        out.kind = ElementProperty::Kind::Int;
        out.minimum = static_cast<double>(s->minimum);
        out.maximum = static_cast<double>(s->maximum);
        out.fitsInInt = rangeFitsInt(out.minimum, out.maximum);
        out.defaultValue = static_cast<qlonglong>(s->default_value);
        return true;
    }
    case G_TYPE_FLOAT: {
        auto* s = G_PARAM_SPEC_FLOAT(spec);
        out.kind = ElementProperty::Kind::Double;
        out.minimum = s->minimum; out.maximum = s->maximum;
        out.defaultValue = static_cast<double>(s->default_value);
        return true;
    }
    case G_TYPE_DOUBLE: {
        auto* s = G_PARAM_SPEC_DOUBLE(spec);
        out.kind = ElementProperty::Kind::Double;
        out.minimum = s->minimum; out.maximum = s->maximum;
        out.defaultValue = s->default_value;
        return true;
    }
    case G_TYPE_STRING: {
        auto* s = G_PARAM_SPEC_STRING(spec);
        out.kind = ElementProperty::Kind::String;
        out.defaultValue = s->default_value ? QString::fromUtf8(s->default_value)
                                            : QString();
        return true;
    }
    case G_TYPE_ENUM: {
        auto* s = G_PARAM_SPEC_ENUM(spec);
        out.kind = ElementProperty::Kind::Enum;
        if (GEnumClass* ec = s->enum_class) {
            for (guint i = 0; i < ec->n_values; ++i) {
                const gchar* nick = ec->values[i].value_nick;
                out.choices.push_back({
                    nick ? QString::fromUtf8(nick) : QString::number(ec->values[i].value),
                    static_cast<int>(ec->values[i].value)});
            }
        }
        out.defaultValue = static_cast<int>(s->default_value);
        return true;
    }
    default:
        return false; // flags, boxed (caps, fraction…) — not edited generically
    }
}

std::vector<ElementProperty> introspect(const QString& factoryName)
{
    ensureGstInit();

    std::vector<ElementProperty> result;

    GstElementFactory* factory =
        gst_element_factory_find(factoryName.toUtf8().constData());
    if (!factory)
        return result;

    GstElement* element = gst_element_factory_create(factory, nullptr);
    gst_object_unref(factory);
    if (!element)
        return result;

    guint count = 0;
    GParamSpec** specs =
        g_object_class_list_properties(G_OBJECT_GET_CLASS(element), &count);

    for (guint i = 0; i < count; ++i) {
        GParamSpec* spec = specs[i];

        if (!(spec->flags & G_PARAM_WRITABLE))
            continue;
        if (spec->flags & G_PARAM_CONSTRUCT_ONLY)
            continue;   // cannot be changed after the element is created

        const QString name = QString::fromUtf8(g_param_spec_get_name(spec));
        if (name == QLatin1String("name") || name == QLatin1String("parent"))
            continue;   // inherited GstObject noise

        ElementProperty prop;
        prop.name = name;
        if (const gchar* blurb = g_param_spec_get_blurb(spec))
            prop.blurb = QString::fromUtf8(blurb);

        if (describeProperty(spec, prop))
            result.push_back(std::move(prop));
    }

    g_free(specs);
    gst_object_unref(element);

    std::ranges::sort(result, [](const ElementProperty& a, const ElementProperty& b) {
        return a.name < b.name;
    });
    return result;
}

} // namespace

#endif // AVENA_HAVE_GSTREAMER

const std::vector<ElementProperty>& forElement(const QString& factoryName)
{
    static QHash<QString, std::vector<ElementProperty>> cache;

    auto it = cache.constFind(factoryName);
    if (it != cache.constEnd())
        return it.value();

#ifdef AVENA_HAVE_GSTREAMER
    return *cache.insert(factoryName, introspect(factoryName));
#else
    return *cache.insert(factoryName, {});
#endif
}

} // namespace ElementProperties
} // namespace ab
