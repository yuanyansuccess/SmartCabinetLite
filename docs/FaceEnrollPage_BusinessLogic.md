# FaceEnrollPage 业务逻辑文档

## 1. 页面概述
**FaceEnrollPage** 是人脸录入页面，用于管理员为人员录入人脸信息。

### 核心功能
1. **摄像头预览**：实时显示摄像头画面
2. **人脸检测**：检测画面中的人脸
3. **人脸采集**：采集多张人脸图片
4. **人脸注册**：将人脸特征注册到系统
5. **录入结果**：显示录入成功/失败结果

## 2. 数据结构

### 2.1 人脸信息 (FaceInfo)
```cpp
struct FaceInfo {
    int id;                 // 人脸ID
    int userId;             // 用户ID
    QString faceToken;      // 人脸Token（百度云返回）
    QByteArray faceFeature; // 人脸特征数据
    QString enrollTime;     // 录入时间 (yyyy-MM-dd HH:mm:ss)
    int quality;            // 质量分数 (0-100)
    QString imagePath;      // 人脸图片路径
};
```

### 2.2 摄像头设备 (CameraDevice)
```cpp
struct CameraDevice {
    QString deviceId;       // 设备ID
    QString deviceName;     // 设备名称
    bool isAvailable;       // 是否可用
};
```

## 3. Service接口定义

### 3.1 FaceRecognitionService 接口

#### getCameraDevices()
```cpp
// 获取可用摄像头设备列表
QList<CameraDevice> getCameraDevices();
```

#### startCameraPreview()
```cpp
// 启动摄像头预览
bool startCameraPreview(const QString& deviceId = "");
```

#### stopCameraPreview()
```cpp
// 停止摄像头预览
void stopCameraPreview();
```

#### detectFace()
```cpp
// 检测人脸
bool detectFace(const QImage& image, QRect& faceRect, int& quality);
```

#### captureFace()
```cpp
// 采集人脸图片
QImage captureFace();
```

#### enrollFace()
```cpp
// 注册人脸
bool enrollFace(int userId, const QList<QImage>& faceImages);
```

#### deleteFace()
```cpp
// 删除人脸
bool deleteFace(int userId);
```

#### getFaceInfo()
```cpp
// 获取人脸信息
FaceInfo getFaceInfo(int userId);
```

## 4. UI组件映射

| Web组件 | Qt Widget | 说明 |
|---------|-----------|------|
| video元素 | QCamera + QVideoWidget | 摄像头预览 |
| canvas | QLabel + QPainter | 人脸检测框 |
| el-button | QPushButton | 开始/停止/采集/注册按钮 |
| el-progress | QProgressBar | 采集进度 |
| el-message | QMessageBox | 提示信息 |
| el-dialog | QDialog | 录入结果对话框 |

## 5. 业务逻辑流程

### 5.1 页面初始化
```
1. 调用getCameraDevices()获取摄像头列表
2. 选择默认摄像头
3. 调用startCameraPreview()启动预览
4. 开始人脸检测循环
```

### 5.2 人脸检测流程
```
1. 从摄像头获取当前帧（QImage）
2. 调用detectFace()检测人脸
3. 如果检测到人脸，绘制检测框
4. 显示质量分数
5. 如果质量达标，启用采集按钮
```

### 5.3 人脸采集流程
```
1. 点击"采集"按钮
2. 调用captureFace()采集当前帧
3. 将图片添加到采集列表
4. 更新采集进度（需要采集5张）
5. 如果采集完成，启用注册按钮
```

### 5.4 人脸注册流程
```
1. 选择要注册的用户
2. 点击"注册"按钮
3. 调用enrollFace()注册人脸
4. 显示注册结果
5. 如果成功，保存人脸图片到本地
```

## 6. 信号槽设计

### 6.1 信号
```cpp
// 摄像头预览帧更新
void frameUpdated(const QImage& frame);
// 人脸检测完成
void faceDetected(const QRect& faceRect, int quality);
// 人脸采集完成
void faceCaptured(const QImage& faceImage);
// 人脸注册完成
void faceEnrolled(bool success, const QString& message);
// 摄像头错误
void cameraError(const QString& error);
```

### 6.2 槽
```cpp
// 开始预览按钮点击
void onStartPreviewClicked();
// 停止预览按钮点击
void onStopPreviewClicked();
// 采集按钮点击
void onCaptureClicked();
// 注册按钮点击
void onEnrollClicked();
// 取消按钮点击
void onCancelClicked();
// 摄像头设备切换
void onCameraDeviceChanged(int index);
```

## 7. 关键代码片段

### 7.1 启动摄像头预览
```cpp
void FaceEnrollPage::startCameraPreview() {
    QString deviceId = ui->cameraComboBox->currentData().toString();
    
    bool success = FaceRecognitionService::instance()->startCameraPreview(deviceId);
    if (!success) {
        QMessageBox::warning(this, "错误", "启动摄像头失败！");
        return;
    }
    
    // 启动定时器，定时获取帧
    m_cameraTimer = new QTimer(this);
    connect(m_cameraTimer, &QTimer::timeout, this, &FaceEnrollPage::updateFrame);
    m_cameraTimer->start(33); // 30 FPS
}
```

