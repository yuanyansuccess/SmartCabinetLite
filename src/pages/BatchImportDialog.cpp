/**
 * @file BatchImportDialog.cpp
 * @brief 批量导入用户对话框实现 — 继承BaseDialog圆角风格
 * @author 袁燕
 * @date 2026-06-26
 */
#include "BatchImportDialog.h"
#include "controller/UserController.h"
#include "components/MessageDialog.h"
#include "utils/StyleHelper.h"
#include "db/UserDAO.h"
#include "common/DatabaseManager.h"
#include "common/Constants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStandardPaths>
#include <QApplication>
#include <QProcess>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>
#include <QTemporaryFile>
#include <QFrame>

// ── 前置工具函数：CSV行解析（处理逗号分隔和引号包裹）──
static QStringList parseCsvLine(const QString& line)
{
    QStringList fields;
    QString field;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        QChar ch = line[i];
        if (inQuotes) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += ch;
            }
        } else {
            if (ch == '"') {
                inQuotes = true;
            } else if (ch == ',' || ch == '\t') {
                fields.append(field.trimmed());
                field.clear();
            } else {
                field += ch;
            }
        }
    }
    fields.append(field.trimmed());
    return fields;
}

// ── CSV字段转义 ──
static QString csvEscape(const QString& field)
{
    if (field.contains(',') || field.contains('"') || field.contains('\n')) {
        QString escaped = field;
        escaped.replace('"', "\"\"");
        return '"' + escaped + '"';
    }
    return field;
}

// ── Windows PowerShell 将 xlsx 转为 CSV ──
#ifdef Q_OS_WIN
static bool convertXlsxToCsv(const QString& xlsxPath, QString& csvPath)
{
    QTemporaryFile tmpFile(QDir::tempPath() + "/smartcabinet_import_XXXXXX.csv");
    tmpFile.setAutoRemove(false);
    if (!tmpFile.open()) return false;
    csvPath = tmpFile.fileName();
    tmpFile.close();

    QString psScript = QString(
        "$excel = New-Object -ComObject Excel.Application;"
        "$excel.Visible = $false;"
        "$excel.DisplayAlerts = $false;"
        "try {"
        "  $wb = $excel.Workbooks.Open('%1');"
        "  $wb.SaveAs('%2', 6);"
        "  $wb.Close();"
        "  $success = $true;"
        "} catch {"
        "  $success = $false;"
        "} finally {"
        "  $excel.Quit();"
        "  [System.Runtime.Interopservices.Marshal]::ReleaseComObject($excel) | Out-Null;"
        "  exit ( $success ? 0 : 1 );"
        "}"
    ).arg(QDir::toNativeSeparators(xlsxPath), QDir::toNativeSeparators(csvPath));

    QProcess ps;
    ps.start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", psScript});
    ps.waitForFinished(30000);

    if (ps.exitCode() != 0 || !QFile::exists(csvPath)) {
        return false;
    }
    return true;
}
#endif

// 模板表头（6列，不含人脸录入）
const QStringList BatchImportDialog::TEMPLATE_HEADERS = {
    QStringLiteral("工号"),
    QStringLiteral("姓名"),
    QStringLiteral("部门"),
    QStringLiteral("角色"),
    QStringLiteral("联系电话"),
    QStringLiteral("状态")
};

const QMap<QString, QString> BatchImportDialog::ROLE_MAP = {
    {QStringLiteral("管理员"),   "admin"},
    {QStringLiteral("普通用户"), "user"}
};

const QMap<QString, QString> BatchImportDialog::STATUS_MAP = {
    {QStringLiteral("启用"), "active"},
    {QStringLiteral("禁用"), "disabled"}
};

BatchImportDialog::BatchImportDialog(QWidget* parent)
    : BaseDialog(parent, 520)
{
    setDialogTitle(QStringLiteral("批量导入用户"));
    setupContent();
    // 调整窗口高度以适应内容
    adjustSize();
    setMinimumHeight(460);
    setMaximumHeight(560);
}

