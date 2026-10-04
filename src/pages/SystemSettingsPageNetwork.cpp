/**
 * @file SystemSettingsPageNetwork.cpp
 * @brief 系统设置-网络配置（面板构建/网卡探测/跨平台应用配置）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 SystemSettingsPage.h。
 */

#include "SystemSettingsPage.h"
#include "components/SoftKeyboard.h"
#include "components/NumKeypad.h"
#include "components/BaseDialog.h"  // 统一圆角对话框
#include "utils/StyleHelper.h"
#include "components/FormFactory.h"  // 表单控件工厂（收敛重复lambda）
#include "services/SettingService.h"
#include "services/AuthService.h"
#include "common/AppConfig.h"  // 机组名称配置
#include "common/Logger.h"     // 统一日志写入入口（Log::appendLog）
#include "common/DatabaseManager.h"     // DB写入机组配置
#include "db/RecordDAO.h"               // 校验机组下未归还记录
#include "db/ToolDAO.h"                 // 加载活跃机组列表
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include "components/MessageDialog.h"
#include <QApplication>
#include <QDebug>
#include <QFrame>
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QFocusEvent>
#include <QShowEvent>  // 页面切换还原
#include <QHideEvent>
#include <QTableWidget>  // 任务类型配置表格
#include <QHeaderView>  // 表格列宽控制
#include <QTableWidgetItem>  // 表格单元格
#include <QProcess>  // 调用PowerShell设置显示器亮度
#include <QFile>  // WMI结果日志记录
#include <QDateTime>  // 亮度日志时间戳
#include <QDir>  // 备份目录操作
#include <QStorageInfo>  // 跨平台磁盘空间读取
#include <QSysInfo>  // 跨平台系统信息读取
#include <QRegularExpression>  // 解析os-release
#include <QFileInfoList>  // 备份文件清理

