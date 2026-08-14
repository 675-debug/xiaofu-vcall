#include "MainWidget.h"
#include "ui_MainWidget.h"
#include "network/NetworkManager.h"
#include "RecordingPaths.h"
#include <QDebug>
#include <QListWidgetItem>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QIcon>
#include <QColor>
#include <QPainter>
#include <QPaintEvent>
#include <QTime>
#include <QJsonArray>
#include <QLineEdit>
#include <QSettings>
#include <QFileDialog>

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

    ui->labelMyName->setText(QStringLiteral("未登录"));
    ui->labelMyStatus->setText(QStringLiteral("等待登录"));

    // 默认显示空态页，联系人和聊天记录均由服务端动态返回。
    ui->chatStack->setCurrentIndex(0);
    // 设置弹窗默认隐藏（.ui 中已设置，这里再次确认）
    ui->settingsMask->setVisible(false);

    // 联系人菜单定位在被右键点击的条目旁边，避免跳到窗口角落。
    ui->listContacts->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->listOffline->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listContacts, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& position) { showContactMenu(ui->listContacts, position, true); });
    connect(ui->listOffline, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& position) { showContactMenu(ui->listOffline, position, false); });
}

MainWidget::~MainWidget() {
    delete ui;
}

void MainWidget::setNetworkManager(NetworkManager* manager) {
    networkManager = manager;
    if (!networkManager)
        return;

    connect(networkManager, &NetworkManager::chatReceived,
            this, &MainWidget::onChatReceived);
    connect(networkManager, &NetworkManager::historyReceived,
            this, &MainWidget::onHistoryReceived);
    connect(networkManager, &NetworkManager::conversationDeleted,
            this, &MainWidget::onConversationDeleted);
    connect(networkManager, &NetworkManager::allChatsCleared,
            this, &MainWidget::onAllChatsCleared);
    connect(networkManager, &NetworkManager::contactsReceived,
            this, &MainWidget::onContactsReceived);
    connect(networkManager, &NetworkManager::presenceChanged,
            this, &MainWidget::onPresenceChanged);
    connect(networkManager, &NetworkManager::chatSendResult,
            this, &MainWidget::onChatSendResult);
    connect(networkManager, &NetworkManager::friendRequestReceived,
            this, &MainWidget::onFriendRequestReceived);
}

void MainWidget::setCurrentUser(const QString& username) {
    resetSession();
    ui->labelMyName->setText(username);
    ui->labelMyStatus->setText(QStringLiteral("在线"));
    ui->btnAvatar->setText(username.left(1).toUpper());
    ui->labelSettingAccount->setText(username);
    ui->labelSettingNick->setText(username);
    qDebug() << "[Main] current user:" << username;
}

void MainWidget::resetSession() {
    // 不同账号不能复用上一个账号的联系人、会话和右侧聊天页面。
    currentContact.clear();
    currentContactOnline = false;
    contactsByUsername.clear();
    ignoredHistoryPeers.clear();
    ui->listContacts->clear();
    ui->listOffline->clear();
    clearMessageList();
    ui->editMessage->clear();
    ui->chatStack->setCurrentIndex(0);
    ui->labelSettingAccount->setText(QStringLiteral("未登录"));
    ui->labelSettingNick->setText(QStringLiteral("未登录"));
    qDebug() << "[Main] workspace session reset";
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
    ui->editRecordDir->setText(recordSaveDirectory());
    ui->settingsMask->raise();
    ui->settingsMask->setVisible(true);
}

void MainWidget::on_btnChooseRecordDir_clicked() {
    const QString current = recordSaveDirectory();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择录屏保存目录"), current,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return;  // 用户取消选择时不修改原路径
    }
    QSettings().setValue(QStringLiteral("recording/saveDirectory"), dir);
    ui->editRecordDir->setText(dir);
}

QString MainWidget::recordSaveDirectory() const {
    const QString dir = RecordingPaths::configuredOrDefault();
    // 目录不存在时自动创建，不要求用户提前建目录。
    RecordingPaths::ensureDirectory(dir);
    return dir;
}