void BatchImportDialog::setupContent()
{
    auto* cl = contentLayout();

    // ── 步骤1：下载模板 ──
    auto* step1Label = new QLabel(QStringLiteral("步骤一：下载Excel模板"));
    step1Label->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    cl->addWidget(step1Label);

    auto* step1Hint = new QLabel(QStringLiteral("请先下载模板，按格式填写人员信息后保存。\n支持 Excel / WPS 打开编辑，保存为 .xlsx 或 .csv 格式均可。"));
    step1Hint->setStyleSheet("font-size:13px;color:#888;background:transparent;padding-bottom:2px;");
    step1Hint->setWordWrap(true);
    cl->addWidget(step1Hint);

    auto* downloadBtn = new QPushButton(QStringLiteral("  下载Excel模板"));
    downloadBtn->setMinimumHeight(48);
    downloadBtn->setCursor(Qt::PointingHandCursor);
    downloadBtn->setStyleSheet(
        "QPushButton{background:#e8f5e9;color:#2e7d32;border:2px solid #4caf50;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#c8e6c9;}"
        "QPushButton:pressed{transform:scale(0.97);}"
    );
    connect(downloadBtn, &QPushButton::clicked, this, &BatchImportDialog::onDownloadTemplate);
    cl->addWidget(downloadBtn);

    // ── 分隔线 ──
    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background:#eef0f3;max-height:1px;margin:4px 0;");
    cl->addWidget(sep);

    // ── 步骤2：上传文件 ──
    auto* step2Label = new QLabel(QStringLiteral("步骤二：上传编辑后的文件"));
    step2Label->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    cl->addWidget(step2Label);

    auto* fileRow = new QHBoxLayout();
    fileRow->setSpacing(10);

    m_filePathEdit = new QLineEdit();
    m_filePathEdit->setReadOnly(true);
    m_filePathEdit->setPlaceholderText(QStringLiteral("请选择已编辑的Excel或CSV文件..."));
    m_filePathEdit->setMinimumHeight(48);
    m_filePathEdit->setStyleSheet(
        "QLineEdit{padding:0 14px;border:2px solid #e0e0e0;border-radius:12px;"
        "font-size:14px;background:#fafafa;color:#333;}"
    );

    auto* selectFileBtn = new QPushButton(QStringLiteral("选择文件"));
    selectFileBtn->setMinimumHeight(48);
    selectFileBtn->setMinimumWidth(100);
    selectFileBtn->setCursor(Qt::PointingHandCursor);
    selectFileBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#3d8ae0;}"
    );
    connect(selectFileBtn, &QPushButton::clicked, this, &BatchImportDialog::onSelectFile);

    fileRow->addWidget(m_filePathEdit, 1);
    fileRow->addWidget(selectFileBtn);
    cl->addLayout(fileRow);

    m_fileStatusLabel = new QLabel();
    m_fileStatusLabel->setStyleSheet("font-size:12px;color:#999;background:transparent;");
    m_fileStatusLabel->setWordWrap(true);
    cl->addWidget(m_fileStatusLabel);

    // ── 底部按钮（通过 BaseDialog::buttonLayout() 添加） ──
    auto* bl = buttonLayout();
    // 清空 stretch
    while (bl->count() > 0) {
        QLayoutItem* item = bl->takeAt(0);
        delete item;
    }
    bl->addStretch();

    m_cancelBtn = new QPushButton(QStringLiteral("取消"));
    m_cancelBtn->setMinimumHeight(48);
    m_cancelBtn->setMinimumWidth(110);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#f5f5f5;}"
    );
    connect(m_cancelBtn, &QPushButton::clicked, this, &BatchImportDialog::onCancel);

    m_confirmBtn = new QPushButton(QStringLiteral("确认导入"));
    m_confirmBtn->setMinimumHeight(48);
    m_confirmBtn->setMinimumWidth(120);
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setEnabled(false);
    m_confirmBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "font-size:15px;font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:disabled{background:#c0c0c0;color:#fff;}"
    );
    connect(m_confirmBtn, &QPushButton::clicked, this, &BatchImportDialog::onConfirm);

    bl->addWidget(m_cancelBtn);
    bl->addWidget(m_confirmBtn);
}

// ── 以下方法与原实现一致 ──

