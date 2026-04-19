#pragma once

#include <QMainWindow>

namespace cld::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void setupMenus();
    void setupCentralPlaceholder();
};

}
