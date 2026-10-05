/**
 * @file DeepFaceExtractor.cpp
 * @brief 深度学习人脸特征提取器实现 — HTTP调用常驻face-server.js
 * @author 袁燕
 *
 * 性能优化：从"每次启动node进程"改为"HTTP调用常驻服务"
 *   原方案：QProcess启动node extract-feature.js → 加载模型2-3s/次
 *   新方案：ensureServerRunning()启动face-server.js常驻 → HTTP POST /extract
 *   提速：2-3s → <300ms（约10倍）
 *
 * 实现原理：
 *   1. ensureServerRunning() 检查face-server.js健康状态，未运行则后台启动
 *   2. QImage → JPEG → base64 → POST http://127.0.0.1:8089/extract
 *   3. QEventLoop同步等待HTTP响应 → 解析JSON → 128维特征字符串
 */
#include "DeepFaceExtractor.h"
#include "common/Constants.h"  // SC::FACE_SERVER_HOST / SC::FACE_SERVER_PORT 人脸服务地址统一来源
#include <QBuffer>
#include <QByteArray>
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDebug>
#include <QtNetwork>
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <atomic>

namespace {
// 服务就绪记忆：就绪后不再每帧重复做/health探测（HTTP风暴），响应异常时自动清除以便自愈
// 该状态会被 HTTP 回调线程写入、UI 线程读取，必须用原子避免竞态
std::atomic<bool> g_serverReady{false};
}

DeepFaceExtractor::DeepFaceExtractor(QObject* parent) : QObject(parent) {}

/**
 * @brief 定位 Node 可执行文件
 * @return Node 路径；未找到时返回空字符串
 */
QString DeepFaceExtractor::findNodePath() {
    // 通过PATH查找node（跨平台：Windows用where，Linux/macOS用which）
    QProcess envCheck;
#ifdef Q_OS_WIN
    envCheck.start("where", QStringList{"node"});
#else
    envCheck.start("which", QStringList{"node"});
#endif
    envCheck.waitForFinished(3000);
    QString output = QString::fromUtf8(envCheck.readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        return output.split('\n').first().trimmed();
    }
    // 回退：常见安装路径探测
#ifdef Q_OS_WIN
    QStringList candidates = {
        "C:\\Program Files\\nodejs\\node.exe",
        "C:\\Program Files (x86)\\nodejs\\node.exe"
    };
#else
    QStringList candidates = {
        "/usr/bin/node",
        "/usr/local/bin/node",
        "/opt/node/bin/node"
    };
#endif
    for (const auto& p : candidates) {
        if (QFileInfo::exists(p)) return p;
    }
    return QString();
}

/**
 * @brief 定位人脸识别服务脚本目录
 * @return 服务目录路径；未找到时返回空字符串
 */
QString DeepFaceExtractor::scriptDir() {
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/../../tools/face-recognition",
        appDir + "/../../../tools/face-recognition",
        appDir + "/tools/face-recognition",
    };
    for (const auto& dir : candidates) {
        QString absDir = QDir::cleanPath(dir);
        if (QFileInfo::exists(absDir + "/face-server.js")) {
            return absDir;
        }
    }
    return appDir + "/tools/face-recognition";
}

/**
 * @brief 判断人脸识别服务是否可用
 * @return true=健康检查通过
 */
bool DeepFaceExtractor::isAvailable() {
    QString nodePath = findNodePath();
    if (nodePath.isEmpty()) {
        qWarning() << "[DeepFaceExtractor] Node.js not found in PATH";
        return false;
    }
    QString dir = scriptDir();
    if (!QFileInfo::exists(dir + "/face-server.js")) {
        qWarning() << "[DeepFaceExtractor] face-server.js not found at" << dir;
        return false;
    }
    if (!QFileInfo::exists(dir + "/models/tiny_face_detector_model-shard1")) {
        qWarning() << "[DeepFaceExtractor] model files not found at" << dir << "/models";
        return false;
    }
    if (!QFileInfo::exists(dir + "/node_modules/@vladmandic/face-api")) {
        qWarning() << "[DeepFaceExtractor] @vladmandic/face-api not installed at" << dir;
        return false;
    }
    return true;
}

