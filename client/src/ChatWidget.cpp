#include "ChatWidget.h"
#include "ui_ChatWidget.h"
#include "network/NetworkManager.h"
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QTime>
#include <QVBoxLayout>

ChatWidget::ChatWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::ChatWidget) {
    ui->setupUi(this);

    ui->listMessages->clear();
    addMessageItem("B", "#10B981", "早，Alice！昨晚的会议纪要整理好了吗？", "09:18", false);
    addMessageItem("A", "#007AFF", "早！刚整理完，这就发你。", "09:20", true);
    addMessageItem("B", "#10B981", "好，收到后我们再开视频确认一下。", "09:21", false);
    ui->convList->setCurrentRow(0);
}

ChatWidget::~ChatWidget() {
    delete ui;
}

void ChatWidget::setNetworkManager(NetworkManager* manager) {
    networkManager = manager;
}

void ChatWidget::on_btnSend_clicked() {
    appendLocalMessage();
}

void ChatWidget::on_btnBack_clicked() {
    // TODO: 返回主界面
    emit backToMain();
}

void ChatWidget::on_btnCall_clicked() {
    // TODO: 发起视频通话
    emit startCall();
}

void ChatWidget::on_btnMin_clicked() {
    window()->showMinimized();
}

void ChatWidget::on_btnMax_clicked() {
    if (window()->isMaximized())
        window()->showNormal();
    else
        window()->showMaximized();
}

void ChatWidget::on_btnClose_clicked() {
    window()->close();
}

void ChatWidget::on_editMessage_returnPressed() {
    appendLocalMessage();
}

void ChatWidget::on_convList_itemClicked(QListWidgetItem* item) {
    if (!item)
        return;

    const QStringList parts = item->text().split(" - ");
    const QString name = parts.value(0).trimmed();
    const QString status = parts.value(1, QStringLiteral("在线")).trimmed();
    ui->headName->setText(name);
    ui->headAvatar->setText(name.left(1).toUpper());
    ui->headStatus->setText(status);
    ui->headStatus->setStyleSheet(status == QStringLiteral("在线")
        ? "font-size:11px;color:#34C759;font-weight:500;"
        : "font-size:11px;color:#8E8E93;font-weight:500;");
    ui->btnCall->setEnabled(status == QStringLiteral("在线"));

    // TODO: 后续根据当前联系人从服务器拉取历史消息并刷新消息列表。
    qDebug() << "[ChatWidget] conversation selected:" << name << status;
}

void ChatWidget::appendLocalMessage() {
    const QString text = ui->editMessage->text().trimmed();
    if (text.isEmpty())
        return;

    // TODO: 后续将文本消息封装为协议 JSON 并通过 NetworkManager 发送。
    addMessageItem("A", "#007AFF", text,
                   QTime::currentTime().toString("HH:mm"), true);
    ui->editMessage->clear();
    ui->listMessages->scrollToBottom();
}

void ChatWidget::addMessageItem(const QString& avatarText, const QString& color,
                                const QString& text, const QString& time, bool mine) {
    auto* item = new QListWidgetItem(ui->listMessages);

    auto* row = new QWidget(ui->listMessages);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(24, 8, 24, 8);
    rowLayout->setSpacing(14);

    auto* avatar = new QLabel(avatarText, row);
    avatar->setFixedSize(34, 34);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(QString(
        "background:%1;color:#FFFFFF;border-radius:17px;font-size:13px;font-weight:700;")
        .arg(color));

    auto* bubble = new QLabel(text, row);
    QFont bubbleFont = bubble->font();
    bubbleFont.setPixelSize(13);
    bubble->setFont(bubbleFont);
    bubble->setWordWrap(true);
    // 使用 QLabel 的内容边距参与 sizeHint 计算，避免样式表 padding 导致文字被压缩裁切。
    bubble->setContentsMargins(14, 10, 14, 10);
    bubble->setStyleSheet(mine
        ? "background:#007AFF;color:#FFFFFF;border-radius:12px;"
        : "background:#FFFFFF;color:#1A1A2E;border:1px solid #E8E8ED;border-radius:12px;");

    // Qt 对 wordWrap 标签的默认 sizeHint 偏窄；按真实字宽限定气泡，长文本才换行。
    const int textWidth = bubble->fontMetrics().horizontalAdvance(text);
    const int bubbleWidth = qBound(60, textWidth + 36, 620);
    const int wrappedTextHeight = bubble->fontMetrics()
        .boundingRect(QRect(0, 0, bubbleWidth - 28, 10000),
                      Qt::TextWordWrap, text).height();
    bubble->setFixedSize(bubbleWidth, qMax(40, wrappedTextHeight + 20));

    auto* timeLabel = new QLabel(time, row);
    timeLabel->setStyleSheet("font-size:11px;color:#8E8E93;background:transparent;border:none;");
    timeLabel->setAlignment(mine ? Qt::AlignRight : Qt::AlignLeft);

    auto* content = new QWidget(row);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(5);
    contentLayout->addWidget(bubble, 0, mine ? Qt::AlignRight : Qt::AlignLeft);
    contentLayout->addWidget(timeLabel);

    if (mine) {
        rowLayout->addWidget(content, 1, Qt::AlignRight);
        rowLayout->addWidget(avatar, 0, Qt::AlignTop);
    } else {
        rowLayout->addWidget(avatar, 0, Qt::AlignTop);
        rowLayout->addWidget(content, 1);
    }
    const int contentHeight = bubble->height() + contentLayout->spacing()
        + timeLabel->sizeHint().height();
    item->setSizeHint(QSize(0, qMax(avatar->height(), contentHeight) + 16));
    ui->listMessages->setItemWidget(item, row);
}
