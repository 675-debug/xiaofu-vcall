#include "MainWidget.h"
#include "ui_MainWidget.h"
#include "network/NetworkManager.h"
#include <QDebug>
#include <QListWidgetItem>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QToolButton>
#include <QIcon>
#include <QColor>
#include <QPainter>
#include <QPaintEvent>
#include <QTime>

// 联系人姓名标签：超长文本自动省略号，避免被右边缘截断
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(const QString& text, QWidget* parent = nullptr)
        : QLabel(text, parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setFont(font());                    // 跟随 QSS 设置的字体
        painter.setPen(QColor(26, 26, 46));         // #1A1A2E
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

MainWidget::MainWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::MainWidget) {
    ui->setupUi(this);

    // 个人信息卡片背景/圆角（纯 QWidget 需要 WA_StyledBackground 才会绘制 QSS 背景）
    ui->myAccountInfo->setAttribute(Qt::WA_StyledBackground, true);

    // 顶部窗口控制按钮改用自定义文字图标（无边框沉浸式）
    auto setupWinButton = [](QToolButton* btn, const QString& glyph) {
        btn->setIcon(QIcon());
        btn->setText(glyph);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    };
    setupWinButton(ui->btnMin, QStringLiteral("\u2500"));   // ─
    setupWinButton(ui->btnMax, QStringLiteral("\u25A1"));   // □
    setupWinButton(ui->btnClose, QStringLiteral("\u2715")); // ✕

    // 为输入/搜索/按钮增加外投影，增强层级感
    auto addShadow = [](QWidget* widget, int blurRadius, int verticalOffset, int opacity) {
        auto* shadowEffect = new QGraphicsDropShadowEffect(widget);
        shadowEffect->setBlurRadius(blurRadius);
        shadowEffect->setOffset(0, verticalOffset);
        shadowEffect->setColor(QColor(0, 0, 0, opacity));
        widget->setGraphicsEffect(shadowEffect);
    };
    addShadow(ui->editSearch, 10, 1, 22);
    addShadow(ui->editMessage, 10, 1, 22);
    addShadow(ui->btnSend, 12, 2, 30);
    addShadow(ui->btnSettings, 8, 1, 18);
    addShadow(ui->btnCall, 8, 1, 18);

    // 静态数据：当前登录账号（后续由登录流程填充，见 TODO）
    ui->labelMyName->setText("Alice");
    ui->labelMyStatus->setText("在线");

    // 静态数据：在线联系人
    addContactItem(ui->listContacts, "Bob",     "在线", "#10B981");
    addContactItem(ui->listContacts, "Charlie", "在线", "#F97316");
    addContactItem(ui->listContacts, "Diana",   "在线", "#8B5CF6");
    // 静态数据：离线联系人
    addContactItem(ui->listOffline, "Eve",   "离线 · 3 小时前在线", "#AEAEB2");
    addContactItem(ui->listOffline, "Frank", "离线 · 昨天在线",     "#8E8E93");

    // 静态数据：示例聊天记录（右侧默认展示 Bob 的会话）
    ui->chatName->setText("Bob");
    ui->chatStatus->setText("在线");
    ui->chatAvatar->setText("B");
    addMessageItem("B", "#10B981", "在吗？今晚八点一起打视频？", "14:20", false);
    addMessageItem("A", "#007AFF", "在的，可以啊，八点见～",     "14:21", true);
    addMessageItem("B", "#10B981", "好嘞，我先去准备一下",       "14:22", false);

    // 默认显示空态页（点击联系人后切到聊天视图，见 TODO）
    ui->chatStack->setCurrentIndex(0);
    // 设置弹窗默认隐藏（.ui 中已设置，这里再次确认）
    ui->settingsMask->setVisible(false);
}

MainWidget::~MainWidget() {
    delete ui;
}

void MainWidget::setNetworkManager(NetworkManager* manager) {
    networkManager = manager;
}

