// 手册第1~6章：目的、测试机理、覆盖矩阵、环境、安装、界面说明（含截图）
const B = require("./base");
const { Paragraph, AlignmentType, PageBreak, t, p, h1, h2, h3, code, table, img } = B;

const SHOT = "D:/CFDZ/smartCabinet/trunk/SmartCabinetLite/tools/EdgeSimulator/docs/shots";
const children = [];

// ── 封面 ──
children.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 2400, after: 300 },
  children: [t("边缘计算机模拟器", { bold: true, size: 48 })] }));
children.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 200 },
  children: [t("使用手册（接口测试版）", { bold: true, size: 32 })] }));
children.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 900 },
  children: [t("智能工具柜上位机管理软件 ↔ 视觉边缘计算机 通信接口测试", { size: 24 })] }));
children.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 120 },
  children: [t("依据文件：《智能工具柜配置项通信协议》（M0评审冻结版）", { size: 22 })] }));
children.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 120 },
  children: [t("工具版本：EdgeSimulator v2.1（协议全接口覆盖 + 自定义扩展）", { size: 22 })] }));
children.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 120 },
  children: [t("编制：袁燕    日期：2026年10月13日", { size: 22 })] }));
children.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 120 },
  children: [t("适用对象：测试工程师（含初次接触本系统的测试人员）", { size: 22 })] }));
children.push(new Paragraph({ children: [new PageBreak()] }));

// ── 1 目的与范围 ──
children.push(h1("1  手册目的与适用范围"));
children.push(p("本手册指导测试工程师使用《边缘计算机模拟器》（EdgeSimulator）对智能工具柜上位机管理软件的“与边缘计算机通信接口”进行协议符合性测试。该工具模拟视觉边缘计算机（真机为 RK3588 设备）的全部协议行为，无需真机即可完成通信接口的用例执行与问题定位。"));
children.push(p("测试对象：上位机管理软件的通信层（监听 9000 端口、协议第2章分帧、第3章消息处理、第5章异常处理、第6章链路判活）。"));
children.push(p("不在本工具测试范围：上位机与 IO 下位机的 Modbus TCP 接口（协议第7章，需另配 IO 模块或 Modbus 模拟器）；人脸识别（上位机本机完成，不走本协议）。"));
children.push(p("阅读建议：初次接触本系统的测试人员请先读第2章“测试机理阐述”（10 分钟），理解被测对象、工具角色与判定方法后再动手执行用例。"));

// ── 2 测试机理阐述（新人必读） ──
children.push(h1("2  测试机理阐述（新人必读）"));
children.push(h2("2.1  被测系统在测什么"));
children.push(p("智能工具柜由三部分组成：上位机管理软件（管人和账，跑在麒麟系统）、视觉边缘计算机（管“眼睛”，拍照识别柜内工具）、IO 下位机（管“手”，控制电磁锁和门磁）。本工具测的是第一和第二之间的“对话”是否严格遵守双方约定好的通信协议。"));
children.push(p("打个比方：上位机是“柜台”，边缘机是“库房”。协议就是双方的对话规范——怎么打招呼（周期帧）、怎么下单（识别请求）、怎么回话（识别清单）、对方没回话怎么办（超时重发）。本工具扮演“假库房”，用各种正确和错误的对话方式考验“真柜台”，看它是否按规范应对。"));
children.push(h2("2.2  协议在 TCP 之上如何工作"));
children.push(table([2400, 6906],
  ["概念", "说明"],
  [
    ["TCP 长连接", "上位机是服务端（监听 9000 端口），边缘机是客户端（连上来）。连上后保持不断，双方在这条连接上对话"],
    ["一行一帧", "TCP 只负责传字节，不区分“句”。协议约定：每说一句话 = 一行紧凑 JSON 加一个换行符。接收方看到换行就知道一句话结束（这叫“分帧”）"],
    ["紧凑 JSON", "JSON 不带空格换行。这保证一句话内部绝不会出现换行符，分帧才可靠。字符串里的换行会被转义，不会误判句尾"],
    ["请求-应答配对", "上位机每发一个识别请求都带唯一编号（request_id，如 r-20261013-001）。边缘机的回答必须原样带回这个编号，上位机靠它把答案对回问题"],
    ["心跳（周期帧）", "边缘机每 3 秒主动说一句“我还活着”（periodic）。上位机连续 15 秒没听到就判它离线，禁止开锁——防止“门开了但没人记录”"],
    ["超时重发", "上位机发了请求 3 秒没回答就重发，最多 2 次（共发 3 次）。还不行就记一次“通信异常”，但请求不丢弃——等链路恢复后按原编号补发一次"],
    ["断线重连", "边缘机断线后按 1/2/4/8/16/30 秒退避自动重连（本工具已模拟该行为）。恢复后上位机自动补发离线期间的请求"],
  ]));
