"""
@file face-server-512.py
@brief 512 维人脸特征提取常驻服务，输出特征向量供上位机做身份比对
@author 袁燕

8089 服务负责人脸检测与定位，本服务负责输出 512 维特征向量，两者配合完成一次识别。
采用 InsightFace 的 buffalo_l 模型组：SCRFD 检测 + ArcFace（ResNet50）512 维识别。

接口：
  POST /extract  body: { "image": "<base64 JPEG>" }
    成功: { "success": true, "descriptor": [512 floats], "confidence": 0.9, "faceSize": 95 }
    失败: { "success": false, "error": "未检测到人脸" }
  GET  /health -> { "ready": true }

端口: 8090，由 8089 服务在启动时拉起，随 8089 退出而停止。
依赖：insightface / onnxruntime / opencv-python / numpy。
"""

import base64
import json
import os
import shutil
import sys

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import cv2
import numpy as np
from insightface.app import FaceAnalysis

# 人脸框边长下限(px)：低于此值视为距离过远，与 face-server.js 的 FACE_MIN_SIZE 保持一致。
FACE_MIN_SIZE = 70
# 检测输入尺寸：SCRFD 检测只负责定位，人脸校正后的识别质量与检测图尺寸无关，
# 仅影响小脸召回。近景登录人脸框(~95px@320图)充足，缩到 320 检测推理快 3~4 倍，
# 不改变最终 512 维特征的识别精度。
DETECT_SIZE = (320, 320)
# 多线程加速：onnxruntime/OpenMP 默认不充分利用多核，单帧推理约 1.2s；
# 由拉起方(face-server.js)注入 ORT_NUM_THREADS/OMP_NUM_THREADS 环境变量后降到 ~0.45s。
# 线程数统一用环境变量控制（insightface 会忽略 providers 里的 options 写法）。

# 512 维识别模型文件名：InsightFace 按输入输出尺寸把它识别为 recognition 模型。
RECOGNITION_MODEL_NAME = "w600k_r50.onnx"
# 特征向量维度，用于校验模型输出是否完整。
FEATURE_DIM = 512

# 全局模型句柄，服务启动时预加载，使 /health 能真实反映引擎是否可用。
_ANALYZER = None


def ensure_recognition_model():
    """把 512 维识别模型放到 InsightFace 的标准模型目录。

    @brief 标准目录缺少识别模型时，从工程自带模型或环境变量指定的路径复制一份。
    @return 标准目录下识别模型文件的绝对路径
    @throws RuntimeError 所有候选位置都找不到模型文件时抛出，说明应放置的路径
    """
    model_dir = os.path.join(os.path.expanduser("~"), ".insightface", "models", "buffalo_l")
    target = os.path.join(model_dir, RECOGNITION_MODEL_NAME)
    if os.path.isfile(target):
        return target

    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.environ.get("FACE_RECOGNIZE_MODEL", ""),
        os.path.join(script_dir, "..", "..", "..", "models", "recognize.onnx"),
        os.path.join(script_dir, "..", "..", "models", "recognize.onnx"),
        os.path.join(script_dir, "models", "recognize.onnx"),
    ]
    for candidate in candidates:
        if not candidate:
            continue
        source = os.path.abspath(candidate)
        if os.path.isfile(source):
            os.makedirs(model_dir, exist_ok=True)
            shutil.copy2(source, target)
            print("[face-server-512] 已补齐 512 维识别模型：%s" % target, flush=True)
            return target

    raise RuntimeError(
        "缺少 512 维识别模型，请把 %s 放到 %s，或设置环境变量 FACE_RECOGNIZE_MODEL 指向该文件"
        % (RECOGNITION_MODEL_NAME, model_dir)
    )


def get_analyzer():
    """加载并缓存 FaceAnalysis 模型组。

    @brief 补齐识别模型后加载模型组，并确认 512 维识别能力确实可用。
    @return 已完成 prepare 的 FaceAnalysis 对象
    @throws RuntimeError 识别模型缺失或未加载时抛出，说明如何补齐
    """
    global _ANALYZER
    if _ANALYZER is None:
        ensure_recognition_model()
        # providers 只指定 CPU 执行后端；线程数由环境变量 ORT_NUM_THREADS/OMP_NUM_THREADS 控制
        app = FaceAnalysis(name="buffalo_l", providers=["CPUExecutionProvider"])
        app.prepare(ctx_id=0, det_size=DETECT_SIZE)
        # 少了 recognition 模型时仍能检测到人脸，但提不出特征；此处直接报错，
        # 避免服务带着残缺模型启动后每帧都失败、健康检查却显示正常。
        if "recognition" not in app.models:
            raise RuntimeError(
                "512 维识别模型未加载，请确认 %s 是 ArcFace 模型（输入 112x112、输出 512 维）"
                % RECOGNITION_MODEL_NAME
            )
        _ANALYZER = app
    return _ANALYZER


