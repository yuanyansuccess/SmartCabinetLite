/**
 * @file  test_face_rtt.js
 * @brief 人脸特征提取耗时实测工具 — 验证提速效果
 * @author 袁燕
 *
 * 用法（face-server 需已在运行——主程序启动后自动拉起，或手动 node face-server.js）：
 *   node test_face_rtt.js <人脸照片路径> [次数，默认5]
 * 输出：每次请求的端到端耗时（RTT，含传输+服务端五段推理）
 *      及服务端分段耗时 decode/detect/landmarks/descriptor（V2.20新增）
 * 用途：对比 inputSize 224→160 等提速改动前后的每帧耗时；
 *      分段数据用于定位瓶颈在"解码传输"还是"模型推理"（NPU决策依据）
 */
'use strict';
const fs = require('fs');
const http = require('http');

const imgPath = process.argv[2];
const times = parseInt(process.argv[3] || '5', 10);
if (!imgPath || !fs.existsSync(imgPath)) {
    console.error('用法: node test_face_rtt.js <人脸照片路径> [次数]');
    process.exit(1);
}
const imgBase64 = fs.readFileSync(imgPath).toString('base64');

function post() {
    return new Promise((resolve) => {
        const t0 = Date.now();
        const req = http.request({
            host: '127.0.0.1', port: 8089, path: '/extract', method: 'POST',
            headers: { 'Content-Type': 'application/json' }
        }, (res) => {
            let d = '';
            res.on('data', c => d += c);
            res.on('end', () => {
                let r = {};
                try { r = JSON.parse(d); } catch (e) { r = { error: '解析失败' }; }
                resolve({ rtt: Date.now() - t0, ok: !!r.success, msg: r.error || '', timing: r.timing || null });
            });
        });
        req.on('error', (e) => resolve({ rtt: Date.now() - t0, ok: false, msg: e.message }));
        req.end(JSON.stringify({ image: imgBase64 }));
    });
}

(async () => {
    console.log(`实测 ${times} 次（照片: ${imgPath}）`);
    let sum = 0, okCnt = 0;
    const segSum = { decode: 0, detect: 0, landmarks: 0, descriptor: 0, total: 0 };
    let segCnt = 0;
    for (let i = 1; i <= times; i++) {
        const r = await post();
        sum += r.rtt;
        if (r.ok) okCnt++;
        console.log(`  第${i}次  RTT=${r.rtt}ms  成功=${r.ok}${r.msg ? '  ' + r.msg : ''}`);
        if (r.timing) {
            segCnt++;
            segSum.decode += r.timing.decode;
            segSum.detect += r.timing.detect;
            segSum.landmarks += r.timing.landmarks;
            segSum.descriptor += r.timing.descriptor;
            segSum.total += r.timing.total;
            console.log(`         [分段ms] decode=${r.timing.decode} detect=${r.timing.detect}`
                + ` landmarks=${r.timing.landmarks} descriptor=${r.timing.descriptor}`
                + ` 服务端total=${r.timing.total}`);
        }
    }
    console.log(`平均 RTT=${Math.round(sum / times)}ms  成功=${okCnt}/${times}`);
    if (segCnt > 0) {
        const n = segCnt;
        console.log(`平均分段(ms, n=${n}): decode=${Math.round(segSum.decode / n)}`
            + `  detect=${Math.round(segSum.detect / n)}`
            + `  landmarks=${Math.round(segSum.landmarks / n)}`
            + `  descriptor=${Math.round(segSum.descriptor / n)}`
            + `  服务端total=${Math.round(segSum.total / n)}`);
        console.log(`传输+序列化开销 ≈ RTT - 服务端total = ${Math.round(sum / times - segSum.total / n)}ms`);
    }
})();
