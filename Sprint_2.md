# SPRINT 2: AI INFERENCE ENGINE & NHẬN DIỆN CỬ CHỈ VỚI DIRECTML

**Sprint ID:** SPRINT-02  
**Thời lượng dự kiến:** 1 Tuần  
**Trọng tâm:** ONNX Runtime C++ Native, DirectML iGPU Execution Provider, UltraFace INT8, Two-Stage MediaPipe Hands INT8, Head Roll Angle Extraction.  
**Mục tiêu tối thượng:** Chạy suy luận nhận diện khuôn mặt và trích xuất cử chỉ tay 3D với tổng thời gian xử lý $\le 12\text{ ms/frame}$ và mức tiêu thụ CPU $\le 5\%$ trên vi xử lý tầm trung 4 nhân nhờ tăng tốc phần cứng iGPU.

---

## 1. TỔNG QUAN & DATA PIPELINE

Sprint 2 xây dựng bộ não AI của GestCam. Để đạt mức CPU $\le 5\%$ khi chạy đồng thời 2-3 mô hình mạng nơ-ron, bắt buộc phải sử dụng **DirectML** để chuyển tải tính toán sang iGPU (Intel UHD Graphics / AMD Radeon Graphics).

```mermaid
flowchart TD
    InFrame["Frame RGB24 (1280x720)"] --> Pre["Downsample & Normalize (Pre-allocated Buffer)"]
    Pre --> UF["UltraFace INT8 (DirectML)"]
    UF -->|Face Box & 2 Eye Points| Tilt["Tính góc nghiêng đầu: θ = atan2(dy, dx)"]
    
    UF --> CheckFace{"Có mặt người trong khung hình?"}
    CheckFace -->|Không| Skip["Bỏ qua xử lý tay (Tiết kiệm 100% tải tay)"]
    CheckFace -->|Có| CacheCheck{"ROI tay từ frame trước còn hiệu lực?"}
    
    CacheCheck -->|Không (Mất dấu)| Palm["Stage 1: BlazePalm Detector (DirectML)"]
    Palm --> Crop["Crop vùng ROI bàn tay (224x224)"]
    CacheCheck -->|Có (Bám dính)| Crop
    
    Crop --> HandLM["Stage 2: Hand Landmark INT8 (21 điểm 3D)"]
    HandLM --> GestCalc["Gesture Classifier: Tính góc & khoảng cách khớp"]
    GestCalc --> Out["Tọa độ Bounding Box, Góc nghiêng θ, Mã Cử chỉ thô"]
```

---

## 2. YÊU CẦU KỸ THUẬT CHI TIẾT

### 2.1 ONNX Runtime & DirectML Configuration
* Sử dụng thư viện **Microsoft.ML.OnnxRuntime.DirectML** bản Native C++ (x64).
* Cấu hình Session:
  * Kích hoạt DirectML Execution Provider: `OrtSessionOptionsAppendExecutionProvider_DML(session_options, 0)`.
  * Tắt Graph Optimization cấp độ động lúc inference, cố định đồ thị: `ORT_ENABLE_EXTENDED`.
  * Khóa thread pool: `session_options.SetIntraOpNumThreads(1)` vì tính toán đã chạy trên GPU.
* Tiền cấp phát bộ nhớ (Pre-allocated Tensors): Khởi tạo sẵn `Ort::MemoryInfo` và tensor input/output tĩnh tại bước khởi động, tuyệt đối không gọi `malloc` hay tái tạo tensor trong vòng lặp frame.

### 2.2 Mô hình Face Tracking & Góc nghiêng đầu (FR-AI-01)
* **Model:** UltraFace INT8 (kích thước input chuẩn $320 \times 240$ hoặc $640 \times 480$).
* Trích xuất: Bounding Box $[x_{min}, y_{min}, x_{max}, y_{max}]$, độ tin cậy $\ge 0.70$.
* Tọa độ mắt: Lấy 2 điểm mốc mắt trái $(x_L, y_L)$ và mắt phải $(x_R, y_R)$.
* Tính góc xoay đầu (Head Roll/Tilt):
  $$\theta = \operatorname{atan2}(y_R - y_L, x_R - x_L)$$
