/**
 * @file ToolService.h
 * @brief 工具服务类 - 提供工具入库/出库业务逻辑
 * @author 袁燕
 * 完善接口定义，统一字段命名
 */
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QList>
#include "comm/CommTypes.h"

class ToolService {
public:
    ToolService();

    /**
     * @brief 工具入库
     * @param data 入库数据（toolCode/toolName/spec/category/quantity/cabinetId/machineGroupId/
     *              position/visionTag/recognitionMethod/documentPath）
     *              recognitionMethod: vision（全系统统一视觉识别）
     *              documentPath: 工具文档本地路径(doc/docx/pdf)
     * @return 是否成功
     */
    bool checkinTool(const QJsonObject& data);

    /// 单据一行与柜内实况的比对结论（识别结果类型，驱动结果页红绿字）
    enum class RecogItemResult {
        Matched,        ///< 通过：入库项货位有正确工具；出库项货位已空
        Empty,          ///< 入库项目标货位是空的（工具没有放入）
        WrongTool,      ///< 入库项目标货位上是别的工具
        LowConfidence,  ///< 识别出本工具但置信度不足门槛
        UnknownItem,    ///< 目标货位上有无法识别的物品
        StillThere      ///< 出库项目标货位上仍有工具（没有取走）
    };

    /// 边缘机上报的一个货位实况（页面中立结构：MainWindow 从通信层转换后注入，
    /// 页面不直接包含通信层头文件）
    struct RecogSpot {
        QString position;    ///< 货位（A-01-02）
        QString toolCode;    ///< 识别出的工具编号（空串=认不出）
        double  confidence = 0.0;  ///< 置信度 0~1
    };

    /// 待比对的一行指令（由页面从单据收集后传入，字段与页面单据行对应）
    struct RecogOrderItem {
        QString type = QStringLiteral("in");  ///< "in"=入库 / "out"=出库
        int     toolId = 0;                   ///< 工具ID
        int     mappingId = 0;                ///< 出库行的货位映射ID（入库行为0）
        QString toolCode;                     ///< 工具编号
        QString toolName;                     ///< 工具名称
        QString position;                     ///< 目标货位（A-01-02 格式）
    };

    /// 单行比对结论（含附加说明，可直接上结果页）
    struct RecogCompareItem {
        RecogItemResult result = RecogItemResult::Matched;  ///< 比对结论
        int     toolId = 0;                                 ///< 工具ID
        QString toolCode;                                   ///< 工具编号
        QString toolName;                                   ///< 工具名称
        QString position;                                   ///< 目标货位
        QString detail;                                     ///< 附加说明（货位上为XX/置信度xx%）
    };

    /// 错放明细（结果页提示行用：本单工具出现在了非目标货位）
    struct RecogMisplaced {
        QString toolCode;         ///< 工具编号
        QString toolName;         ///< 工具名称
        QString actualPosition;   ///< 实际出现的货位
        QString expectedPosition; ///< 本单目标货位（入库项）
    };

    /// 比对总输出：逐行结论 + 错放明细
    struct RecogCompareOutcome {
        QList<RecogCompareItem>  items;      ///< 逐行结论（与入参 items 同序）
        QList<RecogMisplaced>    misplaced;  ///< 错放明细（为空表示无错放）
    };

    /**
     * @brief 单据与边缘机实况比对（纯函数：不读库不写库不碰界面，供单测直接覆盖）
     * @param items   待比对的单据指令列表（入库项已分配好目标货位）
     * @param result  边缘机返回的全柜实况清单（recognize_ack 解析结果）
     * @return 逐行结论与错放明细；结论顺序与 items 一致
     * @note  入库项：目标货位须识别出"正是本工具"且置信度达门槛；出库项：目标货位须已空
     */
    static RecogCompareOutcome compareOrderWithRecognition(
        const QList<RecogOrderItem>& items, const RecognizeResult& result);

    /**
     * @brief 同上，但入参用页面中立实况结构（供页面调用：不碰通信层类型）
     * @param items   待比对的单据指令列表
     * @param spots   全柜实况清单（页面中立结构）
     * @return 逐行结论与错放明细
     */
    static RecogCompareOutcome compareOrderWithRecognition(
        const QList<RecogOrderItem>& items, const QList<RecogSpot>& spots);
};