void BatchImportDialog::onDownloadTemplate()
{
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                          + "/用户导入模板.csv";

    QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存导入模板"),
        defaultPath,
        QStringLiteral("CSV文件 (*.csv);;所有文件 (*.*)")
    );

    if (filePath.isEmpty()) return;

    if (generateTemplate(filePath)) {
        m_fileStatusLabel->setStyleSheet("font-size:13px;color:#2e7d32;font-weight:600;background:transparent;");
        m_fileStatusLabel->setText(QStringLiteral("模板已下载到：%1\n(可用Excel/WPS打开编辑，编辑后保存为.xlsx或.csv均可导入)")
            .arg(filePath));

        m_filePathEdit->setText(filePath);
        m_filePath = filePath;
        m_confirmBtn->setEnabled(true);

#ifdef Q_OS_WIN
        QProcess::startDetached("explorer", {"/select,", QDir::toNativeSeparators(filePath)});
#else
        QProcess::startDetached("xdg-open", {filePath});
#endif
    } else {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("模板生成失败，请重试"));
    }
}

bool BatchImportDialog::generateTemplate(const QString& filePath)
{
    QFile file(filePath);
    // [V7.3 2026-06-26] 修复编码问题：不用QTextStream(默认GBK)，直接用write写入UTF-8字节流
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    // UTF-8 BOM
    file.write("\xEF\xBB\xBF");

    QStringList escapedHeaders;
    for (const auto& h : TEMPLATE_HEADERS) {
        escapedHeaders.append(csvEscape(h));
    }
    file.write((escapedHeaders.join(",") + "\n").toUtf8());

    // [2026-09-23] 工号自增：查询DB中已存在的最大数字工号，生成5行递增示例数据
    //   工号规则：纯数字3位补零（001~999，超999自然进位4位）
    //   生成5行示例（008~012 假设当前最大是007），用户可直接修改或删除示例行
    QStringList exampleDepartments = {
        QStringLiteral("技术部"), QStringLiteral("维修一部"), QStringLiteral("维修二部"),
        QStringLiteral("质检部"), QStringLiteral("维修三部")
    };
    QStringList exampleNames = {
        QStringLiteral("张三"), QStringLiteral("李四"), QStringLiteral("王五"),
        QStringLiteral("赵六"), QStringLiteral("钱七")
    };
    int startIdx = 1;  // 默认从1开始（DB无数字工号时）
    QString nextWorkNo = generateNextWorkNo();
    if (!nextWorkNo.isEmpty()) {
        // 解析纯数字工号
        static QRegularExpression re("^\\d+$");
        if (re.match(nextWorkNo).hasMatch()) {
            startIdx = nextWorkNo.toInt();  // generateNextWorkNo已返回最大+1
        }
    }
    // 生成5行递增示例数据
    for (int i = 0; i < 5; ++i) {
        int num = startIdx + i;
        if (num > 999) break;  // 超过3位数停止
        QString workNo = QStringLiteral("%1").arg(num, 3, 10, QChar('0'));
        QString name = (i < exampleNames.size()) ? exampleNames[i] : QStringLiteral("用户%1").arg(num);
        QString dept = (i < exampleDepartments.size()) ? exampleDepartments[i] : QStringLiteral("技术部");
        QString phone = QStringLiteral("138%1%2").arg(num, 4, 10, QChar('0')).arg(num, 4, 10, QChar('0')).right(8);
        file.write((workNo + "," + csvEscape(name) + ","
            + csvEscape(dept) + ","
            + csvEscape(QStringLiteral("普通用户")) + ","
            + phone + ","
            + csvEscape(QStringLiteral("启用")) + "\n").toUtf8());
    }

    file.close();
    return true;
}

// 查询DB中最大数字工号，返回下一个可用工号（001~999），委托UserDAO执行
QString BatchImportDialog::generateNextWorkNo() const
{
    db::UserDAO userDao;
    return userDao.generateNextWorkNo();
}

void BatchImportDialog::onSelectFile()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择导入文件"),
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
        QStringLiteral("Excel/CSV文件 (*.xlsx *.xls *.csv);;所有文件 (*.*)")
    );

    if (filePath.isEmpty()) return;

    m_filePath = filePath;
    m_filePathEdit->setText(filePath);

    QFileInfo fi(filePath);
    m_fileStatusLabel->setStyleSheet("font-size:13px;color:#1565c0;font-weight:600;background:transparent;");
    m_fileStatusLabel->setText(QStringLiteral("已选择：%1 (%2 KB)").arg(fi.fileName()).arg(fi.size() / 1024));

    m_confirmBtn->setEnabled(true);
}

