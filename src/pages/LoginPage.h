/**
 * @file LoginPage.h
 * @brief 登录页面 - 1:1复刻Vue版Login.vue (双栏+刷脸+密码降级+软键盘)
 * @author 袁燕
 *
 * 状态机: scanning → capturing → success/fail/stranger → no-camera
 *
 * 完善：SoftKeyboard接入、状态闪烁圆点、陌生人卡片完善、密码表单完善
 */
#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QEvent>
#include <QJsonObject>
#include <QDateTime>  // 注销抑制窗口时间戳
#include <QVBoxLayout>

class FaceCameraWidget;
class NumKeypad;

class LoginPage : public QWidget {
    Q_OBJECT
public:
    explicit LoginPage(QWidget* parent = nullptr);
    ~LoginPage();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;  // 退出按钮跟随窗口右上角
    void showEvent(QShowEvent* event) override;  // 窗口显示时定位退出按钮

public:
  /// 重置所有登录状态，清除上一用户的残留信息
    void resetPageState();
    /// 公开停止摄像头方法，供MainWindow在登录成功后兜底调用
    void stopFaceRecognitionPublic() { stopFaceRecognition(); }
    /// 注销后抑制自动刷脸登录：人未离开画面时不自动回登，onFaceLost清除
    /// 抑制时长由 kLogoutSuppressWindowMs 控制，当前为 0（不抑制）；
    /// 机制整体保留：若现场需要防"注销瞬间被同一张脸弹回"，
    /// 把该值改为 3000~5000 即启用。
    /// 配套：resumeFaceRecognition 路径文案为"请重新刷脸"。
    void suppressAutoLoginAfterLogout() {
        m_logoutSuppressed = true;
        m_logoutSuppressUntilMs = QDateTime::currentMSecsSinceEpoch() + kLogoutSuppressWindowMs;
    }
    /// 回到登录页时恢复人脸识别（注销/自动锁屏后摄像头需重新启动，已运行则跳过）
    /// @note relogin=true 时状态文案为"请重新刷脸"（注销/锁屏回登场景）
    void resumeFaceRecognition() {
        if (!m_faceRecognitionActive) startFaceRecognition(true);
    }

signals:
    void loginSuccess(QJsonObject user);

private slots:
    void onFaceDetected();
    void onFaceLost();
    /// 距离过远（人脸框过小）提示，引导用户靠近摄像头
    void onFaceTooFarChanged(bool tooFar);
    /**
     * @brief 距离提示优先级最高：在识别流程各阶段调用，
     *        只要距离仍偏远就持续显示"请靠近"，不被"正在验证身份"等文案覆盖
     * @return true=已显示距离提示（调用方应跳过其原本的文案设置）
     */
    bool keepDistanceHint();
    /**
     * @brief 统一状态提示入口（所有状态文案经此设置，避免颜色/字号混杂与截断）
     * @param text  提示文案（保持简短，控件宽度有限）
     * @param level 0=常规灰 1=引导橙 2=成功绿 3=失败红
     */
    void setScanStatus(const QString& text, int level = 0);
    void onFaceCaptured(const QImage& image, double confidence);
    void onCameraError(const QString& msg);
    void onFaceStateChanged(int state);
    void onUsernameFieldClicked();
    void onPasswordFieldClicked();
    void onPasswordLogin();
    /// 忘记密码：弹出刷脸验证对话框，成功后自助设置新密码
    void onForgotPassword();
    void onTryFaceAgain();
    void onExitSystem();  // 退出系统按钮

private:
    /// 人脸识别状态机（枚举替代裸字符串，避免拼写错误导致状态判断静默失效）
    enum class FaceResult {
        Idle,        // 未开始
        Scanning,    // 扫描中（等待人脸入画）
        Capturing,   // 已捕获，正在提取特征
        Success,     // 识别成功
        Fail,        // 识别失败
        Stranger,    // 陌生人
        NoCamera     // 无摄像头
    };
    /// 状态→可读文本（日志/调试用，禁止用于状态判断）
    static QString faceResultName(FaceResult result);

