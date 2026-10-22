/**
 * @file liveness.js
 * @brief 无感活体检测：基于连续多帧 68 关键点的微动态时序判定
 * @author 袁燕
 *
 * 背景：128 维人脸特征只回答"像不像库内某人"，回答不了"是不是活人"。
 *   一张清晰照片（打印或屏幕显示）检测、关键点、特征与真人几乎一致，
 *   可越过相似度阈值直接登录。本模块补上"是不是活人"这一维。
 *
 * 原理：活人即使刻意保持不动，也存在生理性微动态——
 *   ① 头部微颤：鼻尖/眼中心质心的帧间位移持续存在（亚像素级）；
 *   ② 不自主眨眼：EAR（眼睛纵横比）时序出现周期性下陷，无需用户配合。
 *   照片与屏幕画面帧间完全静止，两个判据均不成立。
 *
 * 输入为 face-api 每帧的 68 关键点（索引见协议注释：36-41 右眼、42-47 左眼、
 *   30 鼻尖、48-67 嘴部）。判据为纯几何运算，不引入任何新模型与新依赖。
 *
 * 常量与 Qt 端 src/common/Constants.h 的 SC::FACE_LIVENESS_* 保持一致，
 *   改一端必须同步另一端。
 */
'use strict';

// 时序窗口（毫秒）：统计最近这么多时间内收到的 landmarks 帧
const WINDOW_MS = 6000;
// 窗口内最少有效帧数：不足时状态为 collecting（继续采集，不判失败）
const MIN_FRAMES = 6;
// 头部微颤判定阈值（px，帧间位移 RMS）
const MICRO_MOTION_MIN = 0.12;
// 自然眨眼判定：EAR 低于窗口中位数的这个比例算一次下陷
const BLINK_EAR_DROP = 0.2;

/// 眼睛纵横比（EAR）：(|p37-p41| + |p38-p40|) / (2 * |p36-p39|)，取单眼 6 点
/// @param pts   68 关键点数组（每项 {x, y} 或 [x, y]）
/// @param from  眼区起点索引（右眼 36 / 左眼 42）
/// @returns EAR 值；点缺失或眼距为 0 时返回 null
function eyeAspectRatio(pts, from) {
    if (!pts || pts.length < from + 6) return null;
    const p = (i) => {
        const v = pts[i];
        return Array.isArray(v) ? { x: v[0], y: v[1] } : { x: v.x, y: v.y };
    };
    const p1 = p(from), p2 = p(from + 1), p3 = p(from + 2), p4 = p(from + 3), p5 = p(from + 4), p6 = p(from + 5);
    const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
    const eyeWidth = dist(p1, p4);            // 眼角间距（36-39）
    if (eyeWidth < 1e-6) return null;
    return (dist(p2, p6) + dist(p3, p5)) / (2 * eyeWidth);
}

/// 面部质心：鼻尖与双眼中心的均值（对单点噪声不敏感的整体位置参考）
/// @param pts 68 关键点数组
/// @returns {x, y}；点缺失时返回 null
function faceCentroid(pts) {
    if (!pts || pts.length < 48) return null;
    const p = (i) => {
        const v = pts[i];
        return Array.isArray(v) ? { x: v[0], y: v[1] } : { x: v.x, y: v.y };
    };
    let ex = 0, ey = 0;
    for (let i = 36; i <= 47; i++) { ex += p(i).x; ey += p(i).y; }
    ex /= 12; ey /= 12;
    const nose = p(30);
    return { x: (ex + nose.x) / 2, y: (ey + nose.y) / 2 };
}

/// 窗口内自然眨眼次数：EAR 序列低于中位数一定比例后回升，计一次下陷事件
/// @param earSeq 按时间排列的 EAR 序列（已过滤 null）
/// @returns 眨眼次数
function countBlinks(earSeq) {
    if (earSeq.length < 3) return 0;
    const sorted = [...earSeq].sort((a, b) => a - b);
    const median = sorted[Math.floor(sorted.length / 2)];
    const threshold = median * (1 - BLINK_EAR_DROP);
    let blinks = 0;
    let inDrop = false;
    for (const ear of earSeq) {
        if (!inDrop && ear < threshold) {
            inDrop = true;
        } else if (inDrop && ear >= threshold) {
            inDrop = false;
            blinks++;
        }
    }
    return blinks;
}

/**
 * @brief 判定当前时序窗口的活体状态
 * @param history landmarks 帧历史，每项 { t: 毫秒时间戳, pts: 68关键点 }
 *                 由调用方维护（只放窗口内的帧），本函数不做时间裁剪
 * @returns { state: 'collecting'|'passed'|'failed', frames, microMotion, blinks }
 *          collecting=窗口帧数不足（继续采集，不判失败）
 *          passed=微颤达标（活体）；failed=帧间静止（照片/静态画面）
 *          microMotion=帧间质心位移 RMS(px)；blinks=窗口内眨眼次数
 * @note  微颤判据只取最近 MIN_FRAMES 帧：防止"真人先攒窗口、再换照片上台"
 *        的掺杂攻击——攻击者换照片后，最近帧必然全静态，立即判 failed。
 *        判定用"过半帧位移达标"而非纯 RMS：RMS 会被个别大位移帧拉高，
 *        而活人微颤是持续的（几乎每帧都有小位移），照片是恒零的。
 */
function evaluateLiveness(history) {
    if (!Array.isArray(history) || history.length < MIN_FRAMES) {
        return { state: 'collecting', frames: history ? history.length : 0, microMotion: 0, blinks: 0 };
    }

    // 微颤只看最近 MIN_FRAMES 帧（抗掺杂攻击）
    const recent = history.slice(-MIN_FRAMES);
    const shifts = [];
    for (let i = 1; i < recent.length; i++) {
        const a = faceCentroid(recent[i - 1].pts);
        const b = faceCentroid(recent[i].pts);
        if (!a || !b) continue;
        shifts.push(Math.hypot(b.x - a.x, b.y - a.y));
    }
    // 有效位移样本不足（关键点缺失）：维持 collecting，不误判
    if (shifts.length < MIN_FRAMES - 1) {
        return { state: 'collecting', frames: history.length, microMotion: 0, blinks: 0 };
    }

    // 达标帧计数：帧间位移超过阈值一半即记一帧（照片的量化噪声约 0.01px，远低于此）
    const activeFrames = shifts.filter(d => d >= MICRO_MOTION_MIN / 2).length;
    const rms = Math.sqrt(shifts.reduce((s, d) => s + d * d, 0) / shifts.length);

    // EAR 序列与眨眼计数用全窗口（眨眼周期 2~10 秒，窗口长些更易捕获；
    // 眨眼仅作佐证，不作为单独否决项）
    const earSeq = [];
    for (const f of history) {
        const l = eyeAspectRatio(f.pts, 42);
        const r = eyeAspectRatio(f.pts, 36);
        if (l !== null && r !== null) earSeq.push((l + r) / 2);
    }
    const blinks = countBlinks(earSeq);

    const passed = activeFrames * 2 >= MIN_FRAMES;   // 过半帧有位移
    return {
        state: passed ? 'passed' : 'failed',
        frames: history.length,
        microMotion: Math.round(rms * 1000) / 1000,
        blinks
    };
}

module.exports = { evaluateLiveness, eyeAspectRatio, faceCentroid, countBlinks, WINDOW_MS, MIN_FRAMES, MICRO_MOTION_MIN };