children.push(h2("2.3  测试方法：黑盒接口测试 + 双日志对拍"));
children.push(p("方法一：黑盒接口测试。测试人员不关心上位机内部代码，只把它当成“按协议应答的黑盒子”——用本工具发各种消息（正常的、异常的），观察它的应答是否符合协议第3章、3.6、第5章、第6章的规定。"));
children.push(p("方法二：双日志对拍。一次测试同时看两份日志互相印证：①本工具日志（界面右侧 + exe 同级 EdgeSimulator_log.txt）记录“我发了什么、收到什么”；②上位机日志（QtSmartCabinet.exe 同级 logs\\app_日期.log）记录“它收到什么、如何处理、判了什么状态”。两份一对，问题定位到是哪一侧。"));
children.push(h2("2.4  异常测试为什么重要"));
children.push(p("协议第5章的容错设计（超时重发、失败保留、恢复补发、离线禁止开锁）是验收硬指标，也是 72 小时稳定性测试的核心内容。这些逻辑在正常流程里永远看不到，必须靠本工具主动制造异常（不回应、停心跳、乱编号、坏帧）才能触发和验证。测试人员每执行一个异常用例，都是在验证协议规定的一条容错规则。"));

// ── 3 覆盖矩阵 ──
children.push(h1("3  协议接口覆盖矩阵（测试依据）"));
children.push(p("本工具对协议 A 接口的全部消息与规则逐条覆盖。测试人员可对照本表核验无遗漏："));
children.push(table([900, 2050, 3550, 2960],
  ["协议条款", "接口/规则", "协议要求", "本工具覆盖方式"],
  [
    ["3.1", "open_req 开箱请求", "上位机→边缘机，仅 type+request_id", "边缘机执行开锁，回 open_ack"],
    ["3.1", "open_ack 开箱回应", "回带 request_id + result(0/1) + message", "上位机据此判断是否真实开柜，离线/超时按失败处理"],
    ["3.2", "recognize 请求", "上位机→边缘机，仅 type+request_id", "工具接收并记录，作为回应对象"],
    ["3.3", "recognize_ack 回应", "回带 request_id + items 全字段必填（position/tool_id/class_id/confidence/alert_type/alert_level，另加 present 表示该货位是否被扫到）", "7 个场景按钮：正常/有借有还/无变化/错放/未知物/置信度低/缺失"],
    ["3.3", "字段规整化约定", "items 每一项 7 个字段全部必填，禁止时有时无；缺货位的项判非法并跳过", "内置场景与自定义清单出口径统一补齐，缺字段即视为协议违规"],
    ["3.3", "present 覆盖标记", "true=摄像头扫到该货位；false=未覆盖。上位机据此区分“扫到了但是空的”与“根本没回报”", "生成清单时可选“附带空货位”，同时模拟两种边缘机回报口径"],
    ["3.3", "alert_type 全枚举", "misplaced/unknown/missing/low_confidence", "4 个告警场景按钮逐一覆盖（含 missing 边界值）"],
    ["3.4", "periodic 周期帧", "3秒一条，uptime_sec + camera_status[{camera_id,status}]", "周期帧自动发送；相机5态可选；空自检容错场景"],
    ["3.4", "相机自检5态", "ok/offline/low_light/dirty/comm_error", "两个相机下拉各自可选（具体以硬件选型为准）"],
    ["3.5", "version_query/ack", "查询与回应，software_version", "收到查询自动回应；版本号可配；可手动回"],
    ["3.6-①", "超时重发", "3秒未收到重发，最多2次", "【不回应】+【延迟回应】场景触发"],
    ["3.6-②", "坏帧/未知type/乱序id", "丢弃并计数，不断链", "三个注入按钮逐一覆盖"],
    ["3.6-③", "request_id 去重", "同一 id 多次回应只认第一次", "【重复回应×2】按钮"],
    ["3.6-④", "周期帧15秒判离线", "连丢5条判离线", "【停发周期帧】按钮"],
    ["第2章", "换行分帧", "一行紧凑JSON+换行；粘包可靠切分", "【两帧拼一行】按钮；自定义帧"],
    ["第2章", "单帧上限1MB", "超长断开保护", "【注入超长帧>1MB】按钮"],
    ["第5章", "失败保留与恢复补发", "记异常保留请求；链路恢复按原 id 补发一次", "【停发周期帧】+【不回应】组合场景"],
    ["第5章⑤", "离线禁止发识别", "离线时拒绝下发请求", "停发周期帧 15 秒后上位机应拒绝（观察其日志）"],
    ["第6章", "链路判活独立判定", "周期帧管链路，请求超时管业务，两者独立", "两类场景可分别单独执行"],
    ["工程项", "断线自动重连", "模拟真实边缘机退避重连（协议外，工程需要）", "1/2/4/8/16/30 秒退避，可开关"],
    ["扩展项", "自定义任意帧", "测试人员自建异常场景", "自定义页签：JSON/原文两种模式、多行逐行发、循环发送"],
  ]));

