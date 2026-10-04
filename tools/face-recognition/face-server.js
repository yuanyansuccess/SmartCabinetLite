/**
 * @file face-server.js
 * @brief 人脸特征提取常驻HTTP服务 — 启动一次模型，后续请求毫秒级响应
 * @author 袁燕
 *
 * [V2.03 2026-06-28] 性能优化：从"每次启动node进程"改为"常驻HTTP服务"
 *   原方案：每次captureNow()启动node extract-feature.js → 加载模型2-3s → 总计9s(3帧)
 *   新方案：Qt启动时拉起face-server.js常驻 → 模型只加载一次 → 每次请求<300ms
 *
 * [V2.16 2026-07-06] 新增/posture接口：只检测landmarks不提取特征，用于人脸录入方位引导
 *   /extract响应增加landmarks字段（68个[x,y]坐标），供Qt端方位判断
 *
 * API:
 *   POST /extract  body: { "image": "<base64 JPEG>" }
 *     响应: { "success": true, "descriptor": [128 floats], "confidence": 0.9, "landmarks": [[x,y],...] }
 *     响应: { "success": false, "error": "未检测到人脸" }
 *   POST /posture  body: { "image": "<base64 JPEG>" }
 *     响应: { "success": true, "landmarks": [[x,y],...], "yaw": 0.12, "pitch": 0.05 }
 *     响应: { "success": false, "error": "未检测到人脸" }
 *
 * 端口: 8089 (避开后端8088)
 */
'use strict';

const http = require('http');
const path = require('path');
const MODELS_PATH = path.join(__dirname, 'models');

// [V2.17 2026-10-04 袁燕] 人脸框边长下限(px)：低于此值视为距离过远。
//   与 Qt 端 SC::FACE_MIN_SIZE 保持一致；Qt 端先做本地提示，服务端此处兜底。
const FACE_MIN_SIZE = 80;

let tf, jpeg, faceapi;
let modelsLoaded = false;
let ready = false;

async function init() {
    try {
        tf = require('@tensorflow/tfjs');
        jpeg = require('jpeg-js');
        faceapi = require('@vladmandic/face-api');

        await tf.setBackend('cpu');
        await tf.ready();

        await faceapi.nets.tinyFaceDetector.loadFromDisk(MODELS_PATH);
        await faceapi.nets.faceLandmark68Net.loadFromDisk(MODELS_PATH);
        await faceapi.nets.faceRecognitionNet.loadFromDisk(MODELS_PATH);

        modelsLoaded = true;
        ready = true;
        console.log('[face-server] 模型加载完成，服务就绪 @ http://127.0.0.1:8089');
    } catch (err) {
        console.error('[face-server] 初始化失败:', err.message);
        process.exit(1);
    }
}

async function extractFeature(imageBase64) {
    if (!modelsLoaded) throw new Error('模型未加载完成');

    const imageBuffer = Buffer.from(imageBase64, 'base64');
    const rawImageData = jpeg.decode(imageBuffer, { useTArray: true });

    const width = rawImageData.width;
    const height = rawImageData.height;
    const rgbaData = rawImageData.data;
    const rgbData = new Uint8Array(width * height * 3);
    for (let i = 0, j = 0; i < rgbaData.length; i += 4, j += 3) {
        rgbData[j] = rgbaData[i];
        rgbData[j + 1] = rgbaData[i + 1];
        rgbData[j + 2] = rgbaData[i + 2];
    }

    const tensor = tf.tensor3d(rgbData, [height, width, 3]);
    try {
        const result = await faceapi.detectSingleFace(
            tensor,
            new faceapi.TinyFaceDetectorOptions({ inputSize: 224, scoreThreshold: 0.4 })
        ).withFaceLandmarks().withFaceDescriptor();

        if (!result || !result.descriptor) {
            return { success: false, error: '未检测到人脸，请正对摄像头' };
        }

        // [V2.17 2026-10-04 袁燕] 距离门控：人脸框过小说明特征像素不足，
        //   此时提取出的描述子与库内近距离特征相似度必然偏低（不是人的问题），
        //   明确告知客户端"请靠近"，避免客户端反复重试或误判为陌生人。
        //   性能：仅一次整数比较，服务端本就持有检测框，开销可忽略。
        const box = result.detection && result.detection.box;
        if (box) {
            const faceSize = Math.min(box.width, box.height);
            if (faceSize < FACE_MIN_SIZE) {
                return {
                    success: false,
                    error: '请靠近',
                    code: 'FACE_TOO_FAR',
                    faceSize: Math.round(faceSize)
                };
            }
        }
        // [V2.16 2026-07-06] 返回68关键点坐标，供Qt端方位判断
        const landmarks = result.landmarks.positions.map(p => [Math.round(p.x * 100) / 100, Math.round(p.y * 100) / 100]);
        return {
            success: true,
            descriptor: Array.from(result.descriptor),
            confidence: result.detection.score || 0.8,
            landmarks: landmarks
        };
    } finally {
        tensor.dispose();
    }
}

