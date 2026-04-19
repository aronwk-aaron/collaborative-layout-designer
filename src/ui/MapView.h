#pragma once

#include <QGraphicsView>

#include <memory>

namespace cld::core    { class Map; }
namespace cld::parts   { class PartsLibrary; }
namespace cld::rendering { class SceneBuilder; }

namespace cld::ui {

class MapView : public QGraphicsView {
    Q_OBJECT
public:
    explicit MapView(parts::PartsLibrary& parts, QWidget* parent = nullptr);
    ~MapView() override;

    void loadMap(std::unique_ptr<core::Map> map);

    core::Map* currentMap() { return map_.get(); }
    rendering::SceneBuilder* builder() { return builder_.get(); }

protected:
    void wheelEvent(QWheelEvent* e) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    parts::PartsLibrary& parts_;
    std::unique_ptr<core::Map> map_;
    std::unique_ptr<rendering::SceneBuilder> builder_;
};

}
