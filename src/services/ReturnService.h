/**
 * @file ReturnService.h
 * @brief 归还业务服务 - 超期计算、损坏检测
 * @author 袁燕
 * [V1.00.8] 完善归还逻辑
 */
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QList>

class ReturnService : public QObject {
    Q_OBJECT
public:
    explicit ReturnService(QObject* parent = nullptr);
    
    struct Result { 
        bool success; 
        QString message; 
        int count; 
    };
    
    /**
     * @brief 归还工具（支持损坏检测）
     * @param recordIds 借用记录ID列表
     * @param userId 操作人ID
     * @param returnInfo 归还信息（condition/remark）
     * @return 归还结果
     */
    Result returnTools(const QList<int>& recordIds, int userId, const QJsonObject& returnInfo = QJsonObject());
    
    /**
     * @brief 获取用户借用记录
     * @param userId 用户ID
     * @param page 页码
     * @param pageSize 每页数量
     * @return 借用记录列表
     */
    QJsonObject getUserBorrowingRecords(int userId, int page = 1, int pageSize = 20);
    QJsonObject getAllBorrowingRecords(int page = 1, int pageSize = 20);  // [V2.04] 所有位置待归还
};