// ── 4 环境准备 ──
children.push(h1("4  测试环境准备"));
children.push(h2("4.1  网络拓扑"));
children.push(code("方式一（本机测试，最简单）：\n  同一台电脑同时运行 上位机程序(QtSmartCabinet.exe) 和 本模拟器，IP 填 127.0.0.1\n\n方式二（跨电脑测试）：\n  电脑A：运行上位机程序（监听9000）\n  电脑B：运行本模拟器，IP 填 电脑A 的局域网 IP\n  两台电脑须在同一网段（接同一交换机/路由器）"));
children.push(h2("4.2  防火墙放行（仅跨电脑测试需要，电脑A 执行一次）"));
children.push(code("管理员 PowerShell 执行：\nnetsh advfirewall firewall add rule name=\"SmartCabinet9000\" dir=in action=allow protocol=TCP localport=9000"));
children.push(h2("4.3  测试前检查"));
children.push(p("① 电脑A 的上位机程序启动成功（能看到登录界面）；② 执行 taskkill /f /im QtSmartCabinet.exe 确保只有一个实例（重复实例会占用 9000 端口导致监听失败）；③ 本模拟器填对 IP 和端口（默认 9000）。"));

// ── 5 安装与启动 ──
children.push(h1("5  软件安装与启动"));
children.push(h2("5.1  Windows"));
children.push(p("将 EdgeSimulator_边缘机模拟器.zip 解压到任意目录（含 Qt 运行库，无需安装 Qt），双击 EdgeSimulator.exe。"));
children.push(p("命令行一键连接（免点按钮，用于快速冒烟）：EdgeSimulator.exe --auto-connect"));
children.push(p("命令行指定页签（用于演示/截图）：EdgeSimulator.exe --tab 1（0=周期帧 1=识别回应 2=版本/异常 3=自定义帧）"));
children.push(h2("5.2  麒麟系统（飞腾ARM）"));
children.push(p("源码已随包提供（EdgeSimulator 目录），在麒麟上用 Qt6 编译："));
children.push(code("cd EdgeSimulator\nmkdir build && cd build\ncmake .. && make -j4\n./EdgeSimulator"));
children.push(p("Qt6 查找路径与主工程一致（/opt/Qt/6.5.3/gcc_arm64 或 gcc_64），发行版 Qt6 亦可自动识别。"));