void BatchImportDialog::onConfirm()
{
    if (m_filePath.isEmpty()) {
        MessageDialog::showWarning(this, QStringLiteral("提示"), QStringLiteral("请先选择要导入的文件"));
        return;
    }

    if (!QFile::exists(m_filePath)) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("文件不存在，请重新选择"));
        return;
    }

    ImportResult result = parseExcel(m_filePath);

    if (!result.errors.isEmpty()) {
        QString errorMsg = QStringLiteral("文件校验发现 %1 个问题：\n\n").arg(result.errors.size());
        int showCount = qMin(result.errors.size(), 8);
        for (int i = 0; i < showCount; ++i) {
            errorMsg += QStringLiteral("  · %1\n").arg(result.errors[i]);
        }
        if (result.errors.size() > 8) {
            errorMsg += QStringLiteral("  ... 还有 %1 条错误\n").arg(result.errors.size() - 8);
        }
        errorMsg += QStringLiteral("\n请修正后重新导入");
        MessageDialog::showError(this, QStringLiteral("校验失败"), errorMsg);
        return;
    }

    if (m_parsedRows.isEmpty()) {
        MessageDialog::showWarning(this, QStringLiteral("提示"),
            QStringLiteral("未解析到任何有效数据行。请确认文件中至少有一行数据（不含表头）"));
        return;
    }

    QString confirmMsg = QStringLiteral("即将导入 %1 条用户数据，确认导入？\n\n"
                                        "导入将自动：\n"
                                        "  · 以工号作为登录账号\n"
                                        "  · 默认密码为 123456\n"
                                        "  · 自动创建系统账户")
                             .arg(m_parsedRows.size());

    if (!MessageDialog::showQuestion(this, QStringLiteral("确认导入"), confirmMsg)) {
        return;
    }

    UserController ctrl;
    int successCount = 0;
    QStringList importErrors;

    // 批量导入前先查询数据库中已存在的工号，用于准确提示
    QStringList existingWorkNos;
    UserController::PageResult allUsers = ctrl.getUserList(1, SC::PAGE_SIZE_UNLIMITED);
    for (const auto& u : allUsers.list) {
        existingWorkNos.append(u.workNo.trimmed());
    }

    // 工号格式校验正则（与UserController::validateUsername一致）[2026-09-23] 改纯数字
    static QRegularExpression usernameRe("^[0-9]{1,32}$");

    for (const auto& row : m_parsedRows) {
        QString workNo = row.workNo.trimmed();

        // 1. 校验工号格式（作为username必须符合格式要求）
        if (!usernameRe.match(workNo).hasMatch()) {
            importErrors.append(QStringLiteral("第%1行：%2(%3) 工号格式不合法（需1-32位纯数字）")
                .arg(row.lineNumber).arg(row.realName).arg(workNo));
            continue;
        }

        // 2. 校验工号是否已存在（数据库+同批次）
        if (existingWorkNos.contains(workNo)) {
            importErrors.append(QStringLiteral("第%1行：%2(%3) 导入失败（工号已存在）")
                .arg(row.lineNumber).arg(row.realName).arg(workNo));
            continue;
        }

        User newUser;
        newUser.username   = workNo;
        newUser.realName   = row.realName;
        newUser.workNo     = workNo;
        newUser.department = row.department;
        newUser.role       = ROLE_MAP.value(row.role, "user");
        newUser.phone      = row.phone;
        newUser.status     = STATUS_MAP.value(row.status, "active");

        int newId = ctrl.createUser(newUser, "123456");
        if (newId > 0) {
            successCount++;
            existingWorkNos.append(workNo);
        } else {
            // createUser返回-1但前面已校验过格式和重复，说明是数据库层面错误
            // 诊断日志已在UserController::createUser中打印
            importErrors.append(QStringLiteral("第%1行：%2(%3) 导入失败（数据库写入异常，请查看日志）")
                .arg(row.lineNumber).arg(row.realName).arg(workNo));
        }
    }

    // [2026-06-26] 修复：不在onConfirm中弹出MessageDialog（嵌套事件循环干扰accept）
    // 直接关闭对话框，结果消息由父页面处理
    if (successCount > 0) {
        // 部分成功或全部成功：关闭对话框，父页面UserManagementPage通过refresh()刷新列表
        if (successCount < m_parsedRows.size()) {
            // 部分失败，但在accept()前弹窗告知（非嵌套，不影响accept）
            QString partialMsg = QStringLiteral("成功导入 %1/%2 条用户").arg(successCount).arg(m_parsedRows.size());
            if (!importErrors.isEmpty()) {
                partialMsg += QStringLiteral("\n\n失败详情：\n");
                for (int i = 0; i < qMin(importErrors.size(), 5); ++i) {
                    partialMsg += QStringLiteral("  · %1\n").arg(importErrors[i]);
                }
                if (importErrors.size() > 5)
                    partialMsg += QStringLiteral("  ... 还有 %1 条错误\n").arg(importErrors.size() - 5);
            }
            MessageDialog::showWarning(this, QStringLiteral("部分导入成功"), partialMsg);
        }
        accept();
    } else {
        // 全部失败：显示具体错误原因并保持对话框打开
        QString errorMsg;
        if (!importErrors.isEmpty()) {
            errorMsg = QStringLiteral("共 %1 条全部导入失败：\n\n").arg(importErrors.size());
            for (int i = 0; i < qMin(importErrors.size(), 8); ++i) {
                errorMsg += QStringLiteral("  · %1\n").arg(importErrors[i]);
            }
            if (importErrors.size() > 8)
                errorMsg += QStringLiteral("  ... 还有 %1 条错误\n").arg(importErrors.size() - 8);
        } else {
            errorMsg = QStringLiteral("所有数据导入失败，请检查工号是否重复或数据格式是否正确");
        }
        errorMsg += QStringLiteral("\n提示：请确认工号未被占用，角色填写\"管理员\"或\"普通用户\"");
        MessageDialog::showError(this, QStringLiteral("导入失败"), errorMsg);
    }
}