void MainWidget::addContactItem(QListWidget* list, const QString& name,
                                const QString& status, const QString& color) {
    auto* item = new QListWidgetItem(list);
    item->setData(Qt::UserRole, name);   // 名字存入 data，避免 item 文本与 item widget 重复显示
    item->setSizeHint(QSize(0, 60));

    auto* row = new QWidget(list);
    auto* layout = new QHBoxLayout(row);
    // 列表项高 60px：上方 6px、下方 14px，让 40px 头像略微上移且不再被底部裁切。
    layout->setContentsMargins(16, 6, 16, 14);
    layout->setSpacing(12);

    auto* avatar = new QLabel(row);
    avatar->setFixedSize(40, 40);
    avatar->setText(name.left(1));
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(QString(
        "background:%1;color:#FFFFFF;border-radius:20px;font-size:14px;font-weight:700;")
        .arg(color));

    auto* info = new QWidget(row);
    auto* infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(2);
    auto* nameLabel = new ElidedLabel(name, info);
    nameLabel->setStyleSheet("font-size:14px;font-weight:600;color:#1A1A2E;border:none;background:transparent;");
    auto* statusLabel = new QLabel(status, info);
    statusLabel->setStyleSheet("font-size:11px;color:#AEAEB2;border:none;background:transparent;");
    infoLayout->addWidget(nameLabel);
    infoLayout->addWidget(statusLabel);

    layout->addWidget(avatar);
    layout->addWidget(info, 1);
    list->setItemWidget(item, row);
}

void MainWidget::addMessageItem(const QString& avatarText, const QString& color,
                                const QString& text, const QString& time, bool mine) {
    auto* item = new QListWidgetItem(ui->listMessages);

    auto* row = new QWidget(ui->listMessages);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(24, 8, 24, 8);
    // 消息与头像间距 20px
    layout->setSpacing(20);

    auto* avatar = new QLabel(row);
    avatar->setFixedSize(36, 36);
    avatar->setText(avatarText);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(QString(
        "background:%1;color:#FFFFFF;border-radius:18px;font-size:14px;font-weight:700;")
        .arg(color));

    auto* bubble = new QLabel(text, row);
    QFont bubbleFont = bubble->font();
    bubbleFont.setPixelSize(13);
    bubble->setFont(bubbleFont);
    bubble->setWordWrap(true);
    // 内容边距参与控件尺寸计算，避免 QSS padding 只绘制、不扩展 sizeHint。
    bubble->setContentsMargins(14, 10, 14, 10);
    bubble->setStyleSheet(mine
        ? "background:#007AFF;color:#FFFFFF;border-radius:12px;border-top-right-radius:4px;"
        : "background:#F5F5F5;color:#1A1A2E;border:1px solid #EEEEF1;border-radius:12px;border-top-left-radius:4px;");

    // 按真实字宽确定气泡，超过 520px 后自动换行并同步增加列表项高度。
    const int textWidth = bubble->fontMetrics().horizontalAdvance(text);
    const int bubbleWidth = qBound(60, textWidth + 36, 520);
    const int wrappedTextHeight = bubble->fontMetrics()
        .boundingRect(QRect(0, 0, bubbleWidth - 28, 10000),
                      Qt::TextWordWrap, text).height();
    bubble->setFixedSize(bubbleWidth, qMax(40, wrappedTextHeight + 20));

    auto* wrap = new QWidget(row);
    auto* wrapLayout = new QVBoxLayout(wrap);
    wrapLayout->setContentsMargins(0, 0, 0, 0);
    wrapLayout->setSpacing(6);
    wrapLayout->addWidget(bubble, 0, mine ? Qt::AlignRight : Qt::AlignLeft);
    auto* timeLabel = new QLabel(time, wrap);
    timeLabel->setStyleSheet("font-size:11px;color:#8E8E93;border:none;background:transparent;");
    if (mine)
        timeLabel->setAlignment(Qt::AlignRight);
    wrapLayout->addWidget(timeLabel);

    if (mine) {
        layout->addWidget(wrap, 1, Qt::AlignRight);
        layout->addWidget(avatar, 0, Qt::AlignTop);
    } else {
        layout->addWidget(avatar, 0, Qt::AlignTop);
        layout->addWidget(wrap, 1);
    }
    const int contentHeight = bubble->height() + wrapLayout->spacing()
        + timeLabel->sizeHint().height();
    item->setSizeHint(QSize(0, qMax(avatar->height(), contentHeight) + 16));
    ui->listMessages->setItemWidget(item, row);
}
void MainWidget::on_btnMin_clicked() {
    window()->showMinimized();
}