// ── 6 界面说明（含截图） ──
children.push(h1("6  界面与按钮功能说明"));
children.push(p("界面左侧为控制区（四个页签），右侧为实时日志区（蓝=发送、绿=接收、红=坏帧、灰=事件）。日志同步写入 exe 同级 EdgeSimulator_log.txt（路径见窗口标题栏）。以下截图按页签逐一对应："));
children.push(h2("6.1  连接区（各页签通用，见图1）"));
children.push(img(SHOT + "/tab0_periodic.png", 600, 405, "主界面-连接区与周期帧页签"));
children.push(p("图1  连接区与“周期帧”页签", { alignment: AlignmentType.CENTER }));
children.push(table([2600, 3400, 3360],
  ["控件", "功能", "说明"],
  [
    ["上位机IP / 端口", "连接目标", "本机测试填 127.0.0.1；跨电脑填上位机实际IP；端口 9000"],
    ["【连接】/【断开】", "建立/断开 TCP 连接", "状态灯：灰=未连接 绿=已连接 橙=已断开"],
    ["断开后自动重连", "模拟真实边缘机退避重连", "勾选后断线按 1/2/4/8/16/30 秒自动重连；取消勾选=手动模式"],
  ]));
children.push(h2("6.2  页签一：周期帧（协议3.4，见图1）"));
children.push(table([2600, 3400, 3360],
  ["控件", "功能", "对应测试项"],
  [
    ["CAM1/CAM2 自检状态", "设置两相机自检结论", "ok/offline/low_light/dirty/comm_error 五态；TC-03"],
    ["(空-不报自检)", "该相机不出现在清单", "两相机都选空=空数组，测上位机容错；TC-04B/04C"],
    ["uptime_sec", "边缘机运行秒数", "TC-04"],
    ["【立即发一帧周期帧】", "手动发一帧", "TC-02"],
    ["自动发送 + 间隔", "周期帧自动发送（默认3秒）", "TC-01；间隔可调加速测试"],
  ]));
children.push(h2("6.3  页签二：识别回应（协议3.3，见图2）"));
children.push(p("说明：以下按钮对“最近一次收到的 recognize 请求”进行回应；若上位机未发过请求，点击会提示“没有待回应的请求”。"));
children.push(img(SHOT + "/tab1_recognize.png", 600, 405, "识别回应页签"));
children.push(p("图2  “识别回应”页签：每个按钮对应一个协议场景", { alignment: AlignmentType.CENTER }));
children.push(table([3600, 3100, 2660],
  ["按钮（按钮文字即功能）", "发出的内容", "对应测试项"],
  [
    ["正常清单：2条样例，测消息配对", "A-01-05 扳手 + A-02-01 套筒", "TC-06"],
    ["有借有还：A-01-05空+A-01-06套筒", "仅 A-01-06 一项（空位靠不出现表示）", "TC-07"],
    ["无变化：空清单，测归档分支", "items 为空数组", "TC-08"],
    ["错放告警：misplaced L2，测告警弹窗", "A-02-01 出现套筒 + misplaced/2", "TC-09"],
    ["未知物告警：unknown L3，测严重告警", "tool_id 空 + class_id unknown + unknown/3", "TC-10"],
    ["置信度不足：low_confidence L1，测不落账", "confidence 0.55 + low_confidence/1", "TC-11"],
    ["缺失告警：missing L2，测字段枚举边界", "missing/2（协议枚举边界值）", "TC-16B"],
    ["延迟回应：默认4000ms>3秒，测超时重发", "延迟后回正常清单", "TC-12"],
    ["重复回应×2：测request_id去重", "同一 request_id 回两次", "TC-13"],
    ["两帧拼一行：测粘包分帧", "periodic 与 ack 拼一行发送", "TC-14"],
    ["不回应：关闭自动回应", "保持沉默", "TC-15"],
    ["停发周期帧：测15秒判离线+恢复补发", "N秒后停发、M秒后恢复", "TC-05"],
  ]));
