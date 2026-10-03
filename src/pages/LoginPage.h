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
    /// ⚠ 不可动：抑制必须有 kLogoutSuppressWindowMs 时间兜底。早前版本只靠"人离开画面"解除，
    /// 人一直站在镜头前会导致注销后永远无法刷脸登录（只能重启程序）
    /// 抑制仅在 kLogoutSuppressWindowMs 内有效，超时自动解除（人一直站在镜头前也能登录）
    void suppressAutoLoginAfterLogout() {
        m_logoutSuppressed = true;
        m_logoutSuppressUntilMs = QDateTime::currentMSecsSinceEpoch() + kLogoutSuppressWindowMs;
    }
    /// 回到登录页时恢复人脸识别（注销/自动锁屏后摄像头需重新启动，已运行则跳过）
    void resumeFaceRecognition() {
        if (!m_faceRecognitionActive) startFaceRecognition();
    }

signals:
    void loginSuccess(QJsonObject user);

private slots:
    void onFaceDetected();
    void onFaceLost();
    void onFaceCaptured(const QImage& image, double confidence);
    void onCameraError(const QString& msg);
    void onFaceStateChanged(int state);
    void onUsernameFieldClicked();
    void onPasswordFieldClicked();
    void onPasswordLogin();
    void onTryFaceAgain();
    void retryFaceTimeout();
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
    void buildPasswordForm(QVBoxLayout* layout);
    void buildSuccessCard(QVBoxLayout* layout);
    void buildStrangerCard(QVBoxLayout* layout);
    void buildExitButton();
    void buildCopyright(QVBoxLayout* layout);
    void setFaceResult(FaceResult state);
    /// 清除所有表单输入和状态面板（供resetPageState内部使用）
    void clearAllForms();
    void startFaceRecognition();
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
    QLabel* m_successStatusText = nullptr;
    QLabel* m_failStatusText = nullptr;
    QLabel* m_strangerStatusText = nullptr;
    int m_captureCount = 0;
    static const int MAX_CAPTURES = 3;
    /// 人脸识别是否处于运行中（防止showEvent重复启动；注销/锁屏回登录页时自动恢复）
    bool m_faceRecognitionActive = false;
    bool m_logoutSuppressed = false;  // 注销后抑制自动登录（人离开画面或超时后清除）
    /// 注销抑制截止时间戳（毫秒），超时后自动解除抑制
    qint64 m_logoutSuppressUntilMs = 0;
    /// 注销抑制窗口：仅防止注销瞬间被同一张脸立即弹回
    static constexpr qint64 kLogoutSuppressWindowMs = 5000;

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
    QTimer* m_timeoutTimer;
    QTimer* m_autoJumpTimer;

    // 软键盘
    // 账号改纯数字0....................................................................工号，用户名/密码统一由NumKeypad输入，移除字母软键盘
    NumKeypad* m_numKeypad = nullptr;   // 独立数字键盘，顶层Popup零穿透
    QString m_activeField; // "username" or "password"

    // 待登录用户(识别成功后暂存)
    QJsonObject m_pendingUser;
    bool m_hasCamera = true;
    bool m_destroying = false;  // 析构标记，防止析构期间信号触发访问已删除对象
};
