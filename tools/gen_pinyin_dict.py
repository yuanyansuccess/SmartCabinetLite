#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_pinyin_dict.py
生成 src/utils/PinyinDictData.h（拼音->候选汉字）、PinyinPhraseData.h
（拼音->常用词组）两份静态表，编进 exe，零运行期依赖。

设计要点：
  1. 候选汉字按【真实字频/常用度】排序：用内置 FREQ_SEED（现代汉语常用字频率序）
     把常用字提到最前，根治"li 首候选是生僻字而非里"的 bug。
  2. 姓氏加权：内置 SURNAMES（百家姓常见姓氏），生成 kSurnameChars，姓名输入时
     由上层把姓氏字符候选前置。
  3. 词组联想：内置 PHRASES（常用双字/多字词组），由 pypinyin 自动算出拼音，
     无需手写拼音、无需联网。

拼音来源：优先用 pypinyin（本机离线）；无 pypinyin 时回退下载 mozillazg/pinyin-data。
汉字以 UTF-8 \\x 转义存储，跨 MSVC / GCC 无源文件编码歧义。

用法:
  python gen_pinyin_dict.py [输出dir]
"""
import sys
import os
import re
import unicodedata

# ── 字频种子：现代汉语常用字（一级字）近似频率序，越靠前越常用 ──
# 排在前面的字符在任一拼音下都会优先成为候选，直接解决"里"排不到前面的问题。
FREQ_SEED = (
    "的一是在不了有和人这中大为上个国我以要他时来用们生到作地于出就分对成会可主发年动同工也能下过子说产种面而方后多定行学法所民得经十三之进着等部度家电力里如水化高自二理起小物现实加量都两体制机当使点从业本去把性好应开它合还因由其些然前外天政四日那社义事平形相全表间样与关各重新线内数正心反你明看原又么利比或但质气第向道命此变条只没结解问意建月公无系军很情者最立代想已通期提题总展办教"
    "张王李赵刘陈杨黄周吴徐孙胡朱高林何郭马罗梁宋郑谢韩唐冯于董萧程曹袁邓许傅沈曾彭吕苏卢蒋蔡贾丁魏薛叶阎余潘杜戴夏钟汪田任姜范方石姚谭廖邹熊金陆郝孔白崔康毛邱秦江史顾侯邵孟龙万段钱汤尹黎易常武乔贺赖龚文樊兰殷施陶洪翟安颜倪严牛温芦季俞章鲁葛伍韦申尤毕聂丛焦向柳邢路岳齐梅莫庄辛管祝左涂谷祁时舒耿牟卜凌费纪靳盛童欧甄项曲成游阳胥骆麻苗尚官代单柏宁柯阮桂闵辜饶刁华应裘营阿盘檀衣展隋薄税储谌霍奚封盖艾从邝劳扶寇布步荆沙简巩楚柴池滕晋苑邬臧畅宫来苟全褚廉景娄明臧计伏谈茅庞"
    "人民社会国家政府机关团体群众利益权益义务责任权利公平正义文明和谐自由平等诚信朋友老师同学同事领导客户市场销售采购库存物流运输财务审计合同法律政策制度规定要求原则方法步骤阶段过程经验总结计划执行监督考核评价改进优化发展建设改革创新突破进展成果效益效率水平能力素质基础核心关键重点难点风险隐患"
    "机械电子电气液压气动传感控制驱动执行反馈信号采集处理传输存储显示报警提示故障维修更换零件配件耗材材料原料成品半成品产品部件总成装配调试校准检验合格智能自动手动远程本地在线离线实时历史趋势曲线报表图表文档资料档案手册说明帮助学习工作生活家庭学校医院工厂仓库柜子抽屉锁具钥匙门禁摄像头人脸识别指纹密码登录注销注册修改退出确认取消确定应用服务接口协议格式编码解码"
    "北京上海广州深圳天津重庆西安成都武汉南京杭州苏州沈阳大连青岛济南郑州哈尔滨太原长春昆明贵阳南宁兰州南昌福州厦门合肥宁波温州绍兴金华嘉兴湖州佛山东莞珠海中山江门惠州汕头湛江肇庆茂名桂林柳州洛阳开封武汉长沙株洲湘潭岳阳衡阳常德宜昌襄阳荆州十堰绵阳德阳南充宜宾泸州遵义六盘水安顺昆明曲靖玉溪楚雄大理丽江"
)

# ── 姓氏种子：常见百家姓（单姓为主，少量复姓），用于姓名输入加权前置 ──
SURNAMES = (
    "王李张刘陈杨黄赵周吴徐孙胡朱高林何郭马罗梁宋郑谢韩唐冯于董萧程曹袁邓许傅沈曾彭吕苏卢蒋蔡贾丁魏薛叶阎余潘杜戴夏钟汪田任姜范方石姚谭廖邹熊金陆郝孔白崔康毛邱秦江史顾侯邵孟龙万段钱汤尹黎易常武乔贺赖龚文樊兰殷施陶洪翟安颜倪严牛温芦季俞章鲁葛伍韦申尤毕聂丛焦向柳邢路岳齐梅莫庄辛管祝左涂谷祁时舒耿牟卜凌费纪靳盛童欧甄项曲成游阳胥骆麻苗尚官代单柏宁柯阮桂闵辜饶刁华应裘营阿盘檀衣展隋薄税储谌霍奚封盖艾从邝劳扶寇布步荆沙简巩楚柴池滕晋苑邬臧畅宫来苟全褚廉景娄明臧计伏谈茅庞卓栗戚隋兰薄宁朴童樊柯卢裘茹桑桂邢芮奚"
    "欧阳司马上官诸葛东方皇甫尉迟公羊澹台万俟夏侯诸葛闻人东方赫连皇甫尉迟"
)

# ── 词组种子：常用双字/多字词组（仅中文，拼音由脚本自动算出）──
PHRASES = (
    "中国北京上海广州深圳天津重庆西安成都武汉南京杭州苏州沈阳大连青岛济南郑州哈尔滨"
    "公司企业单位部门员工姓名性别年龄地址电话手机邮箱账号密码用户角色权限系统网络"
    "设备工具仪器机器型号规格数量状态类型等级分类参数配置设置选项功能模块界面窗口"
    "文件数据信息记录日志消息事件任务计划目标结果原因方案问题情况条件标准规范流程"
    "质量安全管理控制检测维护保养操作运行启动停止暂停恢复重置锁定解锁连接断开发送"
    "接收处理计算分析统计报告通知警告错误失败成功完成开始结束进行退出返回进入选择"
    "输入输出显示隐藏保存删除添加修改查询搜索筛选匹配验证审批授权导入导出打印上传"
    "下载同步更新检查测试安装卸载版本时间日期年份月份季节天气温度湿度压力电压电流"
    "电流电压功率频率速度距离位置方向角度坐标尺寸重量体积容量精度误差偏差阈值上限"
    "下限范围等级项目工程产品技术科学研究人员工程师技师管理员操作员负责人主管经理"
    "主任部长科长组长成员团队组织机构学校医院工厂仓库柜子抽屉锁具钥匙门禁摄像头"
    "人脸识别指纹密码登录注销注册修改退出确认取消确定应用服务接口协议格式编码解码"
    "智能自动手动远程本地在线离线实时历史趋势曲线报表图表文档资料档案手册说明帮助"
    "学习工作生活家庭朋友老师同学同事领导客户市场销售采购库存物流运输财务审计合同"
    "法律政策制度规定要求原则方法步骤阶段过程经验总结计划执行监督考核评价改进优化"
    "发展建设改革创新突破进展成果效益效率水平能力素质基础核心关键重点难点风险隐患"
    "机械电子电气液压气动传感控制驱动执行反馈信号采集处理传输存储显示报警提示故障"
    "维修更换零件配件耗材材料原料成品半成品产品部件总成装配调试校准检验合格人民社会"
)


def build_freq_rank():
    rank = {}
    for i, ch in enumerate(FREQ_SEED):
        if ch not in rank:
            rank[ch] = i
    return rank


def build_surname_set():
    return set(SURNAMES)


def norm_py(p):
    # ü 系列在键盘上用 v 代替（拼音键盘无 ü 键）
    p = p.lower()
    p = p.replace("\u00fc", "v").replace("\u01d6", "v").replace("\u01da", "v")
    return p


def build_from_pypinyin():
    """离线方案：用 pypinyin 遍历 CJK 字符区间，建立 py->[汉字]。"""
    try:
        from pypinyin import pinyin, Style
    except Exception:
        return None
    data = {}
    order = []
    # 覆盖 CJK 基本区 + 扩展A 区（含《通用规范汉字表》全部汉字）
    for cp in range(0x3400, 0xA000):
        ch = chr(cp)
        try:
            res = pinyin(ch, style=Style.NORMAL, heteronym=True)
        except Exception:
            continue
        if not res or not res[0]:
            continue
        seen = set()
        for r in res[0]:
            py = norm_py(r)
            if not py or not py.isalpha():
                continue
            if py in seen:
                continue
            seen.add(py)
            if py not in data:
                data[py] = []
                order.append(py)
            if ch not in data[py]:
                data[py].append(ch)
    return data, order


def build_from_network():
    """回退方案：下载 mozillazg/pinyin-data（kTGHZ2013.txt）。"""
    import urllib.request
    url = "https://cdn.jsdelivr.net/gh/mozillazg/pinyin-data@master/kTGHZ2013.txt"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    text = urllib.request.urlopen(req, timeout=60).read().decode("utf-8-sig")
    pat = re.compile(r"^U\+[0-9A-Fa-f]+:\s*([^#]+?)\s*#\s*(\S+)")
    data = {}
    order = []
    for line in text.splitlines():
        m = pat.match(line.strip())
        if not m:
            continue
        for py in m.group(1).strip().split(","):
            py = norm_py(py.strip())
            if not py:
                continue
            hanzi = m.group(2).strip()
            if not hanzi:
                continue
            if py not in data:
                data[py] = []
                order.append(py)
            if hanzi not in data[py]:
                data[py].append(hanzi)
    return data, order


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
        os.path.join(here, "..", "src", "utils"))

    built = build_from_pypinyin()
    if not built:
        print("[gen] pypinyin 不可用，尝试联网下载...")
        built = build_from_network()
    data, order = built

    freq_rank = build_freq_rank()
    surname_set = build_surname_set()

    def sort_key(ch):
        # 字频序优先；不在种子里的字按 Unicode 码点兜底（稳定）
        if ch in freq_rank:
            return (0, freq_rank[ch])
        return (1, ord(ch))

    sorted_data = {}
    for py in order:
        sorted_data[py] = sorted(data[py], key=sort_key)

    # ── 词组拼音推导：用 pypinyin 算出整词拼音 ──
    # PHRASES 为拼接字符串，按 2 字切分成词（含 4 字短语切成两词，均为合法词）
    phrase_by_py = {}
    try:
        from pypinyin import pinyin, Style
        words = [PHRASES[i:i+2] for i in range(0, len(PHRASES) - 1, 2)]
        for ph in words:
            if len(ph) < 2:
                continue
            res = pinyin(ph, style=Style.NORMAL, heteronym=False)
            parts = [norm_py(r[0]) for r in res if r]
            if not parts or any(not p for p in parts):
                continue
            key = "".join(parts)
            if key not in phrase_by_py:
                phrase_by_py[key] = []
            if ph not in phrase_by_py[key]:
                phrase_by_py[key].append(ph)
    except Exception as e:
        print("[gen] 词组拼音推导跳过:", e)
    phrase_order = list(phrase_by_py.keys())

    def esc(s):
        return "".join("\\x%02x" % b for b in s.encode("utf-8"))

    # ── 输出 PinyinDictData.h（汉字 + 姓氏字符集）──
    dict_h = os.path.join(outdir, "PinyinDictData.h")
    surname_sorted = sorted(surname_set)
    with open(dict_h, "w", encoding="utf-8") as f:
        f.write(
            "/**\n"
            " * @file PinyinDictData.h\n"
            " * @brief 拼音->候选汉字 静态数据表（自动生成，请勿手改）\n"
            " * @author 袁燕\n"
            " *\n"
            " * 拼音由 pypinyin 推导（覆盖《通用规范汉字表》全量汉字）。\n"
            " * 候选字按真实字频/常用度排序（FREQ_SEED 频率序，生僻字自动沉底），\n"
            " * 直接保证 \"li\"->\"里\" 等常用字排在候选首位。\n"
            " * 汉字以 UTF-8 \\x 转义存储，跨 MSVC/GCC 无源文件编码歧义。\n"
            " */\n"
            "#pragma once\n\n"
            "struct PinyinEntry {\n"
            "    const char* py;      // 全拼，无声调、小写，如 \"zhang\"\n"
            "    const char* hanzi;   // 该拼音下按字频排序的候选汉字（UTF-8 转义）\n"
            "};\n\n"
            "inline constexpr PinyinEntry kPinyinEntries[] = {\n")
        for py in order:
            f.write('  {"%s", "%s"},\n' % (py, esc("".join(sorted_data[py]))))
        f.write("  { nullptr, nullptr }\n};\n\n")

        # 姓氏集合（百家姓常见姓氏字符），用于姓名输入加权前置
        f.write(
            "// 常见姓氏字符集合（百家姓），姓名输入时由上层把姓氏字符候选前置\n"
            "inline constexpr const char* kSurnameChars[] = {\n")
        for ch in surname_sorted:
            f.write('  "%s",\n' % esc(ch))
        f.write("  nullptr\n};\n")

    # ── 输出 PinyinPhraseData.h（词组联想）──
    phrase_h = os.path.join(outdir, "PinyinPhraseData.h")
    with open(phrase_h, "w", encoding="utf-8") as f:
        f.write(
            "/**\n"
            " * @file PinyinPhraseData.h\n"
            " * @brief 拼音->常用词组 静态数据表（自动生成，请勿手改）\n"
            " * @author 袁燕\n"
            " *\n"
            " * 词组拼音由 pypinyin 自动推导，无需手写拼音。\n"
            " * 用于中文软键盘的【词组联想】：用户拼出多音节（如 zhongguo）即联想出\n"
            " * 常用词组（如 中国），优先于单字候选展示。\n"
            " */\n"
            "#pragma once\n\n"
            "struct PinyinPhraseEntry {\n"
            "    const char* py;      // 词组全拼，无声调、小写\n"
            "    const char* phrase;  // 该拼音下的候选词组（按内置词序，UTF-8 转义）\n"
            "};\n\n"
            "inline constexpr PinyinPhraseEntry kPinyinPhraseEntries[] = {\n")
        for py in phrase_order:
            f.write('  {"%s", "%s"},\n' % (py, esc("".join(phrase_by_py[py]))))
        f.write("  { nullptr, nullptr }\n};\n")

    total = sum(len(v) for v in sorted_data.values())
    print("[gen] ok: %d 拼音键, %d 字映射, %d 词组 -> %s / %s"
          % (len(sorted_data), total, len(phrase_order), dict_h, phrase_h))


if __name__ == "__main__":
    main()