def extract_feature(image_bytes):
    """从图像字节提取 512 维人脸特征。

    输入：image_bytes - JPEG/PNG 编码的图像原始字节
    返回：(success, descriptor_or_error, confidence, face_size)
          success=True 时 descriptor_or_error 为 512 维浮点列表；
          success=False 时 descriptor_or_error 为错误说明字符串。
    """
    img = cv2.imdecode(np.frombuffer(image_bytes, np.uint8), cv2.IMREAD_COLOR)
    if img is None:
        return False, "图像解码失败", 0.0, 0
    analyzer = get_analyzer()
    faces = analyzer.get(img)
    if not faces:
        return False, "未检测到人脸", 0.0, 0
    # 取最大人脸，与 face-api 单脸语义保持一致（避免多脸歧义）。
    face = max(faces, key=lambda f: (f.bbox[2] - f.bbox[0]) * (f.bbox[3] - f.bbox[1]))
    box = face.bbox
    face_size = int(min(box[2] - box[0], box[3] - box[1]))
    if face_size < FACE_MIN_SIZE:
        return False, "请靠近", float(face.det_score), face_size
    # 识别模型缺失时 face.embedding 为空，直接转换成数组会得到 0 维数组，
    # 后续运算抛出难以定位的异常；此处先判空，把原因写清楚。
    embedding = getattr(face, "embedding", None)
    if embedding is None:
        return False, "512 维识别模型未加载，无法提取人脸特征", 0.0, face_size
    emb = np.asarray(embedding, dtype=np.float32)
    if emb.ndim != 1 or emb.shape[0] != FEATURE_DIM:
        return False, "人脸特征维度异常：%s" % (emb.shape,), 0.0, face_size

    # L2 归一化：InsightFace 的 embedding 为 ArcFace 原始输出（未归一化），
    # 归一化后余弦相似度=内积，与客户端比对逻辑语义一致（阈值按 512 维分布标定）。
    norm = float(np.linalg.norm(emb))
    descriptor = (emb / norm).tolist() if norm > 1e-12 else [float(x) for x in emb]
    return True, descriptor, float(face.det_score), face_size


class _Handler(BaseHTTPRequestHandler):
    """HTTP 请求处理：仅实现 /health 与 /extract。"""

    def _send_json(self, obj, status=200):
        """把字典序列化为 JSON 响应并写回客户端。"""
        payload = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path == "/health":
            self._send_json({"ready": _ANALYZER is not None})
        else:
            self._send_json({"error": "Not Found"}, status=404)

    def do_POST(self):
        if self.path != "/extract":
            self._send_json({"error": "Not Found"}, status=404)
            return
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        try:
            data = json.loads(body)
        except json.JSONDecodeError:
            self._send_json({"success": False, "error": "请求体不是合法 JSON"}, status=400)
            return
        image_b64 = data.get("image")
        if not image_b64:
            self._send_json({"success": False, "error": "缺少 image 参数"})
            return
        try:
            image_bytes = base64.b64decode(image_b64)
        except base64.binascii.Error:
            self._send_json({"success": False, "error": "image 不是合法 base64"})
            return
        try:
            success, descriptor, confidence, face_size = extract_feature(image_bytes)
        except Exception as exc:  # 模型未就绪或推理异常，统一以失败响应，不崩服务
            self._send_json({"success": False, "error": "引擎异常: %s" % exc})
            return
        if success:
            self._send_json({
                "success": True,
                "descriptor": descriptor,
                "confidence": confidence,
                "faceSize": face_size,
            })
        else:
            self._send_json({"success": False, "error": descriptor, "faceSize": face_size})


def main():
    """启动 8090 端口常驻服务。

    @brief 先加载并校验模型组，全部就绪后才监听端口；模型不完整时直接退出，
           不占用端口，让拉起方能够重新开始一次干净的启动。
    """
    try:
        get_analyzer()
    except Exception as exc:
        print("[face-server-512] 模型加载失败：%s" % exc, flush=True)
        sys.exit(1)
    server = ThreadingHTTPServer(("127.0.0.1", 8090), _Handler)
    print("[face-server-512] 512 维引擎服务启动 @ http://127.0.0.1:8090（模型已预加载）", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