QWidget* SystemSettingsPage::createNetworkPanel() {
    auto* panel = new QFrame();
    panel->setObjectName("netPanel");
    panel->setStyleSheet(QString("QFrame#netPanel{background:white;border-radius:12px;border:none;}"));
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(6);  // 6更紧凑
    layout->setContentsMargins(20, 10, 20, 10);  // 24,20,10

    // 标题不占满宽度，左对齐+主色底边细线分隔
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("网络参数配置"));
    // 标题样式调整：去掉padding-bottom，减小font-size
    title->setStyleSheet(StyleHelper::sectionTitle());
    titleRow->addWidget(title);
    titleRow->addStretch();
    layout->addLayout(titleRow);
    // 细线分隔标题和内容区
    auto* sep = new QFrame(); sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(StyleHelper::separatorLine());
    layout->addWidget(sep);

    auto* form = new QFormLayout();
    form->setSpacing(8);  // 8紧凑
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);  // 表单字段自动扩展
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);  // 标签右对齐
    QString labelStyle = QString("font-size:14px;font-weight:600;color:%1;background:transparent;").arg(StyleHelper::textColor());  // 14
    auto makeLabel = [&](const QString& text) {
        auto* l = new QLabel(text); l->setStyleSheet(labelStyle); l->setMinimumHeight(36); l->setFixedWidth(100); return l;  // 限宽100px，防止标签列过宽
    };

    // 使用Web端小尺寸settingLineEdit：14px/38px高
    m_ipEdit = new QLineEdit(SC::NET_IP);
    m_ipEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(FormFactory::formLabel(QStringLiteral("IP 地址")), m_ipEdit);

    m_maskEdit = new QLineEdit(SC::NET_MASK);
    m_maskEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(FormFactory::formLabel(QStringLiteral("子网掩码")), m_maskEdit);

    m_gatewayEdit = new QLineEdit(SC::NET_GATEWAY);
    m_gatewayEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(FormFactory::formLabel(QStringLiteral("默认网关")), m_gatewayEdit);

    m_dnsEdit = new QLineEdit(SC::NET_DNS);
    m_dnsEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(FormFactory::formLabel(QStringLiteral("DNS 服务器")), m_dnsEdit);

    // 网口速率：按钮组替代QComboBox
    // 统一按钮尺寸：44px高/Preferred策略不Expanding撑满
    auto makeSpeedBtn = [&](const QString& text, int mode) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(StyleHelper::Token::ControlHeightCompactInput);
        btn->setMinimumWidth(110);
        btn->setMaximumWidth(220);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (mode == 0) btn->setChecked(true);
        connect(btn, &QPushButton::clicked, this, [this, mode]() {
            m_speedMode = mode;
            updateSpeedBtnStyles();
        });
        return btn;
    };
    m_speedBtn1 = makeSpeedBtn(QStringLiteral("1000M 自适应"), 0);
    m_speedBtn2 = makeSpeedBtn(QStringLiteral("100M 全双工"), 1);
    auto* speedBtnGroup = new QWidget();
    speedBtnGroup->setStyleSheet("background:transparent;");
    auto* speedBtnLayout = new QHBoxLayout(speedBtnGroup);
    speedBtnLayout->setContentsMargins(0, 0, 0, 0);
    speedBtnLayout->setSpacing(8);
    speedBtnLayout->addWidget(m_speedBtn1);
    speedBtnLayout->addWidget(m_speedBtn2);
    updateSpeedBtnStyles();
    form->addRow(FormFactory::formLabel(QStringLiteral("网口速率")), speedBtnGroup);

    // 组网模式：按钮组替代QComboBox
    // 统一按钮尺寸：44px高/Preferred策略
    auto makeModeBtn = [&](const QString& text, int mode) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(StyleHelper::Token::ControlHeightCompactInput);
        btn->setMinimumWidth(110);
        btn->setMaximumWidth(220);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (mode == 0) btn->setChecked(true);
        connect(btn, &QPushButton::clicked, this, [this, mode]() {
            m_networkMode = mode;
            updateModeBtnStyles();
        });
        return btn;
    };
    m_modeBtn1 = makeModeBtn(QStringLiteral("单机运行"), 0);
    m_modeBtn2 = makeModeBtn(QStringLiteral("组网管理"), 1);
    auto* modeBtnGroup = new QWidget();
    modeBtnGroup->setStyleSheet("background:transparent;");
    auto* modeBtnLayout = new QHBoxLayout(modeBtnGroup);
    modeBtnLayout->setContentsMargins(0, 0, 0, 0);
    modeBtnLayout->setSpacing(8);
    modeBtnLayout->addWidget(m_modeBtn1);
    modeBtnLayout->addWidget(m_modeBtn2);
    updateModeBtnStyles();
    form->addRow(FormFactory::formLabel(QStringLiteral("组网模式")), modeBtnGroup);

    m_serverEdit = new QLineEdit();
    m_serverEdit->setPlaceholderText(QStringLiteral("组网管理时配置"));
    m_serverEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(FormFactory::formLabel(QStringLiteral("服务器地址")), m_serverEdit);

    layout->addLayout(form);

    // 保存栏：右对齐+顶部分割线，48px按钮
    auto* saveBar = new QFrame();
    saveBar->setStyleSheet(StyleHelper::saveBarSeparator());
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 8, 0, 0);  // 8
    saveBarLayout->addStretch();
    auto* saveBtn = new QPushButton(QStringLiteral("保存网络设置"));
    saveBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &SystemSettingsPage::onSaveNetwork);
    saveBarLayout->addWidget(saveBtn);
    layout->addWidget(saveBar);

    // installFocusEvents已移除
    return panel;
}

// 获取第一块有线网卡名称
// Windows: 用 netsh interface show interface 获取，过滤掉 Loopback/虚拟网卡
// 麒麟: 用 ip -o link show 获取，过滤掉 lo/wlan/docker/br/veth 等虚拟/无线网卡
// 返回网卡名称用于后续网络配置命令定位
QString SystemSettingsPage::detectWiredInterfaceName() {
    QString ifName;

#ifdef Q_OS_WIN
    // Windows: netsh interface show interface 获取网卡列表
    QProcess proc;
    proc.start("netsh", QStringList() << "interface" << "show" << "interface");
    proc.waitForFinished(5000);
    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    // 解析输出：跳过表头，取第一个非"Loopback"的网卡名
    QStringList lines = output.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        // 跳过表头行
        if (trimmed.contains("Admin State") || trimmed.contains("管理员状态")) continue;
        // 跳过回环
        if (trimmed.contains("Loopback", Qt::CaseInsensitive)) continue;
        // 取最后一列作为网卡名（netsh输出格式：状态 状态 类型 接口名称）
        QStringList parts = trimmed.split(QRegularExpression("\\s+"));
        if (parts.size() >= 4) {
            // 取第4列开始的所有部分作为接口名（名称可能含空格）
            ifName = parts.mid(3).join(" ");
            if (!ifName.isEmpty()) break;
        }
    }
