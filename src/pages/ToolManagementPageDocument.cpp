/**
 * @file ToolManagementPageDocument.cpp
 * @brief 工具管理-工具文档管理（文档Tab构建/上传下载）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 ToolManagementPage.h。
 */

#include "ToolManagementPage.h"
#include "ui_ToolManagementPage.h"
#include "components/PaginationBar.h"
#include "utils/StyleHelper.h"
#include "components/FormFactory.h"  // 表单控件工厂（收敛重复lambda）
#include "controller/ToolController.h"
#include "components/SoftKeyboard.h"
#include "components/MultiSelectFilter.h"
#include "components/BaseDialog.h"     // 统一圆角对话框
#include "common/AppConfig.h"          // 读取本机机组ID
#include "common/Constants.h"          // 识别方式常量
#include "db/RecordDAO.h"              // 查询工具的借用记录
#include <QApplication>                // 屏幕高度自适应
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFormLayout>
#include <QRegularExpression>     // 位置格式化提取数字
#include "components/MessageDialog.h"
#include <QDebug>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QTabWidget>          // 工具详情对话框Tab选项卡
#include <QScrollArea>         // 借用记录Tab滚动支持
#include <QDesktopServices>    // 文档在线浏览(调用系统默认程序)
#include <QUrl>                // 文档URL
#include <QFileInfo>           // 文档文件信息
#include <QFileDialog>         // 文档下载(另存为)
#include <QStandardPaths>      // 文档上传存储路径
#include <QDateTime>           // 文档文件名时间戳
#include <QDir>                // 创建文档存储目录
#include <QTimer>              // 上传后延迟重新打开详情对话框