    void setupUI();
    void setupLeftPanel(QVBoxLayout* parentLayout);
    void setupRightPanel(QVBoxLayout* parentLayout);
    /** 右侧认证区构建（setupRightPanel拆解，按区块划分） */
    void buildWelcomeHeader(QVBoxLayout* layout);
    void buildFaceScanArea(QVBoxLayout* layout);
    void buildStatusRow(QVBoxLayout* layout);
    /** 构建密码登录表单：拆为错误条/凭据字段/操作按钮，降低单函数规模 */
    void buildPasswordForm(QVBoxLayout* layout);
    /** 登录失败/摄像头不可用提示条（人脸失败或锁定文案共用样式入口） */
    void buildErrorTip(QVBoxLayout* pfLayout);
    /** 单条凭据输入行（工号或密码）：标签+圆角输入框+键盘按钮，两字段复用消除重复 */
    void buildCredentialField(QVBoxLayout* pfLayout, const QString& labelText, QLineEdit*& edit,
                              const QString& placeholder, bool isPassword, void (LoginPage::*clickedSlot)());
    /** 登录/忘记密码/重新刷脸 操作区 */
    void buildLoginActions(QVBoxLayout* pfLayout);
    void buildSuccessCard(QVBoxLayout* layout);
    void buildStrangerCard(QVBoxLayout* layout);
    void buildExitButton();
    void buildCopyright(QVBoxLayout* layout);
    void setFaceResult(FaceResult state);
    /// 清除所有表单输入和状态面板（供resetPageState内部使用）
    void clearAllForms();
    /// 显示红色错误提示（登录失败/密码锁定警示共用样式入口）
    void showFormError(const QString& text);
    /// 显示绿色操作提示（密码重置成功/锁定解除等正向反馈）
    void showFormNotice(const QString& text);
    /// 启动人脸识别
    /// @param relogin true=注销/锁屏回登场景（状态文案为"请重新刷脸"），false=首次登录
    void startFaceRecognition(bool relogin = false);
    void stopFaceRecognition();
    void verifyFace(const QString& descriptor, const QImage& image);
    void collectBestSample();
    /** 验证失败处理，尝试备用帧 */
    void handleVerifyFailure(const QString& errMsg);
    /** 本地降级验证（后端不可用时） */
    void doLocalFaceVerify(const QString& descriptor);
    /** 人脸识别成功统一处理 */
    void handleFaceSuccess(const QJsonObject& resp);
    /** 陌生人检测统一处理 */
    void handleFaceStranger(const QJsonObject& resp);
    /** 设置状态圆点颜色 */
    void setStatusDot(const QString& colorStyle);
    /** 停止状态圆点闪烁 */
    void stopDotBlink();
    /** 切换到密码登录 */
    void switchToPasswordLogin();
    /** 重新尝试人脸识别 */
    void retryFace();
    /** 注销抑制是否仍生效（未到截止时间才抑制） */
    bool isLogoutSuppressed() const {
        return m_logoutSuppressed
            && QDateTime::currentMSecsSinceEpoch() < m_logoutSuppressUntilMs;
    }
    /** 处理陌生人确认 */
    void handleStrangerConfirm();

    QLabel* m_welcomeLabel;
    QLabel* m_subtitleLabel;