#else
    // 麒麟: ip -o link show 获取网卡列表，过滤虚拟/无线网卡
    QProcess proc;
    proc.start("ip", QStringList() << "-o" << "link" << "show");
    proc.waitForFinished(5000);
    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    QStringList lines = output.split('\n');
    for (const QString& line : lines) {
        // 格式: "2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 ..."
        QRegularExpression re("(\\d+):\\s+(\\w+):");
        QRegularExpressionMatch match = re.match(line);
        if (!match.hasMatch()) continue;
        QString name = match.captured(2);
        // 过滤虚拟/无线网卡，只保留有线网卡
        if (name == "lo" || name.startsWith("wlan") || name.startsWith("wlp") ||
            name.startsWith("docker") || name.startsWith("br-") ||
            name.startsWith("veth") || name.startsWith("virbr") ||
            name.startsWith("tun") || name.startsWith("tap")) {
            continue;
        }
        // 优先返回 eth* 或 en* 格式的有线网卡名
        if (name.startsWith("eth") || name.startsWith("en") ||
            name.startsWith("em") || name.startsWith("p")) {
            ifName = name;
            break;
        }
    }
#endif

    if (ifName.isEmpty()) {
        qWarning() << "[Network] No wired interface detected";
    } else {
        qInfo() << "[Network] Detected wired interface:" << ifName;
    }
    return ifName;
}