// 构建详情对话框的工具文档Tab
// 输入: toolId - 工具ID(预留,可用于后续文档替换), docPath - 文档本地路径
// 输出: QWidget* - 文档Tab页面，含下载/在线浏览按钮
// 功能: 无文档时显示上传提示；有文档时显示文件信息+下载+在线浏览按钮
QWidget* ToolManagementPage::createDocumentTab(int toolId, const QString& docPath) {
    Q_UNUSED(toolId);  // 预留：后续可用于文档替换/删除功能
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 16, 0, 0);
    layout->setSpacing(16);
    layout->setAlignment(Qt::AlignTop);

    if (docPath.isEmpty() || !QFileInfo::exists(docPath)) {
        // 无文档 — 友好提示 + 上传入口 
        //  要求详情页可直接上传文档，无需进入入库流程
        auto* emptyCard = new QFrame();
        emptyCard->setStyleSheet(StyleHelper::cardLight());
        auto* emptyLayout = new QVBoxLayout(emptyCard);
        emptyLayout->setContentsMargins(40, 50, 40, 50);
        emptyLayout->setAlignment(Qt::AlignCenter);

        auto* iconLabel = new QLabel(QStringLiteral("📄"));
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setStyleSheet("font-size:48px;background:transparent;");
        emptyLayout->addWidget(iconLabel);

        auto* tipLabel = new QLabel(QStringLiteral("该工具暂未上传文档"));
        tipLabel->setAlignment(Qt::AlignCenter);
        tipLabel->setStyleSheet("font-size:16px;color:#999;font-weight:600;background:transparent;");
        emptyLayout->addWidget(tipLabel);

        auto* subLabel = new QLabel(QStringLiteral("支持 doc / docx / pdf 格式，单个文件不超过 50MB"));
        subLabel->setAlignment(Qt::AlignCenter);
        subLabel->setStyleSheet("font-size:13px;color:#bbb;background:transparent;");
        subLabel->setWordWrap(true);
        emptyLayout->addWidget(subLabel);

        // 上传文档按钮 — 详情页直接上传，无需进入入库流程
        auto* uploadBtn = new QPushButton(QStringLiteral("⬆ 上传文档"));
        uploadBtn->setStyleSheet(StyleHelper::buttonPrimary());
        uploadBtn->setCursor(Qt::PointingHandCursor);
        uploadBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
        uploadBtn->setMinimumWidth(160);
        uploadBtn->setMaximumWidth(240);
        // 上传成功后关闭详情对话框并重新打开，刷新文档Tab
        connect(uploadBtn, &QPushButton::clicked, this, [this, toolId, uploadBtn]() {
            if (onUploadDocument(toolId)) {
                QWidget* w = uploadBtn;
                while (w && !w->isWindow()) w = w->parentWidget();
                if (auto* dlg = qobject_cast<QDialog*>(w)) dlg->accept();
                // 上传后重新打开详情——用toolId重新查询位置维度数据
                QTimer::singleShot(0, this, [this, toolId]() {
                    ToolController ctrl;
                    auto pageResult = ctrl.getToolList(1, SC::PAGE_SIZE_UNLIMITED, "", "", "", "", "");
                    for (const auto& ti : pageResult.list) {
                        if (ti.toolId == toolId) { onDetailTool(ti); break; }
                    }
                });
            }
        });
        emptyLayout->addSpacing(16);
        emptyLayout->addWidget(uploadBtn, 0, Qt::AlignCenter);

        layout->addWidget(emptyCard);
        layout->addStretch();
        return tab;
    }

    // 有文档 — 显示文件信息卡片 + 操作按钮
    QFileInfo docInfo(docPath);

    auto* infoCard = new QFrame();
    infoCard->setStyleSheet(StyleHelper::cardLight());
    auto* cardLayout = new QVBoxLayout(infoCard);
    cardLayout->setContentsMargins(24, 24, 24, 24);
    cardLayout->setSpacing(14);

    // 文件图标 + 文件名
    auto* fileRow = new QHBoxLayout();
    fileRow->setSpacing(14);
    auto* fileIcon = new QLabel(QStringLiteral("📄"));
    fileIcon->setStyleSheet("font-size:36px;background:transparent;");
    fileRow->addWidget(fileIcon);

    auto* fileInfoWidget = new QWidget();
    auto* fileInfoLayout = new QVBoxLayout(fileInfoWidget);
    fileInfoLayout->setContentsMargins(0, 0, 0, 0);
    fileInfoLayout->setSpacing(4);

    auto* nameLabel = new QLabel(docInfo.fileName());
    nameLabel->setStyleSheet("font-size:17px;font-weight:700;color:#1a1a2e;background:transparent;");
    nameLabel->setWordWrap(true);
    fileInfoLayout->addWidget(nameLabel);

    // 文件信息行：格式 + 大小
    QString suffix = docInfo.suffix().toUpper();
    qint64 sizeKb = docInfo.size() / 1024;
    QString sizeText = sizeKb > 1024
                       ? QStringLiteral("%1 MB").arg(QString::number(sizeKb / 1024.0, 'f', 1))
                       : QStringLiteral("%1 KB").arg(sizeKb);
    auto* metaLabel = new QLabel(QStringLiteral("%1 格式 · %2").arg(suffix, sizeText));
    metaLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontSmall, StyleHelper::textMuted()));
    fileInfoLayout->addWidget(metaLabel);

    fileRow->addWidget(fileInfoWidget, 1);
    cardLayout->addLayout(fileRow);

    layout->addWidget(infoCard);

    // 操作按钮区
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(12);

    // 下载文档（另存为）
    auto* downloadBtn = new QPushButton(QStringLiteral("⬇ 下载文档"));
    downloadBtn->setStyleSheet(StyleHelper::buttonPrimary());
    downloadBtn->setCursor(Qt::PointingHandCursor);
    downloadBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    downloadBtn->setMinimumWidth(160);
    connect(downloadBtn, &QPushButton::clicked, this, [docPath, docInfo, this]() {
        QString defaultName = docInfo.fileName();
        QString savePath = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存文档"), defaultName,
            QStringLiteral("文档文件 (*.%1)").arg(docInfo.suffix())
        );
        if (savePath.isEmpty()) return;
        if (QFile::exists(savePath)) QFile::remove(savePath);
        if (QFile::copy(docPath, savePath)) {
            MessageDialog::showSuccess(this, QStringLiteral("下载成功"),
                QStringLiteral("文档已保存到：%1").arg(savePath));
        } else {
            MessageDialog::showError(this, QStringLiteral("下载失败"),
                QStringLiteral("文件复制失败，请检查目标路径权限"));
        }
    });
    btnRow->addWidget(downloadBtn);

    // 在线浏览（调用系统默认程序打开）
    auto* viewBtn = new QPushButton(QStringLiteral("👁 在线浏览"));
    viewBtn->setStyleSheet(StyleHelper::buttonOutline());
    viewBtn->setCursor(Qt::PointingHandCursor);
    viewBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    viewBtn->setMinimumWidth(160);
    connect(viewBtn, &QPushButton::clicked, this, [docPath, this]() {
        // QDesktopServices::openUrl 调用系统默认程序打开文档
        // PDF → 系统PDF阅读器, Word → Word/WPS, 跨平台兼容
        bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(docPath));
        if (!ok) {
            MessageDialog::showError(this, QStringLiteral("打开失败"),
                QStringLiteral("无法打开文档，请检查系统是否安装对应的阅读软件"));
        }
    });
    btnRow->addWidget(viewBtn);

    btnRow->addStretch();
    layout->addLayout(btnRow);

    // 提示说明
    auto* tipLabel = new QLabel(QStringLiteral(
        "💡 在线浏览将调用系统默认程序打开文档；下载文档可另存到指定位置"
    ));
    tipLabel->setStyleSheet("font-size:12px;color:#bbb;padding:8px 4px;background:transparent;");
    tipLabel->setWordWrap(true);
    layout->addWidget(tipLabel);

    layout->addStretch();
    return tab;
}