void MainWidget::on_btnAddContact_clicked() {
    if (!networkManager || networkManager->currentUsername().isEmpty() || !networkManager->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("添加联系人"),
                             QStringLiteral("当前未连接到服务器，请重新登录后再试。"));
        return;
    }
    ui->editAddContact->clear();
    ui->addContactMask->setGeometry(rect());
    ui->addContactPanel->move((width() - ui->addContactPanel->width()) / 2,
                              (height() - ui->addContactPanel->height()) / 2);
    ui->addContactMask->raise();
    ui->addContactMask->show();
    ui->editAddContact->setFocus();
}

void MainWidget::on_btnAddContactCancel_clicked() {
    ui->addContactMask->hide();
}

void MainWidget::on_btnAddContactConfirm_clicked() {
    const QString username = ui->editAddContact->text().trimmed();
    if (username.isEmpty() || !networkManager)
        return;
    ui->addContactMask->hide();
    networkManager->addContact(username);
}

void MainWidget::on_editAddContact_textChanged(const QString& text) {
    ui->btnAddContactConfirm->setEnabled(!text.trimmed().isEmpty());
}


void MainWidget::on_btnLogout_clicked() {
    // TODO: 通知服务器下线并回到登录页
    ui->settingsMask->setVisible(false);
    emit logoutRequested();
}

void MainWidget::on_btnCloseModal_clicked() {
    ui->settingsMask->setVisible(false);
}

void MainWidget::on_btnClearChatRecords_clicked() {
    if (!networkManager || networkManager->currentUsername().isEmpty())
        return;

    const auto answer = QMessageBox::question(this, QStringLiteral("清空聊天记录"),
                                               QStringLiteral("确定清空所有聊天记录吗？此操作不可恢复。"),
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);
    if (answer == QMessageBox::Yes)
        networkManager->clearAllChats();
}

void MainWidget::on_btnSend_clicked() {
    appendLocalMessage();
}

void MainWidget::on_btnCall_clicked() {
    if (currentContactOnline && !currentContact.isEmpty())
        emit switchToCall(currentContact);
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
    if (text.isEmpty() || currentContact.isEmpty() || !networkManager)
        return;

    // 先显示本人气泡，再由服务端保存并向在线联系人实时推送。
    networkManager->sendChat(currentContact, text);
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
    ContactInfo info = contactsByUsername.value(name);
    if (info.nickname.isEmpty()) {
        info.nickname = name;
        info.online = online;
    }
    currentContact = name;
    qDebug() << "[Main] select contact:" << name << "online:" << online;
    currentContactOnline = online;
    ui->chatName->setText(info.nickname.isEmpty() ? name : info.nickname);
    updateChatAvatar(info.nickname.isEmpty() ? name : info.nickname, info.avatarSeed, online);
    ui->chatStatus->setText(online ? QStringLiteral("在线") : QStringLiteral("离线"));
    ui->chatStatus->setStyleSheet(online
        ? "font-size:11px;color:#34C759;font-weight:500;"
        : "font-size:11px;color:#8E8E93;font-weight:500;");
    ui->btnCall->setEnabled(online);
    ui->chatStack->setCurrentIndex(1);

    clearMessageList();
    if (networkManager)
        networkManager->requestHistory(name);
}

QString MainWidget::avatarColor(int avatarSeed) const {
    static const QStringList colors = {"#10B981", "#F97316", "#8B5CF6", "#007AFF", "#EC4899", "#14B8A6"};
    return colors.at(qAbs(avatarSeed) % colors.size());
}

void MainWidget::updateChatAvatar(const QString& nickname, int avatarSeed, bool online) {
    ui->chatAvatar->setText(nickname.left(1).toUpper());
    ui->chatAvatar->setStyleSheet(QString("background:%1;color:#FFFFFF;border-radius:18px;font-size:14px;font-weight:700;").arg(avatarColor(avatarSeed)));
    QLabel* dot = ui->chatAvatar->findChild<QLabel*>("offlineDot");
    if (!online) {
        if (!dot) {
            dot = new QLabel(ui->chatAvatar);
            dot->setObjectName("offlineDot");
            dot->setFixedSize(10, 10);
            dot->move(27, 27);
            dot->setStyleSheet("background:#FF453A;border:2px solid #FFFFFF;border-radius:5px;");
        }
        dot->show();
    } else if (dot) {
        dot->hide();
    }
}

