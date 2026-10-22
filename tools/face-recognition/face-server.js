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
 * [V2.20 2026-10-13 袁燕] 分段耗时计时（NPU提速决策前的数据采集）
 *   背景：登录RTT偏慢，需定位瓶颈在"图像传输/解码"还是"模型推理"，再决定
 *         是否投入NPU(RKNN)改造。无数据优化是盲目的，故先加五段计时。
 *   实现：链式API按face-api自有task链拆段（detectTask.run→withFaceLandmarks
 *         →withFaceDescriptor），中间结果自动复用不重复推理，与原链式等价。
 *   五段：decode(jpeg-js) / detect(tinyFaceDetector) / landmarks(68点)
 *         / descriptor(recognitionNet) / total(端到端)
 *   开关：SC_FACE_TIMING=0 关闭日志（默认开启，量产可关）；分段数据同时
 *         随响应体 timing 字段返回，test_face_rtt.js 直接展示。
 *
 * [V2.19 2026-10-13 提速] inputSize 224→160（见extractFeature内注释）
 *
 * 端口: 8089 (避开后端8088)
 */
'use strict';

const http = require('http');
const path = require('path');
const MODELS_PATH = path.join(__dirname, 'models');

// [V2.17 2026-10-04 袁燕] 人脸框边长下限(px)：低于此值视为距离过远。
//   与 Qt 端 SC::FACE_MIN_SIZE 保持一致；Qt 端先做本地提示，服务端此处兜底。
//   实测校准：正常约 95~105px，明显偏远约 55~62px，故取 70。
const FACE_MIN_SIZE = 70;

// [V2.20 2026-10-13 袁燕] 分段计时开关：默认开启（调试期采集数据用），
//   量产稳定后可以 SC_FACE_TIMING=0 关闭日志（timing字段仍随响应返回，零开销）
const TIMING_ENABLED = process.env.SC_FACE_TIMING !== '0';

let tf, jpeg, faceapi;
let modelsLoaded = false;
let ready = false;