* Tần suất: Chạy mỗi 3 frame một lần (hoặc 20 FPS). Các frame xen kẽ áp dụng thuật toán nội suy chuyển động để duy trì 60 FPS mà không tốn tải GPU.

### 2.3 Two-Stage Hand Tracking & Temporal Caching (FR-AI-02)
* **Stage 1 (BlazePalm):** Phát hiện bàn tay toàn cảnh, xác định tâm bàn tay và hướng xoay cổ tay.
* **Stage 2 (Hand Landmark):** Nhận đầu vào là ảnh crop $224 \times 224$ xoay vuông góc theo cổ tay, xuất ra 21 điểm keypoints 3D $(x, y, z)$.
* **Temporal ROI Caching:**
  * Nếu frame $N-1$ bắt được tay với confidence $> 0.8$, frame $N$ sẽ dùng Bounding Box mở rộng 20% của frame $N-1$ làm ROI đưa thẳng vào Stage 2.
  * Chỉ khi confidence của Hand Landmark rớt xuống dưới $0.5$ mới kích hoạt lại Stage 1 (BlazePalm). Kỹ thuật này giúp giảm tới 60% tải tính toán tổng thể.

### 2.4 Thuật toán Phân loại Cử chỉ Hình học (FR-AI-03)
Dựa trên 21 điểm khớp tay 3D chuẩn hóa $[0.0, 1.0]$:
* **Góc uốn ngón tay:** Tính góc giữa 3 khớp nối: Khớp gốc (MCP) $\to$ Khớp giữa (PIP) $\to$ Khớp đầu ngón (DIP/TIP) bằng tích vô hướng vector:
  $$\cos(\alpha) = \frac{\vec{u} \cdot \vec{v}}{\|\vec{u}\| \|\vec{v}\|}$$
* **Cử chỉ `G_POINT` (Chỉ trỏ):**
  * Ngón trỏ duỗi: $\alpha_{\text{trỏ}} > 160^\circ$.
  * Ngón giữa, áp út, ngón út co: $\alpha < 60^\circ$.
* **Cử chỉ `G_PRAY` (Chắp tay):**
  * Yêu cầu phát hiện 2 bàn tay.
  * Khoảng cách giữa 2 cổ tay (Wrist 0) và khoảng cách giữa các cặp đầu ngón tay (Tip 4, 8, 12, 16, 20) $\le \epsilon$ (ngưỡng định chuẩn theo chiều rộng bàn tay).
* **Cử chỉ `G_HEART` (Bắn tim):**
  * Ngón cái và ngón trỏ bắt chéo hoặc khép cong tạo thành góc tim, các ngón còn lại gập sát lòng bàn tay.

---

## 3. DANH SÁCH CÁC TÁC VỤ (TASKS BREAKDOWN)

- [x] **Task 2.1: Tải & Chuẩn bị Models ONNX INT8**
  - Download pre-trained ONNX models: UltraFace-320, BlazePalm, MediaPipe Hand Landmark.
  - Sử dụng công cụ ONNX Quantization tool (`onnxruntime.quantization`) lượng tử hóa sang INT8.
  - Đảm bảo tổng dung lượng 3 file model cộng lại $\le 10\text{ MB}$. (Đã hoàn thành: 5.52 MB)
- [x] **Task 2.2: Xây dựng Module ONNX Runtime DirectML Wrapper**
  - Viết class `OnnxDirectMLEngine`: Quản lý `Ort::Env`, `Ort::Session`, `Ort::IoBinding`.
  - Thiết lập cơ chế cấp phát bộ nhớ tensor cố định. (Đã hoàn thành: Zero-Allocation Tensor Pipeline & 6/6 tests passed)
- [x] **Task 2.3: Hiện thực UltraFace Detector & Tính góc nghiêng đầu**
  - Viết tiền xử lý: Resize frame từ $1280 \times 720 \to 320 \times 240$, normalize `(pixel - 127.5) / 128.0`.
  - Chạy inference, parse bounding box qua Non-Maximum Suppression (NMS).
  - Trích xuất toạ độ mắt và tính góc $\theta$. (Đã hoàn thành: Latency 8.39ms, sai số góc <= 3 độ, 6/6 tests passed)
