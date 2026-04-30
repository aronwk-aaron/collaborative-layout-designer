#pragma once

#include <QIODevice>
#include <QString>
#include <QXmlStreamWriter>

namespace bld::saveload {

// Phase 1: implement canonicalizer matching C# XmlSerializer output byte-for-byte.
// For now, declare the interface so downstream code can target it.
class XmlCanonicalWriter {
public:
    explicit XmlCanonicalWriter(QIODevice* device);

    void writeStartDocument();
    void writeEndDocument();
    void writeStartElement(const QString& name);
    void writeEndElement();
    void writeTextElement(const QString& name, const QString& value);
    void writeAttribute(const QString& name, const QString& value);

private:
    QXmlStreamWriter writer_;
};

}