async function init() {
    try {
        jpeg = require('jpeg-js');

        // [V2.21 2026-10-13 袁燕] 推理后端切换：tfjs-node 原生(Eigen多线程) 替代 纯JS
        //   背景：V2.20 分段计时实测——推理占端到端耗时 98%（纯JS后端下
        //         1000x1000 矩阵乘 3920ms，原生后端仅 38ms，差 100 倍），
        //         "登录慢"的根因是后端选错，不是模型/网络/参数问题。
        //   注意：tfjs-node 注册的后端名是 'tensorflow'，原代码 setBackend('cpu')
        //         会强制走纯JS后端——装了对的后端不切也白装。
        //   face-api 需同步换成 node 专用入口(dist/face-api.node)，默认入口
        //         内部绑定的是纯JS tfjs，与 tfjs-node 不共享后端。
        //   兜底：原生后端加载失败（如麒麟ARM无预编译二进制）时回退纯JS，
        //         保证服务可用（慢但能用），便于现场定位。
        try {
            tf = require('@tensorflow/tfjs-node');
            faceapi = require('@vladmandic/face-api/dist/face-api.node');
            await tf.setBackend('tensorflow');
            await tf.ready();
            console.log('[face-server] 推理后端: tfjs-node 原生(Eigen多线程) ✓');
        } catch (nativeErr) {
            console.warn('[face-server] tfjs-node 原生后端加载失败，回退纯JS后端:', nativeErr.message);
            tf = require('@tensorflow/tfjs');
            faceapi = require('@vladmandic/face-api');
            await tf.setBackend('cpu');
            await tf.ready();
            console.log('[face-server] 推理后端: tfjs 纯JS(慢速兼容兜底)');
        }

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

/**
 * [V2.20 2026-10-13 袁燕] 提取128维人脸特征（含五段耗时计时）
 *
 * @param imageBase64  JPEG 图像的 Base64 文本
 * @returns {Promise<{success:boolean, error?:string, descriptor?:number[],
 *           confidence?:number, landmarks?:number[][], timing?:object}>}
 *          success=false 时 error 为原因；timing 为分段耗时(ms)，供瓶颈定位
 */
async function extractFeature(imageBase64) {
    if (!modelsLoaded) throw new Error('模型未加载完成');

    // 分段计时起点（t0 含解码，定位"传输/解码 vs 推理"的构成）
    const t0 = Date.now();

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
    const tDecode = Date.now() - t0;   // 段1：base64解码+jpeg解码+张量构建

    // 五段计时的分段时刻（在 try 内逐段刷新，finally 统一 disposal）
    let tDetect = 0, tLandmarks = 0, tDescriptor = 0;
    try {
        // [V2.18 2026-10-04 袁燕] 注意：face-api.js 链式 API 存在强制依赖——
        //   必须先调用 withFaceLandmarks()，withFaceDescriptor() 才存在
        //   （descriptor 需基于 landmarks 对齐后计算）。曾尝试移除 landmarks
        //   以省一次模型推理，结果报 "withFaceDescriptor is not a function"
        //   导致提取 100% 失败，故保留该调用。
        //   客户端虽不消费 landmarks 字段，但模型调用不可省。
        // [V2.19 2026-10-13 提速] inputSize 224→160：检测推理计算量随输入尺寸
        //   显著下降（约提速40~50%）。160=32×5符合face-api要求；
        //   正常登录距离（人脸框>35px@320图）检出无影响。
        // [V2.20 2026-10-13 袁燕] 拆段计时：用 face-api 自有 task 链把原
        //   链式调用拆为三段 await——detectTask 先 run 得到检测框，再
        //   withFaceLandmarks/withFaceDescriptor 复用父任务结果（内部不
        //   重复推理），与原 `detectSingleFace().withFaceLandmarks()
        //   .withFaceDescriptor()` 完全等价，仅用于插入计时点。
        const detectTask = faceapi.detectSingleFace(
            tensor,
            new faceapi.TinyFaceDetectorOptions({ inputSize: 160, scoreThreshold: 0.4 })
        );

        // 段2：人脸检测（tinyFaceDetector）
        const detection = await detectTask.run();
        tDetect = Date.now() - t0 - tDecode;
        if (!detection) {
            return { success: false, error: '未检测到人脸，请正对摄像头' };
        }

        // 段3：68关键点（faceLandmark68Net，descriptor 的对齐依赖它）
        const landmarkTask = detectTask.withFaceLandmarks();
        const withLandmarks = await landmarkTask.run();
        tLandmarks = Date.now() - t0 - tDecode - tDetect;
        if (!withLandmarks || !withLandmarks.landmarks) {
            return { success: false, error: '未检测到人脸，请正对摄像头' };
        }

        // 段4：128维特征（faceRecognitionNet，输入为landmarks对齐后的112图）
        const result = await landmarkTask.withFaceDescriptor().run();
        tDescriptor = Date.now() - t0 - tDecode - tDetect - tLandmarks;
        if (!result || !result.descriptor) {
            return { success: false, error: '未检测到人脸，请正对摄像头' };
        }

        // [V2.17 2026-10-04 袁燕] 距离门控：人脸框过小说明特征像素不足，
        //   此时提取出的描述子与库内近距离特征相似度必然偏低（不是人的问题），
        //   明确告知客户端"请靠近"，避免客户端反复重试或误判为陌生人。
        //   性能：仅一次整数比较，服务端本就持有检测框，开销可忽略。
        const box = detection.box;
        const faceSize = Math.min(box.width, box.height);
        if (faceSize < FACE_MIN_SIZE) {
            // 记录尺寸便于现场校准阈值（不含身份信息，可安全落盘）
            console.log(`[face-server] 人脸过小(${Math.round(faceSize)}px < ${FACE_MIN_SIZE}px)，提示靠近 耗时=${Date.now() - t0}ms`);
            return {
                success: false,
                error: '请靠近',
                code: 'FACE_TOO_FAR',
                faceSize: Math.round(faceSize),
                timing: buildTiming(tDecode, tDetect, tLandmarks, tDescriptor, t0)
            };
        }
        // [V2.20] 分段计时日志：五段构成一眼定位瓶颈（默认开，SC_FACE_TIMING=0关）
        const timing = buildTiming(tDecode, tDetect, tLandmarks, tDescriptor, t0);
        if (TIMING_ENABLED) {
            console.log(`[face-server] timing decode=${timing.decode}ms detect=${timing.detect}ms`
                + ` landmarks=${timing.landmarks}ms descriptor=${timing.descriptor}ms`
                + ` total=${timing.total}ms faceSize=${Math.round(faceSize)}px`);
        }
        // 保持原有响应结构（含 landmarks），确保与客户端行为完全一致
        const landmarks = withLandmarks.landmarks.positions.map(p => [Math.round(p.x * 100) / 100, Math.round(p.y * 100) / 100]);
        return {
            success: true,
            descriptor: Array.from(result.descriptor),
            confidence: result.detection.score || 0.8,
            landmarks: landmarks,
            timing: timing
        };
    } finally {
        tensor.dispose();
    }
}

/**
 * [V2.20 2026-10-13 袁燕] 组装分段计时对象
 *
 * @param decode     base64解码+jpeg解码+张量构建耗时(ms)
 * @param detect     人脸检测耗时(ms)
 * @param landmarks  68关键点耗时(ms)
 * @param descriptor 128维特征耗时(ms)
 * @param t0         请求处理起点时刻(Date.now()值)
 * @returns {{decode:number, detect:number, landmarks:number,
 *           descriptor:number, total:number}} 各段耗时(ms)
 */
function buildTiming(decode, detect, landmarks, descriptor, t0) {
    return {
        decode,
        detect,
        landmarks,
        descriptor,
        total: Date.now() - t0
    };
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
    const t0 = Date.now();
    const tDecode = 0;   // posture 调用方为方位引导，暂不拆分 decode 段（保持轻量）
    let tDetect = 0, tLandmarks = 0;
    try {
        // 只检测landmarks，不提取descriptor，速度更快
        // [V2.19 2026-10-13 提速] inputSize 224→160（与/extract同步提速）
        // [V2.20 2026-10-13 袁燕] 拆段计时（与/extract同法，task链复用不重复推理）
        const detectTask = faceapi.detectSingleFace(
            tensor,
            new faceapi.TinyFaceDetectorOptions({ inputSize: 160, scoreThreshold: 0.4 })
        );

        const detection = await detectTask.run();
        tDetect = Date.now() - t0;
        if (!detection) {
            return { success: false, error: '未检测到人脸' };
        }

        const result = await detectTask.withFaceLandmarks().run();
        tLandmarks = Date.now() - t0 - tDetect;
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
            pitch: Math.round(pitch * 1000) / 1000,
            timing: { decode: tDecode, detect: tDetect, landmarks: tLandmarks, descriptor: 0, total: Date.now() - t0 }
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