children.push(h2("6.4  页签三：版本/异常（协议3.5/3.6，见图3）"));
children.push(img(SHOT + "/tab2_fault.png", 600, 405, "版本与异常注入页签"));
children.push(p("图3  “版本/异常”页签", { alignment: AlignmentType.CENTER }));
children.push(table([3600, 3100, 2660],
  ["按钮（按钮文字即功能）", "发出的内容", "对应测试项"],
  [
    ["立即回 version_ack：版本号取输入框", "software_version 取左侧输入值", "TC-18；收到 version_query 时自动回（TC-17）"],
    ["注入非法JSON：测坏帧丢弃不断链", "非 JSON 文本一行", "TC-19"],
    ["注入未知type：测未知类型丢弃", "type=unknown_type_test", "TC-20"],
    ["注入超长帧>1MB：测断开保护", "超过1MB的 JSON 行", "TC-21"],
    ["回应乱序request_id：测丢弃计数", "request_id=r-INVALID-999", "TC-16"],
    ["断开连接：模拟断链", "断开 TCP", "TC-22；勾选自动重连会重连（TC-22B）"],
  ]));
children.push(h2("6.5  页签四：自定义帧（扩展入口，见图4）"));
children.push(p("这是测试人员自建异常场景的扩展接口：协议任何消息、任何异常都可在此构造，不受预设按钮限制。"));
children.push(img(SHOT + "/tab3_custom.png", 600, 405, "自定义帧页签"));
children.push(p("图4  “自定义帧”页签：自建异常入口", { alignment: AlignmentType.CENTER }));
children.push(table([3600, 3700, 2060],
  ["功能", "用法", "适用场景"],
  [
    ["JSON 模式（默认）", "编辑框每行一帧，点【发送（逐行）】逐条校验后发出", "构造任意协议消息（含协议未预设的字段组合）"],
    ["原文发送（勾选）", "不校验 JSON，按文本原样发出（含换行）", "坏帧文本、多帧粘包等任意字节级异常"],
    ["循环发送", "设间隔后点【启动循环发送】，按间隔周期性重发", "持续异常注入、压力测试、链路稳定性观察"],
    ["多行组合", "编辑框写多行，一次发送逐行发出（间隔100ms）", "组合场景（如先错放后恢复正常）"],
  ]));
children.push(p("扩展示例：① 测“协议外字段”——在 ack 里加 \"extra_field\":1，看上位机是否忽略；② 测“缺字段”——发 {\"type\":\"recognize_ack\"}（缺 request_id），看上位机是否丢弃计数；③ 测“超长 request_id”——原文模式发 10KB 的 id。"));

children.push(h2("6.6  页签五：工具清单（自定义）——按库内货位造真实场景"));
children.push(p("这一页解决“凭空手填清单不像现场”的问题：填好数据库口令后点【按在库全量生成清单】，模拟器直接读 tool_position_mapping 把当前全部在库货位生成清单（每行 货位,工具编号,置信度），测试人员在这份真实基线上改几行就能造场景——删掉一行=工具被取走，把编号改成另一个=工具被换，把编号清空=货位上有认不出的东西。"));
children.push(img(SHOT + "/tab4_list.png", 600, 405, "工具清单页签"));
children.push(p("图5  “工具清单（自定义）”页签：在库全量一键生成", { alignment: AlignmentType.CENTER }));
children.push(table([2600, 3400, 3360],
  ["控件", "功能", "对应测试项"],
  [
    ["【按在库全量生成清单】", "读库内全部货位生成清单，并自动切到自定义清单场景", "TC-23；生成后场景自动生效，不用再手动点启用"],
    ["附带空货位（复选框）", "勾选后空货位也列一行（工具编号留空）", "TC-24；模拟“回报全部货位”的边缘机，上位机才能精确判定工具缺失"],
    ["货位/工具选择框 + 插入", "从库内选一个货位插成清单一行", "在基线上改单行造场景"],
    ["清单行列表", "与文本区逐行同步，点行可在上方改", "改单行后原行被替换"],
    ["【保存到 custom_items.txt】/【重载】", "清单落盘与恢复", "覆盖前先备份；批量场景可用 Excel 备好后重载"],
  ]));
children.push(p("上位机侧配套能力（本次同步完善）：收到识别清单后，除了按单据逐项核对，还会拿库里全量在库货位与清单做一次“全柜核对”——库里在库却空了报缺失、库里是A却识别出B报错位、清单没回报该货位报未覆盖、库里无在库记录却识别出工具报来源不明。核对结论只提示不阻断，写在本单结果页（见 TC-25/26/27）。"));

module.exports = children;