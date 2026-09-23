/**
 * @file BatchImportDialog.h
 * @brief 批量导入用户对话框 — 继承BaseDialog圆角风格
 * @author 袁燕
 * @date 2026-06-26
 * @说明 美观的圆角模态对话框，支持：
 *   1) 下载CSV模板（工号/姓名/部门/角色/联系电话/状态）
 *   2) 上传已编辑的Excel/CSV文件
 *   3) 模板格式校验 + 逐行数据校验
 *   4) 批量入库并刷新用户列表
 * @依赖 无第三方库，纯Qt+CSV方案
 *   Windows: xlsx通过PowerShell+Excel COM转CSV
 *   Linux/Kylin: 直接CSV导入
 */
#pragma once

#include "components/BaseDialog.h"
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

struct ImportRow {
    QString workNo;      // 工号
    QString realName;    // 姓名
    QString department;  // 部门
    QString role;        // 角色 (管理员/普通用户)
    QString phone;       // 联系电话
    QString status;      // 状态 (启用/禁用)
    int     lineNumber;  // Excel行号(用于报错定位)
};

struct ImportResult {
    int     successCount = 0;
    int     failCount = 0;
    QStringList errors;  // 逐行错误信息
};

class BatchImportDialog : public BaseDialog {
    Q_OBJECT
public:
    explicit BatchImportDialog(QWidget* parent = nullptr);

    /** 获取解析后的导入数据列表 */
    QList<ImportRow> parsedRows() const { return m_parsedRows; }

    /** 获取选中的文件路径 */
    QString filePath() const { return m_filePath; }

protected:
    void setupContent() override;

private slots:
    void onDownloadTemplate();
    void onSelectFile();
    void onConfirm();
    void onCancel();

private:
    /** 生成CSV模板文件（UTF-8 BOM，Excel/WPS兼容） */
    bool generateTemplate(const QString& filePath);

    // [2026-06-27] 查询数据库已存在的CF+数字格式工号，返回下一个可用工号
    QString generateNextWorkNo() const;

    /** 解析文件并校验格式 */
    ImportResult parseExcel(const QString& filePath);

    /** 校验单行数据合法性 */
    QString validateRow(const ImportRow& row);

    /** 校验模板表头是否匹配 */
    bool validateHeaders(const QStringList& headers);

    // UI元素
    QLabel*     m_fileStatusLabel;
    QLineEdit*  m_filePathEdit;
    QPushButton* m_confirmBtn;
    QPushButton* m_cancelBtn;

    // 数据
    QString          m_filePath;
    QList<ImportRow> m_parsedRows;

    // 模板列名（不含人脸录入）
    static const QStringList TEMPLATE_HEADERS;
    // 角色映射
    static const QMap<QString, QString> ROLE_MAP;
    // 状态映射
    static const QMap<QString, QString> STATUS_MAP;
};
