/**
 * @file face-server.js
 * @brief 人脸特征提取常驻HTTP服务 — 启动一次模型，后续请求毫秒级响应
 * @author 袁燕
 *
 * 常驻 HTTP 服务：模型只加载一次，后续请求毫秒级响应（Qt 启动时拉起，退出时回收）。
 *
 * API:
 *   POST /extract  body: { "image": "<base64 JPEG>" }
 *     人脸检测与 68 点定位由 face-api 完成，特征向量由 512 维引擎（face-server-512.py @ 8090）提供。
 *     响应: { "success": true, "descriptor": [512 floats], "confidence": 0.9, "landmarks": [[x,y],...] }
 *     响应: { "success": false, "error": "未检测到人脸" }
 *   POST /posture  body: { "image": "<base64 JPEG>" }
 *     只检测 landmarks 不提取特征，用于人脸录入方位引导（比 /extract 快）。
 *     响应: { "success": true, "landmarks": [[x,y],...], "yaw": 0.12, "pitch": 0.05 }
 *     响应: { "success": false, "error": "未检测到人脸" }
 *
 * 耗时计时：请求处理拆为 decode/detect/landmarks/descriptor(512引擎)/total 五段，
 * 随响应体 timing 字段返回；SC_FACE_TIMING=0 可关闭日志输出。
 *
 * 端口: 8089 (避开后端8088)
 */
'use strict';

const http = require('http');
const path = require('path');
const os = require('os');
const { spawn, execSync } = require('child_process');
const MODELS_PATH = path.join(__dirname, 'models');

// 512 维引擎（onnxruntime/OpenMP）默认不会用满多核，单帧推理约 1.2s；
// 显式开启多线程后降到 ~0.45s（约 2.7 倍）。本机多核，留少量余量给 UI/常驻服务。
// 此处注入到 8089 进程环境，spawn 出的 8090 子进程自动继承，无需改 512 引擎代码。
const _faceThreads = String(Math.max(4, os.cpus().length - 4));
if (!process.env.ORT_NUM_THREADS) process.env.ORT_NUM_THREADS = _faceThreads;
if (!process.env.OMP_NUM_THREADS) process.env.OMP_NUM_THREADS = _faceThreads;
if (!process.env.OMP_WAIT_POLICY) process.env.OMP_WAIT_POLICY = 'PASSIVE';

// 特征向量由 512 维引擎（InsightFace buffalo_l，face-server-512.py @ 8090）提供：
// landmarks(68点)/posture/yaw/pitch 仍由 face-api 完成，活体/方位/定位逻辑不受影响。
// 512 引擎输出已 L2 归一化，余弦相似度=内积；判定阈值见 src/services/FaceRecognitionService.h。
// 512 维引擎依赖 Python + InsightFace；Python 解释器路径优先取环境变量 FACE_PYTHON，
// 否则按平台自动探测（Windows: where py/python/python3；Linux: which python3/python）。
const FACE_SERVER_512_PY = path.join(__dirname, '..', 'face-recognition-512', 'face-server-512.py');
const FACE_SERVER_512_URL = 'http://127.0.0.1:8090';

/**
 * 定位 512 维引擎使用的 Python 解释器。
 *
 * @brief 优先读环境变量 FACE_PYTHON，再按平台逐个探测候选命令；
 *        Windows 应用商店的占位程序不是解释器，命中时跳过。
 * @return 解释器路径或命令名；全部探测失败时返回平台默认命令名
 */
function findPython() {
  if (process.env.FACE_PYTHON) return process.env.FACE_PYTHON;
  const isWin = process.platform === 'win32';
  const probe = isWin ? 'where' : 'which';
  // py 是 Windows 官方启动器，路径固定且不会命中应用商店占位程序，优先探测。
  const candidates = isWin ? ['py', 'python', 'python3'] : ['python3', 'python'];
  for (const command of candidates) {
    try {
      const lines = execSync(`${probe} ${command}`, { stdio: ['ignore', 'pipe', 'ignore'] })
        .toString().trim().split(/\r?\n/);
      // WindowsApps 下的 python.exe 是应用商店的占位程序，不是解释器。
      const found = lines.map(line => line.trim()).find(
        line => line && !line.includes('WindowsApps'));
      if (found) return found;
    } catch (probeErr) { /* 未找到，尝试下一个 */ }
  }
  return isWin ? 'python' : 'python3';
}
const PY512 = findPython();

