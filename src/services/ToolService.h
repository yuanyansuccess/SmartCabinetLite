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
#include <QSet>
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
        Misplaced,      ///< 协议告警类型 misplaced（工具放错位置）：本单工具出现在非目标货位
        StillThere      ///< 出库项目标货位上仍有工具（没有取走）
    };

    /// 边缘机上报的一个货位实况（页面中立结构：MainWindow 从通信层转换后注入，
    /// 页面不直接包含通信层头文件）
    struct RecogSpot {
        QString position;    ///< 货位（A-01-02）
        QString toolCode;    ///< 识别出的工具编号（空串=认不出）
        double  confidence = 0.0;  ///< 置信度 0~1
        bool    present = true;    ///< 摄像头是否扫到该货位（false=未覆盖，无法判定）
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
        double  confidence = 0.0;                           ///< 该货位识别置信度（0=未识别到，写入轮次操作日志）
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

    /// 库内一条在库货位（全柜核对的基准数据：页面从 DAO 取后传入，服务层不读库）
    struct RecogStockItem {
        int     mappingId = 0;   ///< 货位映射ID
        int     toolId = 0;      ///< 工具ID
        QString toolCode;        ///< 工具编号
        QString toolName;        ///< 工具名称
        QString position;        ///< 货位（A-01-02）
    };

    /// 全柜核对异常类型（驱动结果页红/黄字与汇总文案）
    enum class RecogStockDiffType {
        Missing,     ///< 库内在库、柜内已空：工具不见了
        Mismatch,    ///< 库内是A工具、柜内识别出的是B：工具被换或被放到别处
        NotCovered,  ///< 库内在库、识别清单未回报该货位：摄像头没覆盖，无法判定
        Extra        ///< 库内该货位无在库记录、柜内却识别出工具：来源不明
    };

    /// 全柜核对单条异常（货位 + 库内工具 + 人读说明）
    struct RecogStockDiff {
        RecogStockDiffType type = RecogStockDiffType::Missing;  ///< 异常类型
        QString position;      ///< 货位（A-01-02）
        QString toolCode;      ///< 库内工具编号（Extra 类为识别出的编号）
        QString toolName;      ///< 工具名称（空串时界面显示编号）
        QString detail;        ///< 人读说明，可直接上结果页
    };

    /// 全柜核对总输出：异常明细 + 两侧规模（供结果页汇总"核对 N 个货位，发现 M 处异常"）
    struct RecogStockOutcome {
        QList<RecogStockDiff> diffs;   ///< 异常明细（为空=全柜一致）
        int stockCount = 0;            ///< 库内在库货位数（比对基准）
        int scannedCount = 0;          ///< 识别清单条数（实况侧）
        bool checked = false;          ///< 本次是否真的做了全柜核对（免开箱等无实况场景为 false，界面不显示核对结论）
    };

    /**
     * @brief 库内全量在库货位 与 边缘机实况清单 的全柜比对（静态纯函数：不读库不写库不碰界面）
     * @param stocks         库内在库货位列表（由页面经 ToolDAO 取后传入）
     * @param spots          边缘机回报的全柜实况清单
     * @param ignorePositions 本单据涉及的目标货位集合（入库/出库项目标货位）
     *                       ——这些货位正在被本单处理，写库前后状态会变，不参与全柜核对
     * @return 异常明细与两侧规模；diffs 为空表示全柜一致
     * @note  单据比对只回答"本单这几十项对不对"，全柜比对回答"柜子里其他货位有没有被动过"：
     *        库里在库却空了=Missing；库里是A却识别出B=Mismatch；清单没回报=NotCovered；
     *        库里无在库记录却识别出工具=Extra（未登记入库的工具出现在柜内）
     */
    static RecogStockOutcome compareStockWithRecognition(
        const QList<RecogStockItem>& stocks, const QList<RecogSpot>& spots,
        const QSet<QString>& ignorePositions);

    /**
     * @brief 借用归还场景下，借出工具回归到非登记货位时判为错放（misplaced）
     * @param diffs             全柜核对异常列表（仅处理 Extra 类：借出工具柜内复现）
     * @param regPositionByCode toolCode → 登记货位（含借出在外的工具，由页面经 ToolDAO 取）
     * @return 错放告警文案列表（每条可直接上结果页，与在库工具错放同一口径）
     * @note  上位机不读取边缘机回包里的 misplaced 标签（RecogSpot 无 alertType 字段），
     *        错放完全由"实际货位 ≠ 登记货位"判定；本函数即归还分支的该判定，抽为纯函数便于单测。
     */
    static QStringList detectReturnMisplaced(
        const QList<RecogStockDiff>& diffs,
        const QHash<QString, QString>& regPositionByCode);

    /**
     * @brief 判断全柜核对结果里有没有"硬差异"（账实不符，需要人工确认）
     * @param outcome 全柜核对结果
     * @return true=有硬差异：库内在库工具柜内已空(Missing)、货位上工具被换(Mismatch)、
     *         柜内出现未登记工具(Extra)；false=只有未覆盖(NotCovered)或没有差异
     * @note  未覆盖不算硬差异：那是摄像头没扫到该货位，属识别覆盖问题，不是账实不符；
     *        硬差异一旦出现，本次出入库就不能判"全部成功"，要让操作人确认后再写库
     */
    static bool hasHardStockDiff(const RecogStockOutcome& outcome);

    /**
     * @brief 取出全柜核对结果里的硬差异明细
     * @param outcome 全柜核对结果
     * @return 硬差异明细列表（排除未覆盖）
     */
    static QList<RecogStockDiff> hardStockDiffs(const RecogStockOutcome& outcome);

    /**
     * @brief 判断本轮识别是否"全过"（页面写库与结果页文案都用这一份判定）
     * @param matchedCount 本轮比对通过的单据项数
     * @param itemCount    本单单据总项数
     * @param outcome      全柜核对结果
     * @return true=本轮全过（可整单写库）；false=本轮未全过（走重新开柜整理/按实况写库双出口）
     * @note  两个条件都要满足才算全过：① 单据项全部通过；② 全柜没有账实不符的硬差异。
     *        只看单据项会把"实际多拿/少拿工具"放过——现场就出过一次：单据只出库 1 件，
     *        柜内实际少了 2 件，结果却显示"全部指令执行成功"
     */
    static bool isRoundFullyPassed(int matchedCount, int itemCount,
                                   const RecogStockOutcome& outcome);
};
