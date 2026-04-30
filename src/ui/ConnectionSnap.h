#pragma once

#include <QPointF>
#include <QSet>
#include <QString>

#include <optional>

namespace bld::core  { class Map; struct Brick; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

// Master-brick connection snap, matching BlueBrick's getMovedSnapPoint in
// MapData/LayerBrick.cs.
//
// The drag model is: the user grabs ONE specific brick (the one under
// the mouse) and ONE of its connection points is the "active" one (picked
// at click time as the connection nearest the click). Every drag frame,
// we compute where that single active connection currently is in world
// coords, and search the whole map for the nearest FREE compatible target
// connection excluding any brick in the moving set. If the nearest target
// is within `thresholdStuds` of the active connection, snap fires and the
// result gives the translation (and optional rotation) that aligns the
// two connections.
//
// Callers apply the translation uniformly to every brick they're moving
// so the group follows the master. Rotation only makes sense for a
// single-brick drop (rotating a whole selection around one pivot is
// jarring).
struct ConnectionSnapResult {
    bool applied = false;

    // Translation to apply assuming the brick's orientation does NOT
    // change. Used by the live drag (no rotation mid-drag) and by group
    // drops (rotating a group around one pivot would shuffle siblings).
    QPointF translationStuds;

    // Translation that pairs with `newOrientation` so angles also align
    // 180° — used by single-brick drops and new-part placement.
    QPointF rotationAlignedTranslationStuds;

    // The orientation the master brick should take at drop time for
    // angles to match. Absent when the snap didn't require a rotation or
    // when angles already match.
    std::optional<float> newOrientation;
};

// Run the master-brick snap against the current map state. `master` is
// the brick holding the active connection; `masterCenterStuds` is where
// that brick's center is RIGHT NOW (post-drag), in stud coords. Pass
// the same brick you already have in the map — the function only reads
// from it. `activeConnIdx` is an index into the brick part's metadata
// connections list. `movingGuids` is every brick currently being dragged
// with the master (including the master itself) so we don't snap to our
// own connections.
ConnectionSnapResult masterBrickSnap(
    const core::Map& map,
    parts::PartsLibrary& lib,
    const core::Brick& master,
    QPointF masterCenterStuds,
    int activeConnIdx,
    const QSet<QString>& movingGuids,
    double thresholdStuds);

// Convenience overload for new-part placement: we don't yet have a
// brick instance, just a part key + intended center + orientation. The
// snap tries each connection on the new part as the active one and
// picks the best result. Returns applied=false if none is within
// threshold.
ConnectionSnapResult newPartPlacementSnap(
    const core::Map& map,
    parts::PartsLibrary& lib,
    const QString& partKey,
    QPointF placementCenterStuds,
    float   placementOrientation,
    double  thresholdStuds);

}  // namespace bld::ui