// 512 维引擎状态：/health 需要把真实状态告诉上位机，避免只报本服务就绪。
let engine512Ready = false;
let engine512Error = '';
let child512 = null;
let ensureTimer = null;

// 人脸框边长下限(px)：低于此值视为距离过远，提示"请靠近"。
// 与 Qt 端 SC::FACE_MIN_SIZE 保持一致；Qt 端先提示，服务端再判一次，防止漏判。
const FACE_MIN_SIZE = 70;

// 分段计时开关：SC_FACE_TIMING=0 关闭日志（timing 字段仍随响应返回）
const TIMING_ENABLED = process.env.SC_FACE_TIMING !== '0';

let tf, jpeg, faceapi;
let modelsLoaded = false;
let ready = false;

async function init() {
    try {
        jpeg = require('jpeg-js');

        // 推理后端：优先 tfjs-node 原生(Eigen多线程)，纯JS后端下推理比原生慢约100倍。
        // 注意：tfjs-node 注册的后端名是 'tensorflow'，setBackend('cpu') 会强制走纯JS；
        // face-api 需用 node 专用入口(dist/face-api.node)才与 tfjs-node 共享后端。
        // 原生后端加载失败时（如麒麟ARM无预编译二进制）改用纯JS，保证服务可用。
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
            console.log('[face-server] 推理后端: tfjs 纯JS(兼容用，速度较慢)');
        }

        // face-api 仅承担人脸检测(tinyFaceDetector)与 68 点定位(faceLandmark68Net)；
        // 特征向量由 512 维引擎(face-server-512.py @ 8090)提供。
        await faceapi.nets.tinyFaceDetector.loadFromDisk(MODELS_PATH);
        await faceapi.nets.faceLandmark68Net.loadFromDisk(MODELS_PATH);

        modelsLoaded = true;
        ready = true;
        console.log('[face-server] 模型加载完成，服务就绪 @ http://127.0.0.1:8089');
    } catch (err) {
        console.error('[face-server] 初始化失败:', err.message);
        process.exit(1);
    }
}

