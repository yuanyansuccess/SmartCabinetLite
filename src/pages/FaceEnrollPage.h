/**
 * @file FaceEnrollPage.h
 * @brief 人脸录入页面 - 管理员为员工录入人脸特征 (1:1复刻Vue版FaceEnroll.vue)
 * @author 袁燕
 *
 * 功能：
 *   - 选择已有用户，为其录入人脸数据
 *   - 摄像头实时预览 + 人脸检测框
 *   - [V2.16] 5方位引导采集：正面/左侧/右侧/偏上/偏下，实时方位检测匹配后才采集
 *   - 录入结果反馈(成功/失败/重复)
 */
#pragma once
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QImage>

class FaceCameraWidget;

class FaceEnrollPage : public QWidget {
    Q_OBJECT
public:
    explicit FaceEnrollPage(QWidget* parent = nullptr);
    ~FaceEnrollPage();

    /// 设置当前登录用户
    void setCurrentUser(const QJsonObject& user);

signals:
    void enrollSuccess(int userId, const QString& realName);

private slots:
    void onUserSelected(int index);
    void onStartEnroll();
    void onCapture();
    void onFaceCaptured(const QImage& image, double confidence);
    void onEnrollConfirm();
    void onCancel();
    void onCameraError(const QString& msg);
    /// [V2.16] 方位检测定时器回调：实时检测当前人脸方位
    void onPostureCheck();
    /// [V2.17] 简单模式自动采集回调（posture API不可用时的fallback）
    void onSimpleCapture();

private:
    void setupUI();
    void loadUsers();
    void resetEnroll();
    void saveFaceData(int userId, const QString& descriptor);
    /// [V8.2] 自动连续采集调度：每次采集完成后等待间隙再触发下一次
    void scheduleNextCapture();
    /// [V2.16] 启动/停止方位检测定时器
    void startPostureCheck();
    void stopPostureCheck();
    /// [V2.17] 检查face-server.js /posture端点可用性，选择采集模式
    void checkPostureServiceAndStart();
    /// [V2.17fix-0706] 启动简单模式（posture API不可用时的fallback）
    void startSimpleMode();

    // UI
    FaceCameraWidget* m_camera = nullptr;
    QComboBox* m_userCombo = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_confirmBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_instructionLabel = nullptr;
    QLabel* m_previewLabel = nullptr;
    QLabel* m_resultIcon = nullptr;
    QLabel* m_resultText = nullptr;
    QWidget* m_resultBox = nullptr;
    QWidget* m_enrollPanel = nullptr;
    /// [V2.16] 方位大字提示标签（左侧/右侧/上偏/下偏/居中）
    QLabel* m_directionLabel = nullptr;

    // 数据
    QJsonArray m_userList;
    QJsonObject m_currentUser;
    int m_selectedUserId = -1;
    QString m_selectedRealName;

    // 采集状态
    enum EnrollState { Idle, Capturing, Confirm, Success, Failed };
    EnrollState m_state = Idle;
    int m_captureCount = 0;
    static const int MAX_ENROLL_CAPTURES = 5;
    /// [V8.2] 连续采集间隔(ms)，给用户微调角度时间
    /// [V2.03l 2026-06-30] 增加到2500ms，给用户足够时间调整5个方向
    static const int CAPTURE_INTERVAL_MS = 2500;
    QString m_bestDescriptor;
    QImage m_bestImage;
    double m_bestConfidence = 0.0;

    // [V8.2] 自动连续采集定时器
    QTimer* m_autoCaptureTimer = nullptr;

    // [V2.17] 方位检测 — 双模式：深度检测模式(posture API) + 简单提示模式(fallback)
    /// 方位检测定时器（每400ms检测一次当前方位）
    QTimer* m_postureTimer = nullptr;
    /// 当前需要采集的目标方位索引（0=正面,1=左侧,2=右侧,3=偏上,4=偏下）
    int m_targetDirection = 0;
    /// 方位匹配连续帧数
    int m_postureMatchCount = 0;
    /// 方位检测间隔(ms)
    static const int POSTURE_CHECK_INTERVAL_MS = 400;
    /// [V2.17fix] 方位匹配所需连续帧数（2→5，需连续5帧=2秒才确认）
    static const int POSTURE_MATCH_REQUIRED = 5;
    /// 方位检测超时(ms)
    static const int POSTURE_TIMEOUT_MS = 60000;
    /// [V2.17fix] 最小停留时间：即使方位已匹配也要等够3秒才采集
    static const int MIN_STAY_MS = 3000;
    /// 方位检测开始时间戳（用于超时判断）
    qint64 m_postureStartTime = 0;
    /// [V2.17fix] 首次方位匹配时间戳（用于最小停留时间计算）
    qint64 m_postureFirstMatchTime = 0;
    /// 超时提示次数（超过3次则强制兜底采集）
    int m_postureTimeoutCount = 0;
    /// detectPosture连续失败次数（超过上限→切换简单模式）
    int m_postureFailCount = 0;
    static const int POSTURE_FAIL_FALLBACK = 10;
    /// 是否使用简单提示模式（posture API不可用时的fallback）
    bool m_simpleMode = false;
    /// 简单模式下的自动采集定时器
    QTimer* m_simpleCaptureTimer = nullptr;
    /// [V2.17fix] 简单模式下每个方向的等待时间(3秒→4秒)
    static const int SIMPLE_CAPTURE_DELAY_MS = 4000;
};
