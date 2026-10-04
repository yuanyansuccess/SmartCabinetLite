/**
 * @file tfjs-node-index.js
 * @brief tfjs-node 兼容层源码 — 由 prune-node-modules.js 写入 node_modules
 * @author 袁燕
 *
 * face-api 的 Node 版本在加载时硬编码 require('@tensorflow/tfjs-node')，
 * 而原生 tfjs-node 依赖 tfjs_binding.node，在 Node.js v22 上加载失败。
 * 本文件以纯 JS 实现顶替该模块，使 face-api 正常加载。
 *
 * 注意：不可改为直接 require('@tensorflow/tfjs-core')，tfjs-core 的 CJS
 * 产物未包含链式 API（as3D/reshape 等），face-api 内部依赖链式调用，会报
 * "xxx is not a function"。链式 API 由 tfjs 聚合包注册，故此处必须用聚合包。
 *
 * 该文件需纳入版本控制：node_modules 被 .gitignore 忽略，
 * 重建依赖时由 prune-node-modules.js 自动写回，避免兼容层丢失。
 */
'use strict';

module.exports = require('@tensorflow/tfjs');
