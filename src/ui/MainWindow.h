#pragma once

#include <QMainWindow>

#include <memory>

namespace cld::parts { class PartsLibrary; }

namespace cld::ui {

class MapView;
class LayerPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(parts::PartsLibrary& parts, QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString& path);

private slots:
    void onOpen();
    void onZoomIn();
    void onZoomOut();
    void onFitToView();

private:
    void setupMenus();

    parts::PartsLibrary& parts_;
    MapView*    mapView_   = nullptr;
    LayerPanel* layerPanel_ = nullptr;
};

}