void MainWidget::renderContacts() {
    ui->listContacts->clear();
    ui->listOffline->clear();
    for (auto it = contactsByUsername.cbegin(); it != contactsByUsername.cend(); ++it) {
        const ContactInfo& info = it.value();
        QListWidget* targetList = info.online ? ui->listContacts : ui->listOffline;
        const QString displayName = info.nickname.isEmpty() ? it.key() : info.nickname;
        addContactItem(targetList, displayName,
                       info.online ? QStringLiteral("在线") : QStringLiteral("离线"), avatarColor(info.avatarSeed));
        targetList->item(targetList->count() - 1)->setData(Qt::UserRole, it.key());
    }
    qDebug() << "[Main] contacts rendered: online=" << ui->listContacts->count()
             << "offline=" << ui->listOffline->count();
}

void MainWidget::addStatusMessage(const QString& text, bool online) {
    auto* item = new QListWidgetItem(ui->listMessages);
    auto* label = new QLabel(text, ui->listMessages);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(online ? "color:#28A65D;font-size:12px;" : "color:#DF3C3C;font-size:12px;");
    item->setSizeHint(QSize(0, 30));
    ui->listMessages->setItemWidget(item, label);
    ui->listMessages->scrollToBottom();
}

void MainWidget::setContactOnlineState(const QString& username, bool online, bool addNotice) {
    if (!contactsByUsername.contains(username)) return;
    ContactInfo info = contactsByUsername.value(username);
    if (info.online == online) return;
    info.online = online;
    contactsByUsername[username] = info;
    renderContacts();
    if (username == currentContact) {
        currentContactOnline = online;
        ui->chatStatus->setText(online ? QStringLiteral("在线") : QStringLiteral("离线"));
        ui->btnCall->setEnabled(online);
        updateChatAvatar(info.nickname.isEmpty() ? username : info.nickname, info.avatarSeed, online);
        if (addNotice) addStatusMessage(online ? QStringLiteral("对方已上线") : QStringLiteral("对方已经离线"), online);
    }
}

void MainWidget::onContactsReceived(const QJsonArray& contacts) {
    contactsByUsername.clear();
    for (const QJsonValue& value : contacts) {
        const QJsonObject object = value.toObject();
        ContactInfo info;
        info.nickname = object.value("nickname").toString();
        info.avatarSeed = object.value("avatarSeed").toInt();
        info.online = object.value("online").toBool();
        contactsByUsername.insert(object.value("username").toString(), info);
    }
    qDebug() << "[Main] contacts response count:" << contacts.size();
    renderContacts();
}

void MainWidget::onPresenceChanged(const QString& username, bool online) {
    setContactOnlineState(username, online, true);
}

void MainWidget::onChatSendResult(const QString& peer, bool online, int code, const QString& message) {
    Q_UNUSED(message);
    if (code == 0)
        setContactOnlineState(peer, online, true);
}

void MainWidget::onFriendRequestReceived(const QString& sender, const QString& nickname, int avatarSeed) {
    if (sender.isEmpty())
        return;
    showFriendRequestPanel(sender, nickname.isEmpty() ? sender : nickname, avatarSeed);
}

void MainWidget::showFriendRequestPanel(const QString& sender, const QString& nickname, int avatarSeed) {
    pendingFriendRequestSender = sender;
    ui->friendRequestAvatar->setText(nickname.left(1).toUpper());
    ui->friendRequestAvatar->setStyleSheet(QStringLiteral("background:%1;").arg(avatarColor(avatarSeed)));
    ui->friendRequestDescription->setText(
        QStringLiteral("%1 请求添加你为好友\n账号：%2").arg(nickname, sender));
    ui->friendRequestMask->setGeometry(rect());
    ui->friendRequestPanel->move((width() - ui->friendRequestPanel->width()) / 2,
                                 (height() - ui->friendRequestPanel->height()) / 2);
    ui->friendRequestMask->raise();
    ui->friendRequestMask->show();
}

