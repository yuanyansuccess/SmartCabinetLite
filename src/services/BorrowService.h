/**
 * @file BorrowService.h
 * @brief 借用业务服务 - 表单校验、错误处理
 * @author 袁燕
 * [V1.00.8] 添加表单校验方法
 */
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>

class BorrowService : public QObject {
    Q_OBJECT
public:
    explicit BorrowService(QObject* parent = nullptr);
    struct Result { bool success; QString message; int recordId; };
    
    /**
     * @brief 借用工具
     * @param userId 用户ID
     * @param toolId 工具ID
     * @param mappingId 位置映射ID（V2.11 按位置维度借用，更新映射表status=borrowed）
     * @param quantity 数量
     * @param reason 借用原因
     * @param expectedReturnTime 预计归还时间
     * @param flowNo 流水号
     * @return 借用结果
     */
    Result borrowTool(int userId, int toolId, int mappingId, int quantity, const QString& reason,
                      const QString& expectedReturnTime, const QString& flowNo,
                      int machineGroupId = 0);
    
    /**
     * @brief 获取用户借用记录
     * @param userId 用户ID
     * @param page 页码
     * @param pageSize 每页数量
     * @return 借用记录列表
     */
    QJsonObject getUserRecords(int userId, int page = 1, int pageSize = 20);
    QJsonObject getAllRecords(int page = 1, int pageSize = 20);  // [V2.04] 所有借用记录
    
    /**
     * @brief 获取任务类型列表
     * @return 任务类型列表
     */
    QJsonArray getTaskTypes();
    
    /**
     * @brief 根据任务类型获取推荐工具
     * @param typeIds 任务类型ID列表
     * @return 推荐工具列表
     */
    QJsonArray getRecommendedTools(const QList<int>& typeIds, int machineGroupId = 0);
    
    /**
     * @brief 生成流水号
     * @param reason 借用原因
     * @return 流水号（格式：JH-MMdd-XXhhmm）
     */
    QString generateFlowNo(const QString& reason);
    
    /**
     * @brief 获取所有在库工具
     * @param page 页码
     * @param pageSize 每页数量
     * @return 工具列表
     */
    QJsonObject getAllInStockTools(int page = 1, int pageSize = 100, int machineGroupId = 0);
    
    /**
     * @brief 搜索在库工具（支持关键词）
     * @param keyword 搜索关键词
     * @param page 页码
     * @param pageSize 每页数量
     * @return 工具列表
     * [V1.00.9.1 架构修复] 新增方法，替代页面直接调用db/ToolDAO —— 作者：袁燕
     */
    QJsonObject searchTools(const QString& keyword, int page = 1, int pageSize = 100, int machineGroupId = 0);
};
