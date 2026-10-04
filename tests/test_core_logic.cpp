/**
 * @file test_core_logic.cpp
 * @brief 核心纯逻辑单元测试：位置格式化 / 设计令牌 / 表单工厂（Qt Test）
 * @author 袁燕
 * @说明 覆盖新人最易改坏的公共逻辑：
 *   1. StyleHelper::formatPosition —— 全项目共用的位置格式化，改错影响所有列表显示
 *   2. StyleHelper::Token —— 触屏尺寸/字号标准，改动会全局影响UI，用测试锁死
 *   3. FormFactory —— 表单控件工厂（标签/字段值/开关）的样式收敛点
 *   不连数据库，无需MySQL环境。运行方式见 tests/CMakeLists.txt 注释。
 */
#include <QtTest/QtTest>
#include <QLabel>
#include <QCheckBox>
#include "utils/StyleHelper.h"
#include "components/FormFactory.h"
#include "common/AppConfig.h"
#include "controller/SettingController.h"
#include <QTemporaryDir>
#include <QFile>

class TestCoreLogic : public QObject {
    Q_OBJECT

private slots:
    // ── 位置格式化 ──
    void position_standardFormat();
    void position_singleDigitPadded();
    void position_stripsNonDigits();
    void position_emptyCabinetShowsDash();
    void position_missingLayerOrPosition();

    // ── 设计令牌（触屏标准，改动即全局UI变更，必须锁死）──
    void token_touchStandard();
    void token_radiusRange();

    // ── 颜色接口 ──
    void colors_areValidHex();

    // ── 表单工厂 ──
    void formFactory_formLabelStyle();
    void formFactory_fieldValueWraps();
    void formFactory_toggleStyle();

    // ── 配置可注入（AppConfig 放开构造 + SettingController 注入）──
    void appConfig_independentInstance();
    void settingController_usesInjectedConfig();
};

// ═══════════ 位置格式化 ═══════════

void TestCoreLogic::position_standardFormat() {
    QCOMPARE(StyleHelper::formatPosition("A柜", "03层", "05位"), QString("A-03-05"));
}

void TestCoreLogic::position_singleDigitPadded() {
    // 层/位为个位数时补零（项目约定：位置统一 A-01-02 两位补零）
    QCOMPARE(StyleHelper::formatPosition("B柜", "1", "2"), QString("B-01-02"));
    QCOMPARE(StyleHelper::formatPosition("A柜", "12", "9"), QString("A-12-09"));
}

void TestCoreLogic::position_stripsNonDigits() {
    // 含非数字字符时剔除（"第03层" → "03"）
    QCOMPARE(StyleHelper::formatPosition("C柜", "第03层", "第05位"), QString("C-03-05"));
}

void TestCoreLogic::position_emptyCabinetShowsDash() {
    // 柜名为空时柜号位用 "-" 占位，因此结果是 "--03-01"（占位符 + 分隔符）
    const QString pos = StyleHelper::formatPosition("", "03", "01");
    QCOMPARE(pos, QString("--03-01"));
}

void TestCoreLogic::position_missingLayerOrPosition() {
    // 层/位缺失时不产生 "NaN" 等异常文本
    const QString pos = StyleHelper::formatPosition("A柜", "", "");
    QVERIFY(!pos.contains(QStringLiteral("NaN")));
    QCOMPARE(pos, QString("A--"));
}

// ═══════════ 设计令牌 ═══════════

void TestCoreLogic::token_touchStandard() {
    // 触屏标准：主交互控件≥48px、正文字号≥16px（改动需同步更新规范文档）
    QVERIFY(StyleHelper::Token::ControlHeightTouch >= 48);
    QVERIFY(StyleHelper::Token::FontInput >= 16);
    QVERIFY(StyleHelper::Token::ControlHeight > 0);
    QVERIFY(StyleHelper::Token::ControlHeightLarge >= StyleHelper::Token::ControlHeightTouch);
}

void TestCoreLogic::token_radiusRange() {
    QVERIFY(StyleHelper::Token::RadiusSmall < StyleHelper::Token::Radius);
    QVERIFY(StyleHelper::Token::Radius <= StyleHelper::Token::RadiusLarge);
}

// ═══════════ 颜色 ═══════════

void TestCoreLogic::colors_areValidHex() {
    // 颜色方法返回非空且为 #RRGGBB（避免误删定义导致 QSS 失效）
    const QStringList colors = {
        StyleHelper::primaryColor(), StyleHelper::textColor(),
        StyleHelper::borderColor(), StyleHelper::successColor(),
        StyleHelper::dangerColor(), StyleHelper::warningColor()
    };
    for (const QString& c : colors) {
        QVERIFY2(c.startsWith('#'), qPrintable(QStringLiteral("color invalid: %1").arg(c)));
        QCOMPARE(c.size(), 7);
    }
}

// ═══════════ 表单工厂 ═══════════

void TestCoreLogic::formFactory_formLabelStyle() {
    QLabel* label = FormFactory::formLabel("工号");
    QVERIFY(label);
    QCOMPARE(label->text(), QString("工号"));
    QVERIFY(label->styleSheet().contains("font-weight:600"));
    QCOMPARE(label->width(), 100);          // 固定宽防止标签列过宽
    QVERIFY(label->minimumHeight() > 0);
    delete label;
}

void TestCoreLogic::formFactory_fieldValueWraps() {
    QLabel* value = FormFactory::fieldValue("A-01-02");
    QVERIFY(value);
    QVERIFY(value->wordWrap());             // 长文本必须换行不截断
    QVERIFY(value->styleSheet().contains("font-weight:600"));
    delete value;
}

void TestCoreLogic::formFactory_toggleStyle() {
    QCheckBox* box = FormFactory::toggle(true);
    QVERIFY(box);
    QVERIFY(box->isChecked());
    QVERIFY(box->styleSheet().contains("QCheckBox::indicator"));
    delete box;
}

void TestCoreLogic::appConfig_independentInstance() {
    // 验证放开构造后：可用指定路径构造独立实例，不会读写生产 system.ini
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString testIni = dir.path() + "/test_config.ini";

    AppConfig cfg(testIni);
    QCOMPARE(cfg.iniFilePath(), testIni);   // 使用指定路径，而非程序目录下的 system.ini
    QVERIFY(QFile::exists(testIni));        // 首次构造自动创建默认 INI

    cfg.setValue("UnitTest/probe", "hello");
    cfg.save();
    AppConfig reread(testIni);
    QCOMPARE(reread.value("UnitTest/probe", ""), QString("hello"));
}

void TestCoreLogic::settingController_usesInjectedConfig() {
    // 验证注入生效：控制器读取的是注入实例，而非全局单例
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString testIni = dir.path() + "/ctrl_config.ini";

    AppConfig cfg(testIni);
    cfg.setValue("UnitTest/key", "injected");
    cfg.save();

    SettingController ctrl(nullptr, &cfg);
    QCOMPARE(ctrl.setting("UnitTest/key", ""), QString("injected"));
}

QTEST_MAIN(TestCoreLogic)
#include "test_core_logic.moc"
