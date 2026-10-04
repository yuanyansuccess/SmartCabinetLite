/**
 * @file prune-node-modules.js
 * @brief 裁剪人脸识别服务依赖体积，并恢复 node_modules 内的定制兼容层
 * @author 袁燕
 *
 * 背景：face-api + tfjs 的 npm 包体积达 292MB，其中 sourcemap 等
 * 非运行时文件占约 195MB，交付到柜机纯属浪费。
 *
 * 功能：
 *   1. 删除 node_modules 下运行期不加载的文件（sourcemap / 演示页 / 构建缓存）
 *   2. 写入 @tensorflow/tfjs-node 兼容层（node_modules 被 git 忽略，重建依赖后会丢失）
 *
 * 用法：node prune-node-modules.js
 * 返回：进程退出码 0 表示成功，非 0 表示失败
 */
'use strict';

const fs = require('fs');
const path = require('path');

const NODE_MODULES_DIR = path.join(__dirname, 'node_modules');

// 运行期不加载的扩展名：sourcemap 仅在浏览器开发者工具中按需拉取，
// tsbuildinfo 是 TypeScript 增量编译缓存，服务端推理均不涉及。
const PRUNE_EXTENSIONS = ['.map', '.tsbuildinfo'];

// 浏览器演示页，服务端不提供静态页面服务
const PRUNE_HTML = true;

const SHIM_SOURCE = path.join(__dirname, 'shims', 'tfjs-node-index.js');
const SHIM_TARGET = path.join(NODE_MODULES_DIR, '@tensorflow', 'tfjs-node', 'index.js');
const SHIM_MARKER = "@tensorflow/tfjs'";

/**
 * 递归统计目录体积
 * @param {string} dir 目录绝对路径
 * @return {number} 字节数
 */
function directorySize(dir) {
    let total = 0;
    const stack = [dir];
    while (stack.length > 0) {
        const current = stack.pop();
        let entries;
        try {
            entries = fs.readdirSync(current, { withFileTypes: true });
        } catch (err) {
            continue;
        }
        for (const entry of entries) {
            const full = path.join(current, entry.name);
            if (entry.isDirectory()) {
                stack.push(full);
            } else if (entry.isFile()) {
                try {
                    total += fs.statSync(full).size;
                } catch (err) {
                    // 文件已被占用或已删除，忽略
                }
            }
        }
    }
    return total;
}

/**
 * 递归删除指定扩展名的文件
 * @param {string} dir 目录绝对路径
 * @param {string[]} extensions 需要删除的扩展名列表
 * @return {{count: number, bytes: number}} 删除文件数与释放字节数
 */
function removeByExtension(dir, extensions) {
    let count = 0;
    let bytes = 0;
    const stack = [dir];
    while (stack.length > 0) {
        const current = stack.pop();
        let entries;
        try {
            entries = fs.readdirSync(current, { withFileTypes: true });
        } catch (err) {
            continue;
        }
        for (const entry of entries) {
            const full = path.join(current, entry.name);
            if (entry.isDirectory()) {
                stack.push(full);
                continue;
            }
            const isTarget = extensions.some(ext => entry.name.endsWith(ext))
                || (PRUNE_HTML && entry.name.endsWith('.html'));
            if (!isTarget) {
                continue;
            }
            try {
                bytes += fs.statSync(full).size;
                fs.unlinkSync(full);
                count++;
            } catch (err) {
                // 文件被占用时跳过，不影响整体裁剪
            }
        }
    }
    return { count, bytes };
}

/**
 * 恢复 tfjs-node 兼容层
 * node_modules 被版本控制忽略，重新安装依赖后原生 tfjs-node 会被还原，
 * 导致服务在 Node.js v22 上因 tfjs_binding.node 加载失败而启动不了。
 * @return {boolean} true=兼容层已就位
 */
function ensureTfjsNodeShim() {
    if (!fs.existsSync(SHIM_SOURCE)) {
        console.error('[FAIL] 兼容层源文件缺失:', SHIM_SOURCE);
        return false;
    }

    const shimContent = fs.readFileSync(SHIM_SOURCE, 'utf8');

    if (fs.existsSync(SHIM_TARGET)) {
        const current = fs.readFileSync(SHIM_TARGET, 'utf8');
        if (current.includes(SHIM_MARKER)) {
            console.log('[OK] tfjs-node 兼容层已存在，无需处理');
            return true;
        }
    }

    fs.mkdirSync(path.dirname(SHIM_TARGET), { recursive: true });
    fs.writeFileSync(SHIM_TARGET, shimContent, 'utf8');
    console.log('[OK] 已写入 tfjs-node 兼容层:', SHIM_TARGET);
    return true;
}

function main() {
    if (!fs.existsSync(NODE_MODULES_DIR)) {
        console.error('[FAIL] 未找到依赖目录，请先执行 npm install:', NODE_MODULES_DIR);
        process.exit(1);
    }

    const before = directorySize(NODE_MODULES_DIR);
    const removed = removeByExtension(NODE_MODULES_DIR, PRUNE_EXTENSIONS);
    const after = directorySize(NODE_MODULES_DIR);

    const mb = bytes => (bytes / 1024 / 1024).toFixed(1);
    console.log('[裁剪] 删除文件 %d 个，释放 %s MB', removed.count, mb(removed.bytes));
    console.log('[体积] %s MB -> %s MB', mb(before), mb(after));

    if (!ensureTfjsNodeShim()) {
        process.exit(1);
    }

    console.log('[完成] 依赖裁剪结束');
}

main();
