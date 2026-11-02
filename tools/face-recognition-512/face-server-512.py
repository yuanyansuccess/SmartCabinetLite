"""
基于 InsightFace 的 512 维人脸特征提取常驻服务
==========================================
与工程现有 face-server.js（face-api.js，128 维）形成对比引擎。
本服务采用 InsightFace 的 buffalo_l 套餐：SCRFD 人脸检测 + ArcFace（ResNet50 主干）512 维识别，
正是 face-api.js 之外的另一主流路线，用于对比评测两套引擎在 AI 脸下的误识率/漏识率。

接口（与 face-server.js 同构，便于 demo 切换）：
  POST /extract  body: { "image": "<base64 JPEG>" }
    成功: { "success": true, "descriptor": [512 floats], "confidence": 0.9, "faceSize": 95 }
    失败: { "success": false, "error": "未检测到人脸" }
  GET  /health -> { "ready": true }

端口: 8090（避开 128 维引擎的 8089，两服务可并存）
依赖：insightface / onnxruntime / opencv-python / numpy（本机 Anaconda3.9.7 已具备）
"""

import base64
import json

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import cv2
import numpy as np
from insightface.app import FaceAnalysis

# 人脸框边长下限(px)：低于此值视为距离过远，与 face-server.js 的 FACE_MIN_SIZE 保持一致。
FACE_MIN_SIZE = 70
# 检测输入尺寸：SCRFD 检测只负责定位，对齐后识别质量与检测图尺寸无关，
# 仅影响小脸召回。近景登录人脸框(~95px@320图)充足，缩到 320 检测推理快 3~4 倍，
# 不改变最终 512 维特征的识别精度。
DETECT_SIZE = (320, 320)
# 多线程加速：onnxruntime/OpenMP 默认不充分利用多核，单帧推理约 1.2s；
# 由拉起方(face-server.js)注入 ORT_NUM_THREADS/OMP_NUM_THREADS 环境变量后降到 ~0.45s。
# 此处不再在 providers 里传 options（insightface 会忽略该写法），统一用环境变量控制。

# 全局模型句柄，首次请求时按需加载，避免服务启动期阻塞。
_ANALYZER = None


def get_analyzer():
    """懒加载并缓存 FaceAnalysis 模型实例。

    返回：已 prepare 的 FaceAnalysis 对象（SCRFD 检测 + ArcFace 512 维识别）。
    """
    global _ANALYZER
    if _ANALYZER is None:
        # providers 只指定 CPU 执行后端；线程数由环境变量 ORT_NUM_THREADS/OMP_NUM_THREADS 控制
        app = FaceAnalysis(name="buffalo_l", providers=["CPUExecutionProvider"])
        app.prepare(ctx_id=0, det_size=DETECT_SIZE)
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
    # L2 归一化：InsightFace 的 embedding 为 ArcFace 原始输出（未归一化），
    # 归一化后余弦相似度=内积，与客户端比对逻辑语义一致（阈值按 512 维分布标定）。
    emb = np.asarray(face.embedding, dtype=np.float32)
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
    """启动 8090 端口常驻服务。"""
    get_analyzer()  # 预加载模型，使 /health 立即可用（避免拉起方误判未就绪而重复拉起）
    server = ThreadingHTTPServer(("127.0.0.1", 8090), _Handler)
    print("[face-server-512] 512 维引擎服务启动 @ http://127.0.0.1:8090（模型已预加载）")
    server.serve_forever()


if __name__ == "__main__":
    main()
