#include "XmlCanonicalWriter.h"

namespace bld::saveload {

XmlCanonicalWriter::XmlCanonicalWriter(QIODevice* device) : writer_(device) {
    writer_.setAutoFormatting(false);
}

void XmlCanonicalWriter::writeStartDocument()             { writer_.writeStartDocument(); }
void XmlCanonicalWriter::writeEndDocument()               { writer_.writeEndDocument(); }
void XmlCanonicalWriter::writeStartElement(const QString& name)           { writer_.writeStartElement(name); }
void XmlCanonicalWriter::writeEndElement()                { writer_.writeEndElement(); }
void XmlCanonicalWriter::writeTextElement(const QString& name, const QString& value) {
    writer_.writeTextElement(name, value);
}
void XmlCanonicalWriter::writeAttribute(const QString& name, const QString& value) {
    writer_.writeAttribute(name, value);
}

}
