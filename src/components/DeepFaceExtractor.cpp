/**
 * @file DeepFaceExtractor.cpp
 * @brief 深度学习人脸特征提取器实现 — HTTP调用常驻face-server.js
 * @author 袁燕
 *
 * [V2.03 2026-06-28] 性能优化：从"每次启动node进程"改为"HTTP调用常驻服务"
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

DeepFaceExtractor::DeepFaceExtractor(QObject* parent) : QObject(parent) {}

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

bool DeepFaceExtractor::checkServerHealth() {
    QNetworkAccessManager mgr;
    QNetworkRequest req;
    req.setUrl(QUrl("http://127.0.0.1:8089/health"));
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

bool DeepFaceExtractor::ensureServerRunning() {
    // 1. 先检查是否已在运行
    if (checkServerHealth()) {
        return true;
    }

    // 2. 启动face-server.js（detached，不随Qt退出）
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

    qDebug() << "[DeepFaceExtractor] Starting face-server.js...";
    QStringList args = {scriptPath};
    QProcess::startDetached(nodePath, args, dir);

    // 3. 等待服务就绪（最多15秒，首次加载模型）
    for (int i = 0; i < 30; ++i) {
        QThread::msleep(500);
        if (checkServerHealth()) {
            qDebug() << "[DeepFaceExtractor] face-server.js ready after" << (i + 1) * 500 << "ms";
            return true;
        }
    }

    qWarning() << "[DeepFaceExtractor] face-server.js failed to become ready within 15s";
    return false;
}

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
        reply->deleteLater();
        return QString();
    }
    QString result = QString::fromUtf8(reply->readAll());
    reply->deleteLater();
    return result;
}

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
        "http://127.0.0.1:8089/extract", body, TIMEOUT_MS);
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

// [V2.16 2026-07-06 袁燕] QImage转base64 JPEG（extract和detectPosture共用）
//   分辨率缩放到320px避免大图传输，质量75平衡速度与清晰度
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

// [V2.16 2026-07-06 袁燕] 人脸方位检测：调用face-server.js的/posture接口
//   比/extract快（不提取128维特征），用于录入页实时方位引导
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
        "http://127.0.0.1:8089/posture", body, POSTURE_TIMEOUT_MS);
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