### 7.2 更新帧并检测人脸
```cpp
void FaceEnrollPage::updateFrame() {
    QImage frame = FaceRecognitionService::instance()->captureFrame();
    if (frame.isNull()) return;
    
    // 人脸检测
    QRect faceRect;
    int quality;
    bool detected = FaceRecognitionService::instance()->detectFace(frame, faceRect, quality);
    
    if (detected) {
        // 绘制检测框
        QPainter painter(&frame);
        painter.setPen(QPen(Qt::green, 3));
        painter.drawRect(faceRect);
        
        // 显示质量分数
        painter.drawText(faceRect.bottomLeft() + QPoint(0, 20), 
                        QString("质量: %1").arg(quality));
        
        ui->qualityProgressBar->setValue(quality);
        ui->captureButton->setEnabled(quality >= 70); // 质量达标才允许采集
    } else {
        ui->qualityProgressBar->setValue(0);
        ui->captureButton->setEnabled(false);
    }
    
    // 显示帧
    ui->previewLabel->setPixmap(QPixmap::fromImage(frame.scaled(
        ui->previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}
```

### 7.3 采集人脸
```cpp
void FaceEnrollPage::onCaptureClicked() {
    QImage faceImage = FaceRecognitionService::instance()->captureFace();
    if (faceImage.isNull()) {
        QMessageBox::warning(this, "错误", "采集人脸失败！");
        return;
    }
    
    m_capturedImages.append(faceImage);
    updateCaptureProgress();
    
    // 显示采集的图片
    QLabel* thumbLabel = new QLabel(this);
    thumbLabel->setPixmap(QPixmap::fromImage(faceImage.scaled(80, 80, Qt::KeepAspectRatio)));
    ui->capturedLayout->addWidget(thumbLabel);
    
    // 检查是否采集完成（5张）
    if (m_capturedImages.size() >= 5) {
        ui->enrollButton->setEnabled(true);
        ui->captureButton->setEnabled(false);
        QMessageBox::information(this, "提示", "采集完成，可以注册了！");
    }
}
```

### 7.4 注册人脸
```cpp
void FaceEnrollPage::onEnrollClicked() {
    int userId = ui->userComboBox->currentData().toInt();
    if (userId <= 0) {
        QMessageBox::warning(this, "错误", "请选择用户！");
        return;
    }
    
    bool success = FaceRecognitionService::instance()->enrollFace(userId, m_capturedImages);
    
    if (success) {
        QMessageBox::information(this, "成功", "人脸注册成功！");
        clearCapture();
    } else {
        QMessageBox::warning(this, "失败", "人脸注册失败，请重试！");
    }
}
```

## 8. 数据格式转换

### 8.1 百度云返回格式 → Qt结构
```cpp
// 百度云返回: { face_token: "...", quality: 85 }
// Qt结构: { faceToken: "...", quality: 85 }

FaceInfo fromBaiduFormat(const QJsonObject& baiduData) {
    FaceInfo info;
    info.faceToken = baiduData["face_token"].toString();
    info.quality = baiduData["quality"].toInt();
    info.enrollTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    return info;
}
```

## 9. 错误处理

### 9.1 常见错误
1. **摄像头打开失败**：显示"无法打开摄像头，请检查设备"
2. **人脸检测失败**：显示"未检测到人脸，请调整位置"
3. **质量不达标**：显示"人脸质量不佳，请重新采集"
4. **注册失败**：显示"注册失败，请重试"

### 9.2 错误提示
```cpp
void FaceEnrollPage::showError(const QString& message) {
    QMessageBox::warning(this, "错误", message);
}
```

## 10. 测试要点

### 10.1 功能测试
- [ ] 摄像头预览
- [ ] 人脸检测
- [ ] 人脸采集（5张）
- [ ] 人脸注册
- [ ] 人脸删除

### 10.2 边界测试
- [ ] 无摄像头设备
- [ ] 摄像头被占用
- [ ] 人脸质量差
- [ ] 用户未选择

### 10.3 交互测试
- [ ] 按钮点击响应
- [ ] 采集进度显示
- [ ] 注册结果提示

## 11. 注意事项

1. **摄像头权限**：需要申请摄像头权限（麒麟系统）
2. **图片质量**：采集的人脸图片质量要足够高（建议5张不同角度）
3. **性能优化**：人脸检测要在后台线程执行，避免卡顿
4. **触屏优化**：按钮要足够大（56px高度），方便手指点击
5. **界面布局**：人脸预览区域要足够大（至少640x480）

## 12. 与Web版差异说明

| 项目 | Web版 | Qt版 |
|------|-------|------|
| 摄像头API | navigator.mediaDevices | QCamera |
| 人脸检测 | 百度云SDK（JS） | 百度云SDK（C++） |
| 图片处理 | Canvas API | QImage + QPainter |
| 文件保存 | FormData + AJAX | QFile |

## 13. 待实现功能

1. **多人脸检测**：同时检测多个人脸
2. **活体检测**：防止照片欺骗
3. **人脸比对**：比对两张人脸的相似度
4. **批量录入**：批量录入多个人脸

## 14. 依赖库

1. **Qt Multimedia**：摄像头预览
2. **Qt Network**：调用百度云API
3. **OpenCV**（可选）：本地人脸检测
4. **百度云C++ SDK**：人脸识别

## 15. 配置项

### 15.1 百度云配置
```cpp
// config.ini
[BaiduCloud]
API_KEY=your_api_key
SECRET_KEY=your_secret_key
```

### 15.2 摄像头配置
```cpp
// config.ini
[Camera]
DefaultDevice=0
Resolution=640x480
FrameRate=30
```

---
**文档版本**: v1.0  
**创建时间**: 2026-06-20  
**作者**: logic-agent
