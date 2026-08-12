#pragma once
#include <QPoint>
#include <QRect>
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
    void showEvent(QShowEvent* event) override;

private:
    void initNetwork();
    void setupConnections();
    void showAuthPage(int index);
    void showWorkspacePage(int index);
    void resetToLoginPage();

    // 无边框窗口缩放：边缘/四角识别 + Qt6 QWindow::startSystemResize
    void installResizeFilters();
    Qt::Edges resizeEdgesAt(const QPoint& globalPos) const;
    void updateResizeCursor(const QPoint& globalPos);
    void startWindowResize(const QPoint& globalPos);
    void applyPageMinimumSize();
    void clearMinSizeRecursive(QWidget* w);

    // 物理像素 -> 逻辑像素：Qt 布局尺寸是逻辑像素，
    // 必须除以 devicePixelRatio，避免 150% 缩放下窗口尺寸偏大。
    QSize logicalSizeFor(const QSize& physical) const;
    static const QSize kAuthWindowSizePhysical;
    static const QSize kWorkspaceWindowSizePhysical;
    static const QSize kMinWorkspaceWindowSizePhysical; // 最小工作区 = 默认约 10%

    static const int kResizeGrip = 6; // 边缘缩放热区宽度（逻辑像素）

    Ui::MainWindow* ui;
    NetworkManager* networkManager = nullptr;
    QPoint dragOffset;
    bool dragging = false;
    bool resizeFiltersInstalled = false;
};