/**
         * @brief 请求服务健康接口确认其可用
         * @return true=服务已就绪
         */
bool DeepFaceExtractor::checkServerHealth() {
    QNetworkAccessManager mgr;
    QNetworkRequest req;
    req.setUrl(QUrl(QString("http://%1:%2/health")
        .arg(SC::FACE_SERVER_HOST).arg(SC::FACE_SERVER_PORT)));
    req.setTransferTimeout(800);
    QNetworkReply* reply = mgr.get(req);

    QEventLoop loop;
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return false;
    }
    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return false;
    return doc.object()["ready"].toBool(false);
}

/// 本程序拉起的人脸服务子进程（由qApp托管，主程序退出时统一终止）
static QProcess* g_faceServerProcess = nullptr;

/**
         * @brief 以子进程方式启动人脸识别服务
         * @return true=进程已拉起
         */
bool DeepFaceExtractor::startServerProcess() {
    if (g_faceServerProcess && g_faceServerProcess->state() != QProcess::NotRunning) {
        return true;  // 已由本程序拉起且仍在运行
    }

    QString nodePath = findNodePath();
    if (nodePath.isEmpty()) {
        qWarning() << "[DeepFaceExtractor] Node.js not found, cannot start face-server";
        return false;
    }
    QString dir = scriptDir();
    QString scriptPath = dir + "/face-server.js";
    if (!QFileInfo::exists(scriptPath)) {
        qWarning() << "[DeepFaceExtractor] face-server.js not found at" << scriptPath;
        return false;
    }

    if (!g_faceServerProcess) {
        // 父对象为应用实例：进程随主程序销毁，避免留下孤儿进程
        g_faceServerProcess = new QProcess(qApp);
    }
    g_faceServerProcess->setWorkingDirectory(dir);
    g_faceServerProcess->start(nodePath, QStringList{scriptPath});
    if (!g_faceServerProcess->waitForStarted(3000)) {
        qWarning() << "[DeepFaceExtractor] face-server.js 启动失败:"
                   << g_faceServerProcess->errorString();
        return false;
    }
    qInfo() << "[DeepFaceExtractor] 已拉起人脸识别服务:" << scriptPath;
    return true;
}

/**
         * @brief 异步轮询等待服务就绪
         * @param timeoutMs 最长等待毫秒数
         * @param callback 就绪或超时后的回调
         */
void DeepFaceExtractor::waitReadyAsync(int triedTimes) {
    if (checkServerHealth()) {
        qInfo() << "[DeepFaceExtractor] 人脸识别服务就绪，用时约"
                << (triedTimes + 1) * 500 << "ms";
        return;
    }
    if (triedTimes >= 29) {  // 最多等15秒（首次加载模型）
        qWarning() << "[DeepFaceExtractor] 人脸识别服务15秒内未就绪";
        return;
    }
    QTimer::singleShot(500, qApp, [triedTimes]() { waitReadyAsync(triedTimes + 1); });
}

/**
         * @brief 预启动服务，不阻塞调用方
         * @note 供程序启动阶段调用：主线程拉起进程，QTimer 轮询就绪状态
         */
void DeepFaceExtractor::prestartAsync() {
    if (!isAvailable()) {
        qWarning() << "[DeepFaceExtractor] 人脸识别环境不完整(缺少node/脚本/模型)，跳过预启动";
        return;
    }
    // 延后到事件循环第一轮执行，不拖慢主窗口显示
    QTimer::singleShot(0, qApp, []() {
        if (checkServerHealth()) {
            qInfo() << "[DeepFaceExtractor] 人脸识别服务已在运行，直接复用";
            return;
        }
        if (!startServerProcess()) return;
        waitReadyAsync(0);
    });
}

/**
         * @brief 关闭由本程序拉起的人脸识别服务进程
         */
void DeepFaceExtractor::shutdownServer() {
    g_serverReady = false;
    if (!g_faceServerProcess) return;                 // 未拉起过（外部服务）→ 不干预
    if (g_faceServerProcess->state() == QProcess::NotRunning) return;
    qInfo() << "[DeepFaceExtractor] 主程序退出，停止人脸识别服务";
    g_faceServerProcess->terminate();
    if (!g_faceServerProcess->waitForFinished(3000)) {
        g_faceServerProcess->kill();
        g_faceServerProcess->waitForFinished(1000);
    }
}