/**
 * 提取人脸特征（含五段耗时计时）：检测与 68 点定位由 face-api 完成，
 * 特征向量由 512 维引擎提供。
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
        // face-api 负责人脸检测与 68 点定位（供客户端方位判断）；
        // 人脸特征向量由 512 维引擎（extract512）提供，无需再跑 faceRecognitionNet。
        // inputSize 160：检测推理计算量随输入尺寸显著下降（约提速40~50%），
        // 160=32×5 符合 face-api 要求，正常登录距离（人脸框>35px@320图）检出无影响。
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

        // 段3：68关键点（faceLandmark68Net），供客户端方位判断使用
        const landmarkTask = detectTask.withFaceLandmarks();
        const withLandmarks = await landmarkTask.run();
        tLandmarks = Date.now() - t0 - tDecode - tDetect;
        if (!withLandmarks || !withLandmarks.landmarks) {
            return { success: false, error: '未检测到人脸，请正对摄像头' };
        }

        // 距离门控：人脸框过小说明特征像素不足，此时提取的描述子相似度
        // 必然偏低（不是人的问题），明确告知客户端"请靠近"，避免反复重试或误判陌生人。
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
        // 特征向量由 512 维引擎提供；只有引擎失联才更新状态并尝试重新拉起，
        // 业务失败（未检出人脸等）保持引擎状态不变，避免健康检查出现自相矛盾的取值。
        let descriptor512;
        try {
            const r512 = await extract512(imageBase64);
            if (!r512.success || !r512.descriptor) {
                const reason = '512维引擎：' + (r512.error || '未检出人脸');
                if (r512.engineUnavailable) {
                    engine512Ready = false;
                    engine512Error = reason;
                    ensure512();
                }
                return { success: false, error: reason, code: 'NO_FACE_512' };
            }
            descriptor512 = r512.descriptor;
            engine512Ready = true;
            engine512Error = '';
        } catch (callErr) {
            engine512Ready = false;
            engine512Error = '512维引擎调用失败：' + callErr.message;
            ensure512();
            return { success: false, error: engine512Error, code: 'ENGINE_512_ERR' };
        }

        // 段4：512 维引擎特征提取（含转发 8090）耗时
        tDescriptor = Date.now() - t0 - tDecode - tDetect - tLandmarks;

        // 分段计时日志：各段构成一眼定位瓶颈（SC_FACE_TIMING=0 关）
        const timing = buildTiming(tDecode, tDetect, tLandmarks, tDescriptor, t0);
        if (TIMING_ENABLED) {
            console.log(`[face-server] timing decode=${timing.decode}ms detect=${timing.detect}ms`
                + ` landmarks=${timing.landmarks}ms descriptor(512引擎)=${timing.descriptor}ms`
                + ` total=${timing.total}ms faceSize=${Math.round(faceSize)}px`);
        }
        // 保持原有响应结构（含 landmarks），确保与客户端行为完全一致
        const landmarks = withLandmarks.landmarks.positions.map(p => [Math.round(p.x * 100) / 100, Math.round(p.y * 100) / 100]);
        return {
            success: true,
            descriptor: descriptor512,
            confidence: detection.score || 0.8,
            landmarks: landmarks,
            timing: timing
        };
    } finally {
        tensor.dispose();
    }
}

/**
 * 组装分段计时对象
 *
 * @param decode     base64解码+jpeg解码+张量构建耗时(ms)
 * @param detect     人脸检测耗时(ms)
 * @param landmarks  68关键点耗时(ms)
 * @param descriptor 512维引擎特征提取(含转发8090)耗时(ms)
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

/**
 * 调用 512 维引擎（face-server-512.py @ 8090）提取特征向量。
 *
 * @param imageBase64  JPEG 图像的 Base64 文本（与 /extract 入参一致）
 * @returns {Promise<{success:boolean, descriptor?:number[], error?:string}>}
 */
function extract512(imageBase64) {
    const payload = JSON.stringify({ image: imageBase64 });
    return new Promise((resolve) => {
        const req = http.request(FACE_SERVER_512_URL + '/extract', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) }
        }, (resp) => {
            let body = '';
            resp.on('data', chunk => body += chunk);
            resp.on('end', () => {
                try { resolve(JSON.parse(body)); }
                catch (parseErr) {
                    resolve({ success: false, error: '512引擎响应解析失败', engineUnavailable: true });
                }
            });
        });
        req.on('error', (connErr) => resolve({
            success: false,
            error: '512引擎连接失败：' + connErr.message,
            engineUnavailable: true
        }));
        req.write(payload);
        req.end();
    });
}

/**
 * 拉起 512 维引擎进程，并接管其输出与退出事件。
 *
 * @brief 由本服务托管的子进程，随本服务退出而结束；输出用于定位模型或依赖问题。
 * @return 无返回值；拉起结果写入 engine512Ready / engine512Error 状态
 */