// 跨平台配置有线网卡
// Windows: netsh interface ip set address/dns（需管理员权限）
// 麒麟: ip addr add + ip route add + resolvconf（需root）
// 异步执行，失败静默处理并记录日志
void SystemSettingsPage::applyNetworkConfig(const QString& ip, const QString& mask,
                                            const QString& gateway, const QString& dns) {
    // 基础校验：IP不能为空
    if (ip.isEmpty() || ip == "0.0.0.0") {
        qWarning() << "[Network] Invalid IP address:" << ip;
        return;
    }

    // 日志路径跨平台
    QString logPath;
#ifdef Q_OS_WIN
    logPath = QStringLiteral("d:/CFDZ/smartCabinet/trunk/code/temp/network.log");
#else
    logPath = QStringLiteral("/tmp/smartcabinet_network.log");
#endif

    auto writeNetLog = [logPath](const QString& action, const QString& result, const QString& detail) {
        Log::appendLog(logPath, QStringLiteral("action=%1 result=%2 detail=%3")
                                   .arg(action).arg(result).arg(detail));
    };

    // 检测有线网卡名称
    QString ifName = detectWiredInterfaceName();
    if (ifName.isEmpty()) {
        writeNetLog(QStringLiteral("DETECT"), QStringLiteral("FAIL"),
                    QStringLiteral("no wired interface found"));
        qWarning() << "[Network] Cannot apply config: no wired interface detected";
        return;
    }

    // 计算子网掩码前缀长度（如 255.255.255.0 → 24）
    int prefixLen = 24;
    if (mask == "255.255.255.0") prefixLen = 24;
    else if (mask == "255.255.0.0") prefixLen = 16;
    else if (mask == "255.0.0.0") prefixLen = 8;
    else if (mask == "255.255.255.128") prefixLen = 25;
    else if (mask == "255.255.255.192") prefixLen = 26;
    else {
        // 通用计算：统计mask中1的位数
        QStringList octets = mask.split('.');
        if (octets.size() == 4) {
            int bits = 0;
            for (const QString& oct : octets) {
                int val = oct.toInt();
                for (int i = 7; i >= 0; --i) {
                    if (val & (1 << i)) bits++;
                    else break;
                }
            }
            if (bits > 0 && bits <= 32) prefixLen = bits;
        }
    }

    writeNetLog(QStringLiteral("DETECT"), QStringLiteral("OK"),
                QStringLiteral("interface=%1 ip=%2/%3 gw=%4 dns=%5")
                    .arg(ifName).arg(ip).arg(prefixLen).arg(gateway).arg(dns));

#ifdef Q_OS_WIN
    // ═══════════ Windows: netsh 配置IP/DNS/Gateway ═══════════
    // netsh interface ip set address name="<ifName>" static <ip> <mask> <gateway> 1
    // netsh interface ip set dns name="<ifName>" static <dns> primary

    // 异步执行IP配置
    auto* ipProc = new QProcess(this);
    connect(ipProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, ifName, dns, writeNetLog](int exitCode, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        p->deleteLater();

        if (exitCode == 0) {
            writeNetLog(QStringLiteral("SET_IP"), QStringLiteral("OK"),
                        QStringLiteral("interface=%1").arg(ifName));
            qInfo() << "[Network] IP config applied to" << ifName;

            // IP配置成功后，配置DNS
            auto* dnsProc = new QProcess(this);
            connect(dnsProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, ifName, dns, writeNetLog](int exitCodeDns, QProcess::ExitStatus) {
                auto* pd = qobject_cast<QProcess*>(sender());
                if (!pd) return;
                pd->deleteLater();
                if (exitCodeDns == 0) {
                    writeNetLog(QStringLiteral("SET_DNS"), QStringLiteral("OK"),
                                QStringLiteral("dns=%1").arg(dns));
                    qInfo() << "[Network] DNS config applied:" << dns;
                } else {
                    writeNetLog(QStringLiteral("SET_DNS"), QStringLiteral("FAIL"),
                                QStringLiteral("exitCode=%1").arg(exitCodeDns));
                    qWarning() << "[Network] DNS config failed, exitCode:" << exitCodeDns;
                }
            });
            dnsProc->start("netsh", QStringList() << "interface" << "ip" << "set" << "dns"
                         << QStringLiteral("name=\"%1\"").arg(ifName)
                         << "static" << dns << "primary");
            if (!dnsProc->waitForStarted(3000)) {
                qWarning() << "[Network] Failed to start DNS config";
                dnsProc->deleteLater();
            }
        } else {
            writeNetLog(QStringLiteral("SET_IP"), QStringLiteral("FAIL"),
                        QStringLiteral("exitCode=%1").arg(exitCode));
            qWarning() << "[Network] IP config failed, exitCode:" << exitCode;
        }
    });

    ipProc->start("netsh", QStringList() << "interface" << "ip" << "set" << "address"
                 << QStringLiteral("name=\"%1\"").arg(ifName)
                 << "static" << ip << mask << gateway << "1");
    if (!ipProc->waitForStarted(3000)) {
        qWarning() << "[Network] Failed to start IP config";
        writeNetLog(QStringLiteral("SET_IP"), QStringLiteral("START_FAILED"),
                    QStringLiteral("cannot start netsh"));
        ipProc->deleteLater();
    }