/**
         * @brief 确保人脸识别服务处于运行状态
         * @return true=服务可用
         * @note 优先复用已就绪的服务；未就绪时按需重新拉起并等待
         */
bool DeepFaceExtractor::ensureServerRunning() {
    // 0. 已确认就绪（上次健康检查通过且服务未失联）→ 直接返回，避免高频/health探测
    if (g_serverReady) return true;

    // 1. 服务已就绪（本程序拉起或外部已启动）→ 直接返回
    if (checkServerHealth()) {
        g_serverReady = true;
        return true;
    }

    // 2. 本程序已持有子进程但还在加载模型 → 同步等待就绪
    if (g_faceServerProcess && g_faceServerProcess->state() != QProcess::NotRunning) {
        for (int i = 0; i < 30; ++i) {
            QThread::msleep(500);
            if (checkServerHealth()) {
                g_serverReady = true;
                return true;
            }
        }
        qWarning() << "[DeepFaceExtractor] face-server.js failed to become ready within 15s";
        return false;
    }

    // 3. 主线程：交本程序托管启动；其它线程：回退startDetached（QProcess受线程亲和性限制）
    if (QThread::currentThread() == qApp->thread()) {
        if (!startServerProcess()) return false;
    } else {
        QString nodePath = findNodePath();
        if (nodePath.isEmpty()) {
            qWarning() << "[DeepFaceExtractor] Node.js not found, cannot start face-server";
            return false;
        }
        QString dir = scriptDir();
        QString scriptPath = dir + "/face-server.js";
        if (!QFileInfo::exists(scriptPath)) {
            qWarning() << "[DeepFaceExtractor] face-server.js not found at" << scriptPath;
            return false;
        }
        QProcess::startDetached(nodePath, QStringList{scriptPath}, dir);
    }

    // 4. 等待服务就绪（最多15秒，首次加载模型）
    for (int i = 0; i < 30; ++i) {
        QThread::msleep(500);
        if (checkServerHealth()) {
            g_serverReady = true;
            qDebug() << "[DeepFaceExtractor] face-server.js ready after" << (i + 1) * 500 << "ms";
            return true;
        }
    }

    qWarning() << "[DeepFaceExtractor] face-server.js failed to become ready within 15s";
    return false;
}

/**
         * @brief 向识别服务发起同步 HTTP 请求
         * @param path 接口路径
         * @param body 请求体 JSON
         * @param timeoutMs 超时毫秒数
         * @param outMessage 失败原因输出参数
         * @return 响应 JSON；失败时返回空对象
         */