    // 人脸摄像头
    QWidget* m_cameraWrap = nullptr;  // 摄像头外层容器，切换密码登录时整体隐藏
    FaceCameraWidget* m_faceCamera;
    QLabel* m_cameraStatusText;
    QLabel* m_captureProgress;
    /** 状态闪烁圆点 */
    QLabel* m_statusDot = nullptr;
    /** 圆点闪烁定时器 */
    QTimer* m_dotBlinkTimer = nullptr;
    /** 状态圆圈 - 1:1复刻Web版 .camera-area success/fail/stranger */
    QLabel* m_statusCircleSuccess = nullptr;
    QLabel* m_statusCircleFail = nullptr;
    QLabel* m_statusCircleStranger = nullptr;
    /** 圆圈下方状态文字(复刻Web版 .cam-text 14px #bbbbbb) */
    QLabel* m_failStatusText = nullptr;
    QLabel* m_strangerStatusText = nullptr;
    int m_captureCount = 0;
    static const int MAX_CAPTURES = 3;
    /// 人脸识别是否处于运行中（防止showEvent重复启动；注销/锁屏回登录页时自动恢复）
    bool m_faceRecognitionActive = false;
    /// 密码登录模式标志：true 时所有人脸回调不得改写界面状态。
    /// 切换密码登录后异步在途的采集回调仍会到达，必须整体拦截，
    /// 防止密码登录页浮现"请保持不动/请靠近"等人脸提示
    bool m_passwordMode = false;
    bool m_logoutSuppressed = false;  // 注销后抑制自动登录（人离开画面或超时后清除）
    /// 注销抑制截止时间戳（毫秒），超时后自动解除抑制
    qint64 m_logoutSuppressUntilMs = 0;
    /// 注销抑制窗口（毫秒）：0=不抑制；改为 3000~5000 可启用"注销后短暂不自动刷脸"
    static constexpr qint64 kLogoutSuppressWindowMs = 0;

    // 采集样本
    struct FaceSample {
        QString descriptor;
        QImage image;
        double confidence;
        double quality;
    };
    QVector<FaceSample> m_samples;

    // 密码登录
    QWidget* m_passwordForm;
    QLineEdit* m_usernameEdit;
    QLineEdit* m_passwordEdit;
    QPushButton* m_loginBtn;
    QLabel* m_errorLabel;
    /** 始终可见的密码登录入口链接 */
    QPushButton* m_altLoginHint = nullptr;
    /** 忘记密码入口链接（密码模式下显示） */
    QPushButton* m_forgotPwdBtn = nullptr;
    QPushButton* m_tryFaceBtn;
    QPushButton* m_exitBtn = nullptr;  // 退出系统按钮

    // 成功卡片
    QWidget* m_successBox;
    QLabel* m_successName;
    QLabel* m_successWorkNo;
    QLabel* m_successDept;
    QLabel* m_successTime;
    QLabel* m_successSimilarity;

    // 陌生人卡片 (完善)
    QWidget* m_strangerBox;
    QLabel* m_strangerTime;
    /** 陌生人ID */
    QLabel* m_strangerIdLabel = nullptr;
    /** IP地址 */
    QLabel* m_strangerIpLabel = nullptr;

    // 状态
    FaceResult m_faceResult = FaceResult::Idle;  // 状态机当前状态
    bool m_isVerifying = false;
    /// 距离提示当前是否已在显示（用于节流，避免每帧重设样式造成闪烁）
    bool m_distanceHintShown = false;
    /**
     * 识别采样预算：单次登录最多比对的帧数与最长等待时间。
     * 远距离/角度不佳时特征相似度天然偏低，而"多帧取最优"可在
     * 不降低任何阈值的前提下提升可用性（判定标准不变，安全性不受影响）。
     */
    int  m_verifyBudget = 0;        // 已比对帧数
    qint64 m_verifyStartMs = 0;     // 首次比对起始时间

    QTimer* m_timeoutTimer;
    QTimer* m_autoJumpTimer;
    /** 密码登录锁定倒计时（锁定中每秒刷新剩余时间提示，到期自动恢复） */
    QTimer* m_lockCountdownTimer = nullptr;

    // 软键盘
    // 账号改纯数字0....................................................................工号，用户名/密码统一由NumKeypad输入，移除字母软键盘
    NumKeypad* m_numKeypad = nullptr;   // 独立数字键盘，顶层Popup零穿透
    QString m_activeField; // "username" or "password"

    // 待登录用户(识别成功后暂存)
    QJsonObject m_pendingUser;
    bool m_hasCamera = true;
    bool m_destroying = false;  // 析构标记，防止析构期间信号触发访问已释放对象
};