function spawn512() {
    if (child512) return;   // 已拉起且尚未退出，避免重复启动占用 8090 端口
    try {
        console.log('[face-server] 拉起 512 维引擎：', FACE_SERVER_512_PY);
        const child = spawn(PY512, [FACE_SERVER_512_PY], {
            cwd: path.dirname(FACE_SERVER_512_PY),
            // 子进程输出到管道时按 UTF-8 编码，转发到本服务日志才不会乱码。
            env: Object.assign({}, process.env, { PYTHONIOENCODING: 'utf-8' }),
            stdio: ['ignore', 'pipe', 'pipe']
        });
        child512 = child;
        let stderrTail = '';
        let stdoutPending = '';
        child.stdout.on('data', chunk => {
            // 按整行转发，避免一行日志被拆成多段后带上重复前缀。
            stdoutPending += chunk.toString();
            const lines = stdoutPending.split(/\r?\n/);
            stdoutPending = lines.pop();
            for (const line of lines) {
                if (line) process.stdout.write('[face-server-512] ' + line + '\n');
            }
        });
        child.stderr.on('data', chunk => {
            stderrTail = (stderrTail + chunk.toString()).slice(-2000);
        });
        child.on('error', err => {
            engine512Ready = false;
            engine512Error = '512 维引擎进程启动失败：' + err.message;
            console.warn('[face-server]', engine512Error);
            child512 = null;
        });
        child.on('exit', code => {
            engine512Ready = false;
            child512 = null;
            if (code !== 0) {
                engine512Error = stderrTail.trim().slice(-400) || ('512 维引擎退出，退出码 ' + code);
                console.warn('[face-server] 512 维引擎退出：', engine512Error);
            }
        });
    } catch (spawnErr) {
        engine512Ready = false;
        engine512Error = '512 维引擎拉起失败：' + spawnErr.message;
        console.warn('[face-server]', engine512Error);
        child512 = null;
    }
}

/**
 * 检查 512 维引擎是否就绪，未运行则拉起。
 *
 * @brief 供启动与定时轮询共用；端口已被占用但模型未就绪时不重复拉起。
 * @return 无返回值；检查结果写入 engine512Ready / engine512Error 状态
 */
function ensure512() {
    const req = http.get(FACE_SERVER_512_URL + '/health', (resp) => {
        let body = '';
        resp.on('data', chunk => body += chunk);
        resp.on('end', () => {
            let ok = false;
            try { ok = JSON.parse(body).ready === true; }
            catch (parseErr) { /* 响应不是 JSON，按未就绪处理 */ }
            if (ok) {
                if (!engine512Ready) console.log('[face-server] 512 维引擎已就绪');
                engine512Ready = true;
                engine512Error = '';
                return;
            }
            engine512Ready = false;
            engine512Error = '512 维引擎已启动但模型未就绪，请查看引擎日志';
        });
    });
    req.on('error', () => {
        engine512Ready = false;
        spawn512();
    });
    req.setTimeout(1500, () => req.destroy());
}

/**
 * 结束本服务拉起的 512 维引擎进程。
 *
 * @brief 供退出处理与 /shutdown 接口调用；进程已退出时不做任何操作。
 * @return 无返回值
 */
function stopChild512() {
    if (!child512) return;
    try { child512.kill(); } catch (killErr) { /* 进程已退出 */ }
    child512 = null;
}

// 人脸方位检测：只检测landmarks不提取特征，响应更快
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
        // inputSize 160 与 /extract 同步；task 链复用不重复推理
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
        res.end(JSON.stringify({ ready, modelsLoaded, engine512Ready, engine512Error }));
        return;
    }

    // 上位机退出前请求此接口：先结束 512 维引擎再关闭本服务，避免留下后台进程。
    if (req.method === 'POST' && req.url === '/shutdown') {
        res.end(JSON.stringify({ success: true }));
        stopChild512();
        server.close(() => process.exit(0));
        setTimeout(() => process.exit(0), 500).unref();
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

    // 人脸方位检测接口（录入页用，比/extract快）
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
    ensure512();
    // 引擎首次加载模型可能需要几十秒；定时复查，就绪或异常退出后都能自动恢复。
    if (!ensureTimer) {
        ensureTimer = setInterval(ensure512, 5000);
        ensureTimer.unref();
    }
    init();
});

process.on('SIGINT', () => { stopChild512(); server.close(); process.exit(0); });
process.on('SIGTERM', () => { stopChild512(); server.close(); process.exit(0); });
process.on('exit', () => { stopChild512(); });
