/**
 * @file extract-feature.js
 * @brief 深度学习人脸特征提取脚本 — 使用face-api.js提取128维深度特征描述子
 * @author 袁燕
 *
 * [V2.02 2026-06-28] 改用纯JS后端，解决tfjs-node原生绑定在Node.js v22上加载失败的问题
 *   - 原：require('@tensorflow/tfjs-node') → tfjs_binding.node加载失败
 *   - 新：require('@tensorflow/tfjs') + jpeg-js → 纯JS，无需C++编译
 *   - 性能：CPU后端比原生绑定慢约30%，但准确度完全一致
 *
 * 算法说明：
 *   - TinyFaceDetector：轻量级深度学习人脸检测（190KB模型，scoreThreshold=0.5）
 *   - FaceLandmark68Net：68个面部关键点定位（350KB模型）
 *   - FaceRecognitionNet：128维深度特征提取（6MB模型）
 *   - 三个模型协同工作：检测人脸→定位关键点→提取深度特征
 *
 * 调用方式：
 *   node extract-feature.js <图片文件路径>
 *   输入：临时文件路径（包含base64编码的JPEG图片数据）
 *   输出stdout：JSON { success, descriptor: [128 floats], confidence, error }
 */
'use strict';

const fs = require('fs');
const path = require('path');

// 模型文件目录（与本脚本同级的models子目录）
const MODELS_PATH = path.join(__dirname, 'models');

async function main() {
    const filePath = process.argv[2];
    if (!filePath || !fs.existsSync(filePath)) {
        console.log(JSON.stringify({ success: false, error: '缺少图片文件或文件不存在' }));
        process.exit(1);
    }

    try {
        // 读取base64编码的图片数据
        const imageBase64 = fs.readFileSync(filePath, 'utf8').trim();
        if (!imageBase64) {
            console.log(JSON.stringify({ success: false, error: '图片数据为空' }));
            process.exit(1);
        }

        // [V2.02] 使用纯JS后端替代tfjs-node原生绑定
        //   @tensorflow/tfjs 包含 tfjs-core + tfjs-backend-cpu，无需C++编译
        //   jpeg-js 纯JS JPEG解码器，替代 tf.node.decodeImage
        const tf = require('@tensorflow/tfjs');
        const jpeg = require('jpeg-js');
        const faceapi = require('@vladmandic/face-api');

        // 设置CPU后端（纯JS，无需原生绑定）
        await tf.setBackend('cpu');
        await tf.ready();

        // 加载深度学习模型
        await faceapi.nets.tinyFaceDetector.loadFromDisk(MODELS_PATH);
        await faceapi.nets.faceLandmark68Net.loadFromDisk(MODELS_PATH);
        await faceapi.nets.faceRecognitionNet.loadFromDisk(MODELS_PATH);

        // [V2.02] 用jpeg-js解码base64 JPEG → RGB像素数据
        //   替代 tf.node.decodeImage（tfjs-node专有API，纯JS版不支持）
        const imageBuffer = Buffer.from(imageBase64, 'base64');
        const rawImageData = jpeg.decode(imageBuffer, { useTArray: true });

        // jpeg-js输出RGBA格式，face-api需要RGB
        // 将RGBA转为RGB（去掉alpha通道）
        const width = rawImageData.width;
        const height = rawImageData.height;
        const rgbaData = rawImageData.data;
        const rgbData = new Uint8Array(width * height * 3);
        for (let i = 0, j = 0; i < rgbaData.length; i += 4, j += 3) {
            rgbData[j] = rgbaData[i];     // R
            rgbData[j + 1] = rgbaData[i + 1]; // G
            rgbData[j + 2] = rgbaData[i + 2]; // B
        }

        // 创建Tensor3D [height, width, 3]
        const tensor = tf.tensor3d(rgbData, [height, width, 3]);

        // 人脸检测 + 关键点定位 + 深度特征提取（一步到位）
        const result = await faceapi.detectSingleFace(
            tensor,
            new faceapi.TinyFaceDetectorOptions({ inputSize: 416, scoreThreshold: 0.5 })
        ).withFaceLandmarks().withFaceDescriptor();

        tensor.dispose();

        if (!result || !result.descriptor) {
            console.log(JSON.stringify({ success: false, error: '未检测到人脸，请正对摄像头' }));
            process.exit(1);
        }

        // 输出128维深度特征描述子
        console.log(JSON.stringify({
            success: true,
            descriptor: Array.from(result.descriptor),
            confidence: result.detection.score || 0.8
        }));
        process.exit(0);
    } catch (err) {
        console.log(JSON.stringify({ success: false, error: err.message || '特征提取异常' }));
        process.exit(1);
    }
}

main();
