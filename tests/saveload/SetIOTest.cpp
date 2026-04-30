#include "saveload/SetIO.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QPointF>
#include <QString>
#include <QTemporaryDir>
#include <QXmlStreamReader>

using namespace bld;

namespace {

// Minimal re-parser so the test doesn't need a full PartsLibrary scan.
// Mirrors the shape readSubPartListTag + readPositionBlock in
// PartsLibrary.cpp but over an in-memory <group>.
struct ParsedSubpart {
    QString id;
    QPointF position;
    double  angle = 0.0;
};
struct ParsedSet {
    QString author;
    QString descriptionEn;
    bool canUngroup = false;
    QList<ParsedSubpart> subparts;
};

QPointF readPosition(QXmlStreamReader& r) {
    QPointF p;
    while (r.readNextStartElement()) {
        if      (r.name() == QStringLiteral("x")) p.setX(r.readElementText().toDouble());
        else if (r.name() == QStringLiteral("y")) p.setY(r.readElementText().toDouble());
        else r.skipCurrentElement();
    }
    return p;
}

ParsedSet parseSet(const QString& path) {
    ParsedSet s;
    QFile f(path);
    [&]{ ASSERT_TRUE(f.open(QIODevice::ReadOnly | QIODevice::Text)); }();
    QXmlStreamReader r(&f);
    if (!r.readNextStartElement()) return s;
    if (r.name() != QStringLiteral("group")) return s;
    while (r.readNextStartElement()) {
        const auto n = r.name();
        if (n == QStringLiteral("Author")) s.author = r.readElementText().trimmed();
        else if (n == QStringLiteral("Description")) {
            while (r.readNextStartElement()) {
                if (r.name() == QStringLiteral("en"))
                    s.descriptionEn = r.readElementText().trimmed();
                else r.skipCurrentElement();
            }
        }
        else if (n == QStringLiteral("CanUngroup"))
            s.canUngroup = (r.readElementText().trimmed().compare(
                QStringLiteral("true"), Qt::CaseInsensitive) == 0);
        else if (n == QStringLiteral("SubPartList")) {
            while (r.readNextStartElement()) {
                if (r.name() != QStringLiteral("SubPart")) { r.skipCurrentElement(); continue; }
                ParsedSubpart sp;
                sp.id = r.attributes().value(QStringLiteral("id")).toString();
                while (r.readNextStartElement()) {
                    if      (r.name() == QStringLiteral("position")) sp.position = readPosition(r);
                    else if (r.name() == QStringLiteral("angle"))    sp.angle = r.readElementText().toDouble();
                    else r.skipCurrentElement();
                }
                s.subparts.push_back(sp);
            }
        }
        else r.skipCurrentElement();
    }
    return s;
}

}  // namespace

TEST(SetIO, WriteMinimalRoundTrips) {
    saveload::SetManifest m;
    m.name = QStringLiteral("Test Set");
    m.author = QStringLiteral("tester");
    m.canUngroup = true;
    saveload::SetSubpart a;
    a.partKey = QStringLiteral("BT R104.8");
    a.positionStuds = QPointF(0, 0);
    a.angleDegrees = 0.0;
    saveload::SetSubpart b;
    b.partKey = QStringLiteral("BT 2865.8");
    b.positionStuds = QPointF(20.19525, -3.647644);
    b.angleDegrees = 708.75;
    m.subparts = { a, b };

    QTemporaryDir dir;
    const QString path = dir.filePath("test.set.xml");
    QString err;
    ASSERT_TRUE(saveload::writeSetXml(path, m, &err)) << err.toStdString();

    const ParsedSet back = parseSet(path);
    EXPECT_EQ(back.author, QStringLiteral("tester"));
    EXPECT_EQ(back.descriptionEn, QStringLiteral("Test Set"));
    EXPECT_TRUE(back.canUngroup);
    ASSERT_EQ(back.subparts.size(), 2);
    EXPECT_EQ(back.subparts[0].id, QStringLiteral("BT R104.8"));
    EXPECT_NEAR(back.subparts[0].position.x(), 0.0, 1e-6);
    EXPECT_NEAR(back.subparts[0].position.y(), 0.0, 1e-6);
    EXPECT_NEAR(back.subparts[0].angle, 0.0, 1e-6);
    EXPECT_EQ(back.subparts[1].id, QStringLiteral("BT 2865.8"));
    EXPECT_NEAR(back.subparts[1].position.x(), 20.19525, 1e-4);
    EXPECT_NEAR(back.subparts[1].position.y(), -3.647644, 1e-4);
    EXPECT_NEAR(back.subparts[1].angle, 708.75, 1e-4);
}

TEST(SetIO, OmitsEmptyMetadataFields) {
    saveload::SetManifest m;
    // Everything default-empty except one subpart.
    saveload::SetSubpart a;
    a.partKey = QStringLiteral("2865.8");
    a.angleDegrees = 0.0;
    m.subparts = { a };

    QTemporaryDir dir;
    const QString path = dir.filePath("bare.set.xml");
    ASSERT_TRUE(saveload::writeSetXml(path, m));

    const ParsedSet back = parseSet(path);
    EXPECT_TRUE(back.author.isEmpty());
    EXPECT_TRUE(back.descriptionEn.isEmpty());
    // CanUngroup defaults to true in the manifest, so it should still serialise.
    EXPECT_TRUE(back.canUngroup);
    ASSERT_EQ(back.subparts.size(), 1);
    EXPECT_EQ(back.subparts[0].id, QStringLiteral("2865.8"));
}

TEST(SetIO, PreservesPrecisionOnMultiDecimalPositions) {
    // BrickTracks sets routinely store 5-6 decimal positions. If the
    // writer rounds to 2 digits, tracks misalign by a stud or more when
    // the set is re-loaded. Guard against regressions on float format.
    saveload::SetManifest m;
    m.name = QStringLiteral("Precision Set");
    for (const QPointF pos : {
            QPointF(-131.4375, -3.811996),
            QPointF(-77.30539,  27.87526),
            QPointF(-137.3951,  44.1042 ) }) {
        saveload::SetSubpart sp;
        sp.partKey = QStringLiteral("TB 2865.8");
        sp.positionStuds = pos;
        sp.angleDegrees = 179.9998;
        m.subparts.push_back(sp);
    }

    QTemporaryDir dir;
    const QString path = dir.filePath("precise.set.xml");
    ASSERT_TRUE(saveload::writeSetXml(path, m));

    const ParsedSet back = parseSet(path);
    ASSERT_EQ(back.subparts.size(), 3);
    for (int i = 0; i < back.subparts.size(); ++i) {
        EXPECT_NEAR(back.subparts[i].position.x(),
                    m.subparts[i].positionStuds.x(), 5e-5)
            << "subpart " << i;
        EXPECT_NEAR(back.subparts[i].position.y(),
                    m.subparts[i].positionStuds.y(), 5e-5)
            << "subpart " << i;
    }
}