void MainWidget::on_btnMax_clicked() {
    if (window()->isMaximized())
        window()->showNormal();
    else
        window()->showMaximized();
}

void MainWidget::on_btnClose_clicked() {
    window()->close();
}

void MainWidget::on_btnAvatar_clicked() {
    // TODO: 点击头像展示当前登录用户资料/默认进入发消息界面
    emit switchToChat();
}

void MainWidget::on_btnSettings_clicked() {
    // TODO: 打开设置弹窗（账号、昵称、状态等信息由登录数据填充）
    ui->settingsMask->setGeometry(rect());
    ui->settingsPanel->move((width() - ui->settingsPanel->width()) / 2,
                            (height() - ui->settingsPanel->height()) / 2);
    ui->settingsMask->raise();
    ui->settingsMask->setVisible(true);
}

void MainWidget::on_btnLogout_clicked() {
    // TODO: 通知服务器下线并回到登录页
    ui->settingsMask->setVisible(false);
    emit logoutRequested();
}

void MainWidget::on_btnCloseModal_clicked() {
    ui->settingsMask->setVisible(false);
}

void MainWidget::on_btnSend_clicked() {
    appendLocalMessage();
}

void MainWidget::on_btnCall_clicked() {
    // TODO: 发起视频通话（离线联系人提示不可呼叫）
    emit switchToCall();
}

void MainWidget::on_editMessage_returnPressed() {
    appendLocalMessage();
}

void MainWidget::on_listContacts_itemClicked(QListWidgetItem* item) {
    selectContact(item, true);
}

void MainWidget::on_listOffline_itemClicked(QListWidgetItem* item) {
    selectContact(item, false);
}

void MainWidget::on_editSearch_textChanged(const QString& text) {
    const QString keyword = text.trimmed();
    filterContactList(ui->listContacts, keyword);
    filterContactList(ui->listOffline, keyword);
}

void MainWidget::appendLocalMessage() {
    const QString text = ui->editMessage->text().trimmed();
    if (text.isEmpty())
        return;

    // TODO: 后续将消息通过 TCP 长连接发送给当前联系人。
    addMessageItem("A", "#007AFF", text,
                   QTime::currentTime().toString("HH:mm"), true);
    ui->editMessage->clear();
    ui->listMessages->scrollToBottom();
}

void MainWidget::selectContact(QListWidgetItem* item, bool online) {
    if (!item)
        return;

    if (online)
        ui->listOffline->clearSelection();
    else
        ui->listContacts->clearSelection();

    const QString name = item->data(Qt::UserRole).toString();
    ui->chatName->setText(name);
    ui->chatAvatar->setText(name.left(1).toUpper());
    ui->chatStatus->setText(online ? QStringLiteral("在线") : QStringLiteral("离线"));
    ui->chatStatus->setStyleSheet(online
        ? "font-size:11px;color:#34C759;font-weight:500;"
        : "font-size:11px;color:#8E8E93;font-weight:500;");
    ui->btnCall->setEnabled(online);
    ui->chatStack->setCurrentIndex(1);

    // TODO: 后续根据当前联系人从服务器拉取历史消息并刷新消息列表。
    qDebug() << "[MainWidget] contact selected:" << name << "online:" << online;
}

void MainWidget::filterContactList(QListWidget* list, const QString& keyword) {
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        const QString name = item->data(Qt::UserRole).toString();
        item->setHidden(!keyword.isEmpty()
                        && !name.contains(keyword, Qt::CaseInsensitive));
    }
}
