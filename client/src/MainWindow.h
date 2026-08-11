#pragma once
#include <QPoint>
#include <QSize>
#include <QWidget>

class NetworkManager;

namespace Ui {
class MainWindow;
}

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initNetwork();
    void setupConnections();
    void installTitleBarDragging();
    void showAuthPage(int index);
    void showWorkspacePage(int index);
    void resetToLoginPage();

    static const QSize kAuthWindowSize;
    static const QSize kWorkspaceWindowSize;

    Ui::MainWindow* ui;
    NetworkManager* networkManager = nullptr;
    QPoint dragOffset;
    bool dragging = false;
};