// [V2.16 2026-07-06 袁燕] 人脸方位检测：只检测landmarks不提取特征，响应更快
//   入参：imageBase64 base64编码的JPEG图像
//   返回：{ success, landmarks, yaw, pitch } 或 { success:false, error }
//   yaw:  左右偏转比（>0.15左偏，<-0.15右偏，|yaw|<0.10居中）
//   pitch: 上下偏转比（>0.15抬头，<-0.15低头，|pitch|<0.10居中）
async function detectPosture(imageBase64) {
    if (!modelsLoaded) throw new Error('模型未加载完成');

    const imageBuffer = Buffer.from(imageBase64, 'base64');
    const rawImageData = jpeg.decode(imageBuffer, { useTArray: true });

    const width = rawImageData.width;
    const height = rawImageData.height;
    const rgbaData = rawImageData.data;
    const rgbData = new Uint8Array(width * height * 3);
    for (let i = 0, j = 0; i < rgbaData.length; i += 4, j += 3) {
        rgbData[j] = rgbaData[i];
        rgbData[j + 1] = rgbaData[i + 1];
        rgbData[j + 2] = rgbaData[i + 2];
    }

    const tensor = tf.tensor3d(rgbData, [height, width, 3]);
    try {
        // 只检测landmarks，不提取descriptor，速度更快
        const result = await faceapi.detectSingleFace(
            tensor,
            new faceapi.TinyFaceDetectorOptions({ inputSize: 224, scoreThreshold: 0.4 })
        ).withFaceLandmarks();

        if (!result || !result.landmarks) {
            return { success: false, error: '未检测到人脸' };
        }

        const pts = result.landmarks.positions;
        const landmarks = pts.map(p => [Math.round(p.x * 100) / 100, Math.round(p.y * 100) / 100]);

        // 方位计算：基于68关键点几何
        // 鼻尖=index30，右眼中心=avg(36-41)，左眼中心=avg(42-47)
        // 嘴巴中心=avg(48-67)
        let leftEyeX = 0, leftEyeY = 0;
        for (let i = 42; i <= 47; i++) { leftEyeX += pts[i].x; leftEyeY += pts[i].y; }
        leftEyeX /= 6; leftEyeY /= 6;

        let rightEyeX = 0, rightEyeY = 0;
        for (let i = 36; i <= 41; i++) { rightEyeX += pts[i].x; rightEyeY += pts[i].y; }
        rightEyeX /= 6; rightEyeY /= 6;

        const eyeMidX = (leftEyeX + rightEyeX) / 2;
        const eyeMidY = (leftEyeY + rightEyeY) / 2;
        const eyeDist = Math.abs(leftEyeX - rightEyeX);

        const noseTip = pts[30];

        // 左右偏转：鼻尖x相对两眼中心x的偏移 / 两眼间距
        // 正值=鼻子偏图像右（用户左转），负值=鼻子偏图像左（用户右转）
        const yaw = eyeDist > 1 ? (noseTip.x - eyeMidX) / eyeDist : 0;

        // 上下偏转：鼻尖到嘴巴中心距离 / 两眼中心到鼻尖距离
        // 抬头时嘴巴-鼻子距离变大，低头时变小
        let mouthX = 0, mouthY = 0;
        for (let i = 48; i <= 67; i++) { mouthX += pts[i].x; mouthY += pts[i].y; }
        mouthX /= 20; mouthY /= 20;

        const noseToMouth = Math.abs(mouthY - noseTip.y);
        const eyeToNose = Math.abs(noseTip.y - eyeMidY);
        // pitch = (实际距离比 - 标准距离比) / 标准距离比
        // 正常 noseToMouth/eyeToNose ≈ 0.8，抬头时变小，低头时变大
        const ratioStandard = 0.8;
        const ratioActual = eyeToNose > 1 ? noseToMouth / eyeToNose : 0;
        // 正值=低头（ratio变大），负值=抬头（ratio变小）
        const pitch = (ratioActual - ratioStandard) / ratioStandard;

        return {
            success: true,
            landmarks: landmarks,
            yaw: Math.round(yaw * 1000) / 1000,
            pitch: Math.round(pitch * 1000) / 1000
        };
    } finally {
        tensor.dispose();
    }
}

const server = http.createServer(async (req, res) => {
    res.setHeader('Content-Type', 'application/json');

    if (req.method === 'GET' && req.url === '/health') {
        res.end(JSON.stringify({ ready, modelsLoaded }));
        return;
    }

    if (req.method === 'POST' && req.url === '/extract') {
        let body = '';
        req.on('data', chunk => { body += chunk; });
        req.on('end', async () => {
            try {
                if (!ready) {
                    res.end(JSON.stringify({ success: false, error: '服务未就绪' }));
                    return;
                }
                const data = JSON.parse(body);
                if (!data.image) {
                    res.end(JSON.stringify({ success: false, error: '缺少image参数' }));
                    return;
                }
                const result = await extractFeature(data.image);
                res.end(JSON.stringify(result));
            } catch (err) {
                res.end(JSON.stringify({ success: false, error: err.message }));
            }
        });
        return;
    }

    // [V2.16 2026-07-06 袁燕] 人脸方位检测接口（录入页用，比/extract快）
    if (req.method === 'POST' && req.url === '/posture') {
        let body = '';
        req.on('data', chunk => { body += chunk; });
        req.on('end', async () => {
            try {
                if (!ready) {
                    res.end(JSON.stringify({ success: false, error: '服务未就绪' }));
                    return;
                }
                const data = JSON.parse(body);
                if (!data.image) {
                    res.end(JSON.stringify({ success: false, error: '缺少image参数' }));
                    return;
                }
                const result = await detectPosture(data.image);
                res.end(JSON.stringify(result));
            } catch (err) {
                res.end(JSON.stringify({ success: false, error: err.message }));
            }
        });
        return;
    }

    res.statusCode = 404;
    res.end(JSON.stringify({ error: 'Not Found' }));
});

server.listen(8089, '127.0.0.1', () => {
    console.log('[face-server] HTTP服务启动 @ http://127.0.0.1:8089');
    init();
});

process.on('SIGINT', () => { server.close(); process.exit(0); });
process.on('SIGTERM', () => { server.close(); process.exit(0); });
