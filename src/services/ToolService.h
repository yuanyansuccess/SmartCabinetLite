/**
 * @file ToolService.h
 * @brief 工具服务类 - 提供工具入库/出库业务逻辑
 * @author 袁燕
 * [V1.00.8] 完善接口定义，统一字段命名
 */
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

class ToolService {
public:
    ToolService();

    /**
     * @brief 工具入库
     * @param data 入库数据（toolCode/toolName/spec/category/quantity/cabinetId/machineGroupId/
     *              position/rfidTag/recognitionMethod/documentPath）
     *              recognitionMethod: rfid(默认) | vision
     *              documentPath: 工具文档本地路径(doc/docx/pdf)
     * @return 是否成功
     */
    bool checkinTool(const QJsonObject& data);
};