// 详情页上传工具文档
// 输入: toolId - 工具ID
// 输出: bool - true=上传成功, false=用户取消或失败
// 功能: 选文件→校验格式大小→复制到AppData→更新DB document_path
//  校验规则与入库页完全一致，保证一致性
bool ToolManagementPage::onUploadDocument(int toolId) {
    // 文件过滤器：Word文档 + PDF（与入库页一致）
    QString filter = QStringLiteral(
        "文档文件 (*.doc *.docx *.pdf);;"
        "Word 97-2003 文档 (*.doc);;"
        "Word 文档 (*.docx);;"
        "PDF 文件 (*.pdf);;"
        "所有文件 (*.*)"
    );
    QString srcPath = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择工具文档"), QString(), filter
    );
    if (srcPath.isEmpty()) return false;  // 用户取消

    // 校验文件后缀
    QFileInfo srcInfo(srcPath);
    QString suffix = srcInfo.suffix().toLower();
    if (!SC::TOOL_DOC_SUFFIXES.contains(suffix)) {
        MessageDialog::showWarning(this, QStringLiteral("格式不支持"),
            QStringLiteral("仅支持 %1 格式的文档").arg(SC::TOOL_DOC_SUFFIXES.join(" / ")));
        return false;
    }

    // 校验文件大小
    qint64 sizeMb = srcInfo.size() / (1024 * 1024);
    if (sizeMb > SC::TOOL_DOC_MAX_SIZE_MB) {
        MessageDialog::showWarning(this, QStringLiteral("文件过大"),
            QStringLiteral("文档大小不能超过 %1MB，当前 %2MB").arg(SC::TOOL_DOC_MAX_SIZE_MB).arg(sizeMb));
        return false;
    }

    // 存储目录：AppData/SmartCabinet/QtSmartCabinet/tool_documents/（与入库页统一）
    QString docDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                     + "/tool_documents";
    QDir().mkpath(docDir);

    // 生成唯一文件名：时间戳_原文件名，避免冲突
    QString destName = QString("%1_%2").arg(
        QDateTime::currentDateTime().toString("yyyyMMddHHmmss"), srcInfo.fileName()
    );
    QString destPath = docDir + "/" + destName;

    // 复制文件
    if (QFile::exists(destPath)) QFile::remove(destPath);
    if (!QFile::copy(srcPath, destPath)) {
        MessageDialog::showError(this, QStringLiteral("上传失败"),
            QStringLiteral("文件复制失败，请检查磁盘空间或权限"));
        return false;
    }

    // 更新DB document_path字段
    ToolController ctrl;
    if (!ctrl.updateToolDocument(toolId, destPath)) {
        MessageDialog::showError(this, QStringLiteral("上传失败"),
            QStringLiteral("文档路径更新失败，请稍后重试"));
        QFile::remove(destPath);  // 回滚已复制的文件
        return false;
    }

    MessageDialog::showSuccess(this, QStringLiteral("上传成功"),
        QStringLiteral("工具文档已上传，可在此Tab下载或在线浏览"));
    return true;
}