QString DeepFaceExtractor::httpPostSync(const QString& url, const QByteArray& body, int timeoutMs) {
    QNetworkAccessManager mgr;
    QUrl reqUrl(url);
    QNetworkRequest req;
    req.setUrl(reqUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(timeoutMs);

    QNetworkReply* reply = mgr.post(req, body);

    QEventLoop loop;
    QTimer::singleShot(timeoutMs + 1000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[DeepFaceExtractor] HTTP error:" << reply->errorString();
        g_serverReady = false;   // 服务无响应 → 清除就绪记忆，下次调用重新走健康检查/拉起
        reply->deleteLater();
        return QString();
    }
    QString result = QString::fromUtf8(reply->readAll());
    reply->deleteLater();
    return result;
}

/**
         * @brief 从图像提取人脸特征
         * @param imageBase64 图像的 Base64 文本
         * @param outFeature 特征串输出参数（128维，逗号分隔）
         * @param outConfidence 置信度输出参数
         * @param outMessage 失败原因输出参数
         * @return true=提取成功
         */
bool DeepFaceExtractor::extract(const QImage& image,
                                 QString& outFeature,
                                 double& outConfidence,
                                 QString& outMessage) {
    outFeature.clear();
    outConfidence = 0;
    outMessage.clear();

    // 1. 确保face-server.js常驻服务在运行
    if (!ensureServerRunning()) {
        outMessage = QStringLiteral("人脸识别服务启动失败，请检查Node.js环境");
        return false;
    }

    // 2. QImage → base64 JPEG
    QByteArray base64Data = imageToBase64Jpeg(image);

    // 3. 构建JSON请求体
    QJsonObject bodyObj;
    bodyObj["image"] = QString::fromUtf8(base64Data);
    QByteArray body = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    // 4. HTTP POST到face-server.js
    QString response = httpPostSync(
        QString("http://%1:%2/extract")
            .arg(SC::FACE_SERVER_HOST).arg(SC::FACE_SERVER_PORT),
        body, TIMEOUT_MS);
    if (response.isEmpty()) {
        outMessage = QStringLiteral("人脸识别服务请求超时");
        return false;
    }

    // 5. 解析JSON响应
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        outMessage = QStringLiteral("JSON解析失败: ") + parseError.errorString();
        return false;
    }

    QJsonObject obj = doc.object();
    bool success = obj["success"].toBool(false);
    if (!success) {
        outMessage = obj["error"].toString(QStringLiteral("特征提取失败"));
        return false;
    }

    // 6. 提取128维特征描述子
    QJsonArray descriptor = obj["descriptor"].toArray();
    if (descriptor.size() < 64) {
        outMessage = QStringLiteral("特征维度不足: %1 (需要128)").arg(descriptor.size());
        return false;
    }

    QStringList parts;
    for (const auto& v : descriptor) {
        parts.append(QString::number(v.toDouble(), 'f', 8));
    }
    outFeature = parts.join(",");
    outConfidence = obj["confidence"].toDouble(0.8);

    qDebug() << "[DeepFaceExtractor] 特征提取成功，维度=" << descriptor.size()
             << "置信度=" << outConfidence;
    return true;
}

// QImage转base64 JPEG（extract和detectPosture共用）
// 分辨率缩放到320px避免大图传输，质量75平衡速度与清晰度
QByteArray DeepFaceExtractor::imageToBase64Jpeg(const QImage& image) {
    QImage scaledImg = image;
    if (image.width() > 320 || image.height() > 320) {
        scaledImg = image.scaled(320, 320, Qt::KeepAspectRatio,
                                  Qt::FastTransformation);
    }
    QByteArray jpegBytes;
    QBuffer buffer(&jpegBytes);
    buffer.open(QIODevice::WriteOnly);
    scaledImg.save(&buffer, "JPEG", 75);
    buffer.close();
    return jpegBytes.toBase64();
}

// 人脸方位检测：调用face-server.js的/posture接口
// 比/extract快（不提取128维特征），用于录入页实时方位引导
bool DeepFaceExtractor::detectPosture(const QImage& image,
                                       double& outYaw,
                                       double& outPitch,
                                       QString& outMessage) {
    outYaw = 0;
    outPitch = 0;
    outMessage.clear();

    // 1. 确保face-server.js常驻服务在运行
    if (!ensureServerRunning()) {
        outMessage = QStringLiteral("人脸识别服务未就绪");
        return false;
    }

    // 2. QImage → base64 JPEG
    QByteArray base64Data = imageToBase64Jpeg(image);

    // 3. 构建JSON请求体
    QJsonObject bodyObj;
    bodyObj["image"] = QString::fromUtf8(base64Data);
    QByteArray body = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    // 4. HTTP POST到face-server.js /posture接口
    QString response = httpPostSync(
        QString("http://%1:%2/posture")
            .arg(SC::FACE_SERVER_HOST).arg(SC::FACE_SERVER_PORT),
        body, POSTURE_TIMEOUT_MS);
    if (response.isEmpty()) {
        outMessage = QStringLiteral("方位检测请求超时");
        return false;
    }

    // 5. 解析JSON响应
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        outMessage = QStringLiteral("JSON解析失败: ") + parseError.errorString();
        return false;
    }

    QJsonObject obj = doc.object();
    bool success = obj["success"].toBool(false);
    if (!success) {
        outMessage = obj["error"].toString(QStringLiteral("方位检测失败"));
        return false;
    }

    outYaw = obj["yaw"].toDouble(0);
    outPitch = obj["pitch"].toDouble(0);
    return true;
}