void MainWidget::on_btnFriendReject_clicked() {
    ui->friendRequestMask->hide();
    if (networkManager && !pendingFriendRequestSender.isEmpty())
        networkManager->respondToFriendRequest(pendingFriendRequestSender, false);
    pendingFriendRequestSender.clear();
}

void MainWidget::on_btnFriendAccept_clicked() {
    ui->friendRequestMask->hide();
    if (networkManager && !pendingFriendRequestSender.isEmpty())
        networkManager->respondToFriendRequest(pendingFriendRequestSender, true);
    pendingFriendRequestSender.clear();
}

void MainWidget::filterContactList(QListWidget* list, const QString& keyword) {
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        const QString name = item->data(Qt::UserRole).toString();
        item->setHidden(!keyword.isEmpty()
                        && !name.contains(keyword, Qt::CaseInsensitive));
    }
}

void MainWidget::showContactMenu(QListWidget* list, const QPoint& position, bool online) {
    QListWidgetItem* item = list->itemAt(position);
    if (!item)
        return;

    list->setCurrentItem(item);
    const QString contact = item->data(Qt::UserRole).toString();
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu{background:#FFFFFF;border:1px solid #E8E8ED;border-radius:10px;padding:6px;}"
        "QMenu::item{padding:9px 28px 9px 12px;border-radius:7px;color:#1A1A2E;font-size:13px;}"
        "QMenu::item:selected{background:#F0F0F3;}"
        "QMenu::item:disabled{color:#AEAEB2;}");
    QAction* deleteAction = menu.addAction(QStringLiteral("删除聊天记录"));
    QAction* callAction = menu.addAction(QStringLiteral("视频聊天"));
    callAction->setEnabled(online);
    QAction* selectedAction = menu.exec(list->viewport()->mapToGlobal(position));

    if (selectedAction == deleteAction) {
        if (networkManager)
            networkManager->deleteConversation(contact);
        if (contact == currentContact)
            clearMessageList();
    } else if (selectedAction == callAction) {
        currentContact = contact;
        currentContactOnline = true;
        emit switchToCall(contact);
    }
}

void MainWidget::clearMessageList() {
    ui->listMessages->clear();
}

void MainWidget::appendHistoryMessage(const QString& sender, const QString& content, const QString& sentAt) {
    const bool mine = networkManager && sender == networkManager->currentUsername();
    const QString avatarText = mine ? QStringLiteral("A") : sender.left(1).toUpper();
    const QString avatarColor = mine ? QStringLiteral("#007AFF") : QStringLiteral("#10B981");
    const QString displayTime = sentAt.size() >= 5 ? sentAt.right(5) : sentAt;
    addMessageItem(avatarText, avatarColor, content, displayTime, mine);
}

void MainWidget::onChatReceived(const QString& sender, const QString& content, const QString& sentAt) {
    if (sender != currentContact)
        return;
    appendHistoryMessage(sender, content, sentAt);
    ui->listMessages->scrollToBottom();
}

void MainWidget::onHistoryReceived(const QString& peer, const QJsonArray& messages) {
    if (peer != currentContact)
        return;
    if (ignoredHistoryPeers.remove(peer))
        return;
    clearMessageList();
    for (const QJsonValue& value : messages) {
        const QJsonObject message = value.toObject();
        appendHistoryMessage(message.value("from").toString(),
                             message.value("content").toString(),
                             message.value("sentAt").toString());
    }
    ui->listMessages->scrollToBottom();
}

void MainWidget::onConversationDeleted(const QString& peer, int code, const QString& message) {
    Q_UNUSED(message);
    if (code == 0 && peer == currentContact)
        clearMessageList();
}

void MainWidget::onAllChatsCleared(int code, const QString& message) {
    Q_UNUSED(message);
    if (code == 0) {
        ignoredHistoryPeers.insert(currentContact);
        clearMessageList();
        ui->settingsMask->setVisible(false);
    }
}
