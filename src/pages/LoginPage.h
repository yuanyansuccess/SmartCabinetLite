/**
 * @file LoginPage.h
 * @brief 登录页面 - 1:1复刻Vue版Login.vue (双栏+刷脸+密码降级+软键盘)
 * @author 袁燕
 *
 * 状态机: scanning → capturing → success/fail/stranger → no-camera
 * - scanning: 摄像头扫描中，自动采集
 * - capturing: 正在验证身份
 * - success: 识别成功，2秒后自动跳转
 * - fail: 识别失败，降级密码登录
 * - stranger: 检测到陌生人
 * - no-camera: 无摄像头，密码登录
 *
 * [2026-06-14] 完善：SoftKeyboard接入、状态闪烁圆点、陌生人卡片完善、密码表单完善
 */
#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QEvent>
#include <QJsonObject>
#include <QVBoxLayout>
#include <QStackedWidget>

class FaceCameraWidget;
class SoftKeyboard;
class NumKeypad;

class LoginPage : public QWidget {
    Q_OBJECT
public:
    explicit LoginPage(QWidget* parent = nullptr);
    ~LoginPage();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;  // [V2.04] 退出按钮跟随窗口右上角
    void showEvent(QShowEvent* event) override;      // [V2.05] 窗口显示时定位退出按钮

public:
    /// [V6.3 退出登录修复] 重置所有登录状态，清除上一用户的残留信息
    void resetPageState();
    /// [2026-06-23] 公开停止摄像头方法，供MainWindow在登录成功后兜底调用
    void stopFaceRecognitionPublic() { stopFaceRecognition(); }

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
    void onExitSystem();  // [V2.03u] 退出系统按钮

private:
    void setupUI();
    void setupLeftPanel(QVBoxLayout* parentLayout);
    void setupRightPanel(QVBoxLayout* parentLayout);
    void setFaceResult(const QString& state);
    /// [V6.3] 清除所有表单输入和状态面板（供resetPageState内部使用）
    void clearAllForms();
    void startFaceRecognition();
    void stopFaceRecognition();
    void verifyFace(const QString& descriptor, const QImage& image);
    void collectBestSample();
    /** [v3 新增] 验证失败处理，尝试备用帧 */
    void handleVerifyFailure(const QString& errMsg);
    /** [v4 新增] 本地降级验证（后端不可用时） */
    void doLocalFaceVerify(const QString& descriptor);
    /** [v4 新增] 人脸识别成功统一处理 */
    void handleFaceSuccess(const QJsonObject& resp);
    /** [v4 新增] 陌生人检测统一处理 */
    void handleFaceStranger(const QJsonObject& resp);
    /** [新增] 设置状态圆点颜色 */
    void setStatusDot(const QString& colorStyle);
    /** [新增] 停止状态圆点闪烁 */
    void stopDotBlink();
    /** [v2.0] 切换到密码登录 */
    void switchToPasswordLogin();
    /** [复刻Vue版] 重新尝试人脸识别 */
    void retryFace();
    /** [复刻Vue版] 处理陌生人确认 */
    void handleStrangerConfirm();

    // 左侧品牌区
    QWidget* m_leftPanel;
    // 右侧认证区
    QWidget* m_rightPanel;
    QStackedWidget* m_rightStack;
    QLabel* m_welcomeLabel;
    QLabel* m_subtitleLabel;

    // 人脸摄像头
    QWidget* m_cameraWrap = nullptr;        // [v4] 摄像头外层容器，切换密码登录时整体隐藏
    FaceCameraWidget* m_faceCamera;
    QLabel* m_cameraStatusIcon;
    QLabel* m_cameraStatusText;
    QLabel* m_captureProgress;
    /** [新增] 状态闪烁圆点 */
    QLabel* m_statusDot = nullptr;
    /** [新增] 圆点闪烁定时器 */
    QTimer* m_dotBlinkTimer = nullptr;
    /** [V6.5] 状态圆圈 - 1:1复刻Web版 .camera-area success/fail/stranger */
    QLabel* m_statusCircleSuccess = nullptr;
    QLabel* m_statusCircleFail = nullptr;
    QLabel* m_statusCircleStranger = nullptr;
    /** [V6.5] 圆圈下方状态文字(复刻Web版 .cam-text 14px #bbbbbb) */
    QLabel* m_successStatusText = nullptr;
    QLabel* m_failStatusText = nullptr;
    QLabel* m_strangerStatusText = nullptr;
    int m_captureCount = 0;
    static const int MAX_CAPTURES = 3;

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
    /** [v2.0] 始终可见的密码登录入口链接 */
    QPushButton* m_altLoginHint = nullptr;
    QPushButton* m_tryFaceBtn;
    QPushButton* m_exitBtn = nullptr;  // [V2.03u] 退出系统按钮

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
    /** [新增] 陌生人ID */
    QLabel* m_strangerIdLabel = nullptr;
    /** [新增] IP地址 */
    QLabel* m_strangerIpLabel = nullptr;

    // 状态
    QString m_faceResult; // scanning/capturing/success/fail/stranger/no-camera
    bool m_isVerifying = false;
    QTimer* m_timeoutTimer;
    QTimer* m_autoJumpTimer;

    // 软键盘
    SoftKeyboard* m_softKeyboard;
    NumKeypad* m_numKeypad = nullptr;   // [2026-06-26] 独立数字键盘，嵌入式零穿透
    QString m_activeField; // "username" or "password"

    // 待登录用户(识别成功后暂存)
    QJsonObject m_pendingUser;
    bool m_hasCamera = true;
    bool m_destroying = false;    // [v4] 析构标记，防止析构期间信号触发访问已删除对象
};