- [ ] **Task 2.4: Hiện thực Pipeline Bàn tay Two-Stage + Temporal Caching**
  - Cài đặt BlazePalm detector và hàm crop ROI $224 \times 224$ có xoay ma trận chuẩn hóa.
  - Cài đặt Hand Landmark inference trích xuất 21 điểm 3D.
  - Xây dựng logic State Tracker: Giữ ROI qua các frame, tự động re-detect khi confidence tụt.
- [ ] **Task 2.5: Xây dựng Bộ Phân loại Cử chỉ Hình học (Gesture Classifier)**
  - Viết hàm hình học tính góc uốn ngón tay từ 3 điểm 3D.
  - Viết bộ quy tắc logic nhận diện: `G_NONE`, `G_PRAY`, `G_HEART`, `G_POINT`.
- [ ] **Task 2.6: Benchmark & Tối ưu hiệu năng DirectML**
  - Viết test script đo thời gian từng công đoạn (Pre-process, Inference, Post-process).
  - Tối ưu bộ nhớ tensor để đảm bảo FPS $\ge 30$ và CPU $\le 5\%$.
- [ ] **Task 2.7: Xây dựng Automated Test Suite cho AI & Cử chỉ (Sprint 2)**
  - Tích hợp các ảnh mẫu tĩnh (`face_centered.png`, `gesture_pray.png`, `gesture_heart.png`, `gesture_point.png`).
  - Viết `tests/test_sprint2_ai.cpp`: Tự động kiểm thử độ chính xác Bounding box của UltraFace, sai số góc nghiêng đầu $\theta \le 3^\circ$, phân loại đúng 100% các mẫu cử chỉ tĩnh, và tự động đo độ trễ suy luận AI trên DirectML (Assert $\le 12.0\text{ ms}$).

---

## 4. INTERFACE CỐT LÕI (C++ SPECIFICATION)

### `include/AiEngine.h`
```cpp
#pragma once
#include <vector>
#include <string>
#include <memory>

enum class GestureType {
    NONE = 0,
    PRAY,       // Chắp tay
    HEART,      // Bắn tim
    POINT       // Chỉ trỏ
};

struct FaceDetectionResult {
    bool has_face = false;
    float xmin, ymin, xmax, ymax; // Chuẩn hóa [0.0, 1.0]
    float head_roll_angle;         // Góc radian (-PI đến +PI)
    float confidence;
};

struct HandJoint {
    float x, y, z;
};

struct HandDetectionResult {
    bool has_hand = false;
    HandJoint joints[21];
    float confidence;
};

struct AiFrameOutput {
    FaceDetectionResult face;
    HandDetectionResult hands[2]; // Tối đa 2 bàn tay
    GestureType detected_gesture = GestureType::NONE;
    float inference_time_ms = 0.0f;
};

class AiEngine {
public:
    virtual ~AiEngine() = default;
    virtual bool Initialize(const std::string& model_dir) = 0;
    virtual AiFrameOutput ProcessFrame(const uint8_t* rgb24_data, int width, int height) = 0;
};
```

---

## 5. TIÊU CHÍ NGHIỆM THU (DEFINITION OF DONE - DoD)

1. **Test Case 2.1 (DirectML Initialization):** Khởi tạo thành công ONNX Session trên iGPU DirectML, không có cảnh báo fallback sang CPU trong log.
2. **Test Case 2.2 (Latency & CPU Footprint):**
   * Thời gian chạy UltraFace: $\le 4.0\text{ ms}$.
   * Thời gian chạy Hand Landmark (Stage 2 với ROI cache): $\le 7.0\text{ ms}$.
   * Tổng thời gian inference AI: $\le 12.0\text{ ms/frame}$.
   * Mức chiếm dụng CPU khi AI chạy liên tục ở 30 FPS: $\le 5\%$ trên Intel Core i5 Gen 10.
3. **Test Case 2.3 (Head Roll Precision):** Khi người dùng nghiêng đầu sang trái/phải từ $-45^\circ$ đến $+45^\circ$, góc $\theta$ tính toán bám sát theo độ lệch mắt với sai số $\le 3^\circ$.
4. **Test Case 2.4 (Gesture Accuracy):** Nhận diện chính xác 3 cử chỉ mục tiêu (`G_PRAY`, `G_HEART`, `G_POINT`) với tỷ lệ nhận diện đúng (True Positive Rate) $\ge 90\%$ trong điều kiện ánh sáng phòng bình thường.