void BatchImportDialog::onCancel()
{
    reject();
}

bool BatchImportDialog::validateHeaders(const QStringList& headers)
{
    if (headers.size() < TEMPLATE_HEADERS.size()) {
        return false;
    }
    for (const auto& expected : TEMPLATE_HEADERS) {
        bool found = false;
        for (const auto& actual : headers) {
            if (actual.trimmed() == expected) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

QString BatchImportDialog::validateRow(const ImportRow& row)
{
    if (row.workNo.isEmpty()) {
        return QStringLiteral("第%1行：工号不能为空").arg(row.lineNumber);
    }
    if (row.realName.isEmpty()) {
        return QStringLiteral("第%1行：姓名不能为空").arg(row.lineNumber);
    }
    if (row.department.isEmpty()) {
        return QStringLiteral("第%1行：部门不能为空").arg(row.lineNumber);
    }
    if (!ROLE_MAP.contains(row.role)) {
        return QStringLiteral("第%1行：角色\"%2\"不合法，请填写\"管理员\"或\"普通用户\"")
            .arg(row.lineNumber).arg(row.role);
    }
    if (!row.status.isEmpty() && !STATUS_MAP.contains(row.status)) {
        return QStringLiteral("第%1行：状态\"%2\"不合法，请填写\"启用\"或\"禁用\"")
            .arg(row.lineNumber).arg(row.status);
    }
    if (!row.phone.isEmpty()) {
        static QRegularExpression phoneRe("^1[3-9]\\d{9}$");
        if (!phoneRe.match(row.phone).hasMatch()) {
            return QStringLiteral("第%1行：联系电话\"%2\"格式不正确（应为11位手机号）")
                .arg(row.lineNumber).arg(row.phone);
        }
    }
    return QString();
}

ImportResult BatchImportDialog::parseExcel(const QString& filePath)
{
    ImportResult result;
    m_parsedRows.clear();

    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();
    bool isXlsx = (suffix == "xlsx" || suffix == "xls");

    QString csvFilePath = filePath;

    if (isXlsx) {
#ifdef Q_OS_WIN
        QString tempCsv;
        if (convertXlsxToCsv(filePath, tempCsv)) {
            csvFilePath = tempCsv;
        } else {
            result.errors.append(QStringLiteral("无法解析Excel文件。请确认：\n"
                "  1. 已安装Microsoft Excel或WPS\n"
                "  2. 文件未被其他程序占用\n"
                "  或者将文件另存为CSV格式后重新导入"));
            return result;
        }
#else
        result.errors.append(QStringLiteral("Linux/麒麟系统暂不支持直接导入.xlsx文件，请将文件另存为CSV格式后重新导入"));
        return result;
#endif
    }

    QFile file(csvFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errors.append(QStringLiteral("无法打开文件：%1").arg(csvFilePath));
        return result;
    }

    QByteArray raw = file.readAll();
    file.close();

    // [2026-06-26v10] 编码修复：Excel COM SaveAs(6)=xlCSV 产生的是系统区域编码(中文GBK)
    // 优先检测BOM，无BOM则尝试UTF-8，失败则回退GBK
    QString content;
    if (raw.startsWith("\xEF\xBB\xBF")) {
        // UTF-8 BOM
        content = QString::fromUtf8(raw.mid(3));
    } else if (raw.startsWith("\xFF\xFE")) {
        // UTF-16 LE BOM
        content = QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.data() + 2), (raw.size() - 2) / 2);
    } else if (raw.startsWith("\xFE\xFF")) {
        // UTF-16 BE BOM
        QByteArray swapped;
        swapped.resize(raw.size() - 2);
        for (int i = 2; i < raw.size(); i += 2) {
            if (i + 1 < raw.size()) { swapped[i - 2] = raw[i + 1]; swapped[i - 1] = raw[i]; }
        }
        content = QString::fromUtf16(reinterpret_cast<const char16_t*>(swapped.constData()), swapped.size() / 2);
    } else {
        // 无BOM：先尝试UTF-8，失败则按本地编码(GBK)解码
        content = QString::fromUtf8(raw);
        // 如果UTF-8解码后包含替换字符(U+FFFD)，说明不是有效UTF-8，回退GBK
        if (content.contains(QChar::ReplacementCharacter)) {
            content = QString::fromLocal8Bit(raw);
        }
    }

    QStringList allLines = content.split('\n', Qt::SkipEmptyParts);
    if (allLines.isEmpty()) {
        result.errors.append(QStringLiteral("文件中没有数据"));
        if (isXlsx && csvFilePath != filePath) QFile::remove(csvFilePath);
        return result;
    }

    QStringList headers = parseCsvLine(allLines.first());

    if (!validateHeaders(headers)) {
        QString expected = TEMPLATE_HEADERS.join("、");
        QString actual = headers.join("、");
        result.errors.append(QStringLiteral("模板表头不匹配！\n  期望：%1\n  实际：%2\n\n请使用下载的模板格式，确保表头列名完全一致")
            .arg(expected, actual));
        if (isXlsx && csvFilePath != filePath) QFile::remove(csvFilePath);
        return result;
    }

    QMap<QString, int> colIndex;
    for (int i = 0; i < headers.size(); ++i) {
        colIndex[headers[i].trimmed()] = i;
    }

    for (int lineIdx = 1; lineIdx < allLines.size(); ++lineIdx) {
        const QString& line = allLines[lineIdx];
        if (line.trimmed().isEmpty()) continue;

        QStringList fields = parseCsvLine(line);
        if (fields.size() < TEMPLATE_HEADERS.size()) continue;

        ImportRow row;
        row.lineNumber = lineIdx + 1;
        row.workNo     = fields.value(colIndex.value(QStringLiteral("工号"), 0)).trimmed();
        row.realName   = fields.value(colIndex.value(QStringLiteral("姓名"), 1)).trimmed();
        row.department = fields.value(colIndex.value(QStringLiteral("部门"), 2)).trimmed();
        row.role       = fields.value(colIndex.value(QStringLiteral("角色"), 3)).trimmed();
        row.phone      = fields.value(colIndex.value(QStringLiteral("联系电话"), 4)).trimmed();
        row.status     = fields.value(colIndex.value(QStringLiteral("状态"), 5), "启用").trimmed();

        QString err = validateRow(row);
        if (!err.isEmpty()) {
            result.errors.append(err);
            result.failCount++;
            continue;
        }

        m_parsedRows.append(row);
    }

    result.successCount = m_parsedRows.size();

    if (isXlsx && csvFilePath != filePath) {
        QFile::remove(csvFilePath);
    }

    return result;
}