#else
    // ═══════════ 麒麟Linux: ip + resolvconf 配置IP/DNS/Gateway ═══════════
    // ip addr flush dev <ifName>
    // ip addr add <ip>/<prefix> dev <ifName>
    // ip route add default via <gateway> dev <ifName>
    // echo "nameserver <dns>" > /etc/resolv.conf

    // 用 sh 脚本一次性执行所有网络配置命令（需root，通过pkexec提权）
    // 如果pkexec不可用则直接用sh（可能因权限不足失败，记录日志）
    QString netScript = QStringLiteral(
        "# 网络配置脚本 - 仅配置有线网卡 %1\n"
        "IFACE=\"%1\"\n"
        "IP=\"%2\"\n"
        "PREFIX=%3\n"
        "GW=\"%4\"\n"
        "DNS=\"%5\"\n"
        "# 清除旧IP配置\n"
        "ip addr flush dev $IFACE 2>/dev/null\n"
        "# 设置新IP\n"
        "ip addr add $IP/$PREFIX dev $IFACE 2>/dev/null\n"
        "# 启用网卡\n"
        "ip link set $IFACE up 2>/dev/null\n"
        "# 设置默认网关（先删除旧的再添加新的）\n"
        "ip route del default 2>/dev/null\n"
        "ip route add default via $GW dev $IFACE 2>/dev/null\n"
        "# 设置DNS\n"
        "if [ -d /etc/resolvconf ]; then "
        "  echo \"nameserver $DNS\" | resolvconf -a $IFACE 2>/dev/null; "
        "else "
        "  echo \"nameserver $DNS\" > /etc/resolv.conf 2>/dev/null; "
        "fi\n"
        "# 麒麟系统持久化网络配置（写入netplan或NetworkManager）\n"
        "if command -v nmcli >/dev/null 2>&1; then "
        "  nmcli con modify $IFACE ipv4.addresses $IP/$PREFIX 2>/dev/null; "
        "  nmcli con modify $IFACE ipv4.gateway $GW 2>/dev/null; "
        "  nmcli con modify $IFACE ipv4.dns $DNS 2>/dev/null; "
        "  nmcli con modify $IFACE ipv4.method manual 2>/dev/null; "
        "  nmcli con up $IFACE 2>/dev/null; "
        "fi\n"
        "echo 'NETWORK_CONFIG_OK'\n"
    ).arg(ifName).arg(ip).arg(prefixLen).arg(gateway).arg(dns);

    auto* netProc = new QProcess(this);
    connect(netProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, ifName, writeNetLog](int exitCode, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString output = p->readAllStandardOutput().trimmed();
        p->deleteLater();

        if (exitCode == 0 && output.contains("NETWORK_CONFIG_OK")) {
            writeNetLog(QStringLiteral("SET_NETWORK"), QStringLiteral("OK"),
                        QStringLiteral("interface=%1").arg(ifName));
            qInfo() << "[Network] Network config applied to" << ifName;
        } else {
            writeNetLog(QStringLiteral("SET_NETWORK"), QStringLiteral("FAIL"),
                        QStringLiteral("exitCode=%1 output=%2").arg(exitCode).arg(output));
            qWarning() << "[Network] Network config failed, exitCode:" << exitCode << "output:" << output;
        }
    });

    // 优先尝试 pkexec 提权（麒麟系统polkit已集成）
    // 如果pkexec不可用则直接用sh（可能因权限不足失败，记录日志）
    netProc->start("pkexec", QStringList() << "sh" << "-c" << netScript);
    if (!netProc->waitForStarted(3000)) {
        // pkexec不可用，降级用sh直接执行（可能因权限不足失败）
        netProc->deleteLater();
        auto* shProc = new QProcess(this);
        connect(shProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [ifName, writeNetLog](int exitCode, QProcess::ExitStatus) {
            auto* p = qobject_cast<QProcess*>(sender());
            if (!p) return;
            QString output = p->readAllStandardOutput().trimmed();
            p->deleteLater();
            if (exitCode == 0 && output.contains("NETWORK_CONFIG_OK")) {
                writeNetLog(QStringLiteral("SET_NETWORK"), QStringLiteral("OK"),
                            QStringLiteral("interface=%1 (no pkexec)").arg(ifName));
                qInfo() << "[Network] Network config applied (no pkexec) to" << ifName;
            } else {
                writeNetLog(QStringLiteral("SET_NETWORK"), QStringLiteral("FAIL"),
                            QStringLiteral("exitCode=%1 output=%2 (no root)").arg(exitCode).arg(output));
                qWarning() << "[Network] Network config failed (no root), exitCode:" << exitCode;
            }
        });
        shProc->start("sh", QStringList() << "-c" << netScript);
        if (!shProc->waitForStarted(3000)) {
            qWarning() << "[Network] Failed to start network config script";
            writeNetLog(QStringLiteral("SET_NETWORK"), QStringLiteral("START_FAILED"),
                        QStringLiteral("cannot start sh"));
            shProc->deleteLater();
        }
    }
#endif
}
