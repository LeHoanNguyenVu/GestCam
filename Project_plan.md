# SOFTWARE REQUIREMENTS SPECIFICATION (SRS) & SPRINT PLAN
**Project Name:** GestCam (PC Desktop)  
**Document Version:** 1.1.0 (Optimized Architecture)  
**Target Platform:** Windows 10 / Windows 11 (x64)  
**Cost Model:** 100% Free & Open-Source (Zero API, Zero Cloud, Offline Native Execution)  
**Core Process License:** MIT License | **Driver DLL License:** GNU GPL v2.0 (IPC Isolated)

---

## 1. GIỚI THIỆU (INTRODUCTION)

### 1.1 Mục đích (Purpose)
Tài liệu đặc tả yêu cầu phần mềm và kế hoạch triển khai chi tiết cho dự án **GestCam** (Virtual Gesture Meme Camera). Ứng dụng hoạt động như một webcam ảo trên máy tính cá nhân, tự động nhận diện cử chỉ tay và khuôn mặt của người dùng từ webcam vật lý để chèn hình ảnh meme tương ứng che phủ khuôn mặt theo thời gian thực (real-time) trước khi xuất luồng video ra các phần mềm hội nghị trực tuyến (Google Meet, Zoom, Discord, Microsoft Teams).

### 1.2 Phạm vi hệ thống (System Scope)
* **Loại ứng dụng:** Native Desktop Client chạy ngầm trên Windows (x64).
* **Môi trường hoạt động:** Chạy cục bộ (offline hoàn toàn), không gửi dữ liệu ra bên ngoài, không tốn chi phí duy trì máy chủ hay API token.
* **Ràng buộc hiệu năng:**
  * **Tốc độ khung hình (Frame Rate):** Tối thiểu 30 FPS (mục tiêu 60 FPS tại độ phân giải 720p).
  * **Độ trễ toàn chu trình (End-to-End Latency):** Không quá 25 ms.
  * **Mức tiêu thụ bộ nhớ (RAM Footprint):** Dưới hoặc bằng 70 MB (đã bao gồm SwapChain D3D11, DirectML Runtime và Frame Buffers).
  * **Mức chiếm dụng CPU:** Dưới 5% trên vi xử lý tầm trung 4 nhân (nhờ tăng tốc phần cứng DirectML trên iGPU Intel/AMD).

---

## 2. KIẾN TRÚC HỆ THỐNG (SYSTEM ARCHITECTURE)

Hệ thống được thiết kế theo mô hình **Đa tiến trình cách ly (Multi-Process Isolation)** kết hợp **Bộ nhớ chia sẻ không khóa (Lock-Free Triple-Buffering Shared Memory)** nhằm đảm bảo an toàn tuyệt đối, chống crash trình duyệt hoặc ứng dụng hội nghị khi ứng dụng chính gặp sự cố.

```mermaid
flowchart TD
    subgraph CoreProcess ["TIẾN TRÌNH CORE (C++20 Native Client)"]
        T1["Luồng 1: Camera Capture (MF: NV12/YUY2/MJPEG)"]
        RB["SPSC Lock-Free Ring Buffer (Drop-frame)"]
        T2["Luồng 2: AI Inference (ONNX DirectML iGPU)"]
        T3["Luồng 3: Render (SIMD AVX2 Premultiplied Blend & Rotate)"]
        T4["Luồng 4: UI & Tray (Dear ImGui DX11 / Win32 Tray)"]

        T1 -->|Raw Camera Frames| RB
        RB -->|Latest Frame| T2
        T2 -->|Face Tilt, Bounding Box & Gestures| T3
        T4 -.->|User Settings & Meme Mapping| T3
    end

    subgraph IPC ["BỘ NHỚ CHIA SẺ IPC (CHỐNG CRASH & SANDBOX-AWARE)"]
        SM["Shared Memory (Local\\GestCam_SharedBuffer)
Triple Buffering + Atomic Ready Index
DACL: Everyone + ALL APPLICATION PACKAGES"]
    end

    subgraph Driver ["TIẾN TRÌNH CONSUMER (DIRECTSHOW FILTER DLL)"]
        VCD["DirectShow VirtualCam Filter (obs-virtualcam.dll)"]
        Fallback["Safe-Fallback Screen (Khi Core App tắt / Idle)"]
        VCD --- Fallback
    end

    subgraph Consumers ["ỨNG DỤNG ĐÍCH"]
        Meet["Google Meet / Zoom / Discord / MS Teams"]
    end

    T3 -->|Write Free Slot + Atomic Commit| SM
    SM -->|Read Ready Slot (Lock-Free)| VCD
    VCD -->|DirectShow Video Stream| Meet
```

---

## 3. YÊU CẦU CHỨC NĂNG (FUNCTIONAL REQUIREMENTS)

### 3.1 Module Thu nhận Video (FR-CAP: Video Capture)
* **FR-CAP-01:** Truy xuất webcam vật lý thông qua Microsoft Media Foundation (`IMFSourceReader`), loại bỏ hoàn toàn overhead so với OpenCV.
* **FR-CAP-02 (Hardware Compatibility):**
  * Hỗ trợ định dạng không nén `NV12` và `YUY2` cho camera hỗ trợ băng thông cao.
  * Bổ sung giải mã phần cứng/phần mềm `MJPEG` để đảm bảo đạt 720p @ 60 FPS trên các webcam chuẩn USB 2.0 (vốn bị nghẽn băng thông bus nếu truyền video thô không nén).
* **FR-CAP-03:** Sử dụng tập lệnh SIMD (AVX2/SSSE3) để chuyển đổi Color Space từ YUY2/NV12 sang RGB24.
* **FR-CAP-04:** Lưu trữ frame vào cấu trúc hàng đợi Single Producer Single Consumer (SPSC) Lock-Free Ring Buffer. Tự động drop frame cũ nếu luồng AI/Render xử lý không kịp, cam kết Zero-Latency.

### 3.2 Module AI & Phân loại Cử chỉ (FR-AI: Inference & Logic)
* **FR-AI-01 (Face Tracking & Head Rotation):**
  * Tích hợp mô hình UltraFace / MediaPipe BlazeFace lượng tử hóa **INT8**.
  * Trích xuất Bounding Box khuôn mặt kèm tọa độ 2 mắt (`left_eye`, `right_eye`).
  * Tính toán góc nghiêng đầu (Head Roll/Tilt): $\theta = \operatorname{atan2}(\Delta y, \Delta x)$ để hỗ trợ xoay meme nghiêng tự nhiên theo khuôn mặt.
  * Tần suất chạy: 15–20 FPS (xen kẽ nội suy tuyến tính sang 60 FPS để tối ưu tài nguyên).
* **FR-AI-02 (Two-Stage Hand Tracking):**
  * **Stage 1 (Palm Detector):** Quét phát hiện Bounding Box bàn tay và góc định hướng cổ tay.
  * **Stage 2 (Hand Landmark):** Crop vùng ROI ($224 \times 224$) đưa vào mô hình MediaPipe Hand Landmark INT8 xuất 21 điểm 3D.
  * **Temporal Caching:** Khi đã nhận diện được bàn tay ở frame trước, tái sử dụng ROI frame trước để chạy thẳng Stage 2, chỉ kích hoạt lại Stage 1 khi mất dấu tay (giảm 60% tải suy luận).
* **FR-AI-03 (Tăng tốc phần cứng DirectML):**
  * Sử dụng ONNX Runtime C++ với **DirectML Execution Provider**, offload toàn bộ tính toán tensor sang GPU tích hợp (Intel UHD Graphics / AMD Radeon Graphics), giữ mức chiếm dụng CPU $\le 5\%$.
* **FR-AI-04 (State Machine & Logic Cử chỉ):**
  * `G_NONE` (Mặc định): **Passthrough Mode** — xuất thẳng video camera gốc, không chèn meme.
  * `G_PRAY` (Chắp tay): Khoảng cách Euclidean giữa 2 cổ tay và các cặp đầu ngón tay nhỏ hơn ngưỡng định trước.
  * `G_HEART` (Bắn tim): Khoảng cách và góc tạo bởi ngón cái và ngón trỏ hai tay (hoặc ngón cái chéo ngón trỏ một tay) khớp mẫu tim.
  * `G_POINT` (Chỉ trỏ): Ngón trỏ duỗi $> 160^\circ$, 3 ngón còn lại gập $< 60^\circ$.
* **FR-AI-05 (Chống rung & Debounce):**
  * Kích hoạt meme khi cử chỉ ổn định tối thiểu 3 frame liên tiếp (~100 ms).
  * Bộ đếm duy trì (Hold State): Duy trì meme tối thiểu 0.5s sau khi buông tay để tránh giật hình khi tay bị che khuất thoáng qua.

### 3.3 Module Xử lý Đồ họa & Hòa trộn (FR-REN: Overlay & Smoothing)
* **FR-REN-01 (Asset Loading):** Nạp ảnh PNG RGBA 32-bit bằng `stb_image`, chuyển đổi trước sang định dạng **Premultiplied Alpha** lúc nạp vào RAM.
* **FR-REN-02 (Lọc nhiễu tọa độ):** Áp dụng **One Euro Filter (1€ Filter)** lên tọa độ Bounding Box và góc xoay $\theta$ để triệt tiêu rung giật (jitter) khi ngồi yên, đồng thời bám dính tức thì khi chuyển động nhanh.
* **FR-REN-03 (SIMD Premultiplied Alpha Blending):**
  * Hòa trộn điểm ảnh tối ưu hóa bằng AVX2 (`_mm256_maddubs_epi16`).
  * Sử dụng kỹ thuật xấp xỉ phép chia 255 bằng bitshift: `(x + 1 + (x >> 8)) >> 8` giúp tăng tốc độ render lên gấp 4 lần so với phép chia thông thường.
* **FR-REN-04 (Affine Transformation):** Xoay ảnh meme theo góc nghiêng đầu $\theta$ và co giãn vừa vặn Bounding Box mở rộng 15–20% để che trọn khuôn mặt.
* **FR-REN-05 (Transition Animation):** Hiệu ứng Fade-in / Fade-out mờ dần (150 ms) khi bật/tắt meme thông qua nội suy hệ số alpha.

### 3.4 Module Camera Ảo & Giao tiếp IPC (FR-CAM: Driver & IPC)
* **FR-CAM-01 (DirectShow VirtualCam Filter):**
  * Sử dụng base code từ OBS VirtualCam (`obs-virtualcam.dll`), đăng ký vào Windows Registry qua `regsvr32` khi cài đặt.
  * Tương thích hoàn toàn với Chrome, Edge, Zoom, Discord, Skype, Teams.
* **FR-CAM-02 (Lock-Free Triple-Buffering IPC):**
  * Tạo Shared Memory `Local\GestCam_SharedBuffer` gồm 3 slot bộ nhớ đệm (1 slot Core đang ghi, 1 slot Driver đang đọc, 1 slot trung gian).
  * Trỏ slot hoàn thành thông qua `std::atomic<uint32_t> ready_slot` — **loại bỏ hoàn toàn Named Mutex cross-process**, triệt tiêu nguy cơ deadlock, loại bỏ giật xé hình (tearing) và không tốn chi phí Context Switch.
* **FR-CAM-03 (Bảo mật Chrome Sandbox - DACL):**
  * Khởi tạo Shared Memory với chuỗi Security Descriptor SDDL: `D:(A;;GA;;;WD)(A;;GA;;;AC)` cấp quyền đọc cho nhóm `Everyone` và `ALL APPLICATION PACKAGES`.
  * Đảm bảo trình duyệt Chrome/Edge chạy ở chế độ Low Integrity Sandbox đọc được Shared Memory mà không bị chặn `ERROR_ACCESS_DENIED`.
* **FR-CAM-04 (Safe-Fallback Screen):**
  * Khi Core Process tắt hoặc chưa khởi chạy, VirtualCam DLL tự động xuất màn hình chờ tĩnh "GestCam - Standby", giữ luồng video của Google Meet không bị crash hoặc gián đoạn.

### 3.5 Module Cấu hình & Giao diện (FR-UI: Settings & Tray)
* **FR-UI-01:** Giao diện điều khiển viết bằng **Dear ImGui (DirectX 11 backend)** hoặc Win32 Native nhẹ, tối ưu RAM.
* **FR-UI-02:** Tính năng giao diện:
  * Lựa chọn webcam vật lý đầu vào và độ phân giải mong muốn.
  * Tùy chỉnh gán ảnh meme cho từng cử chỉ (`G_PRAY`, `G_HEART`, `G_POINT`).
  * Preview thời gian thực kèm nút Toggle bật/tắt hiệu ứng.
* **FR-UI-03 (Chạy ngầm & Idle Sleep):**
  * Nút đóng (X) thu nhỏ ứng dụng xuống khay hệ thống (System Tray).
  * Lắng nghe trạng thái mở/đóng luồng từ VirtualCam DLL: Tự động đưa luồng AI về chế độ Sleep khi không có ứng dụng nào đang gọi Virtual Camera (tiết kiệm 0% CPU lúc nhàn rỗi).
* **FR-UI-04:** Lưu trữ cấu hình người dùng vào file `config.json`.

---

## 4. YÊU CẦU PHI CHỨC NĂNG (NON-FUNCTIONAL REQUIREMENTS)

| Mã tiêu chuẩn | Tiêu chí kỹ thuật | Ngưỡng định lượng bắt buộc | Phương án đảm bảo |
| :--- | :--- | :--- | :--- |
| **NFR-LATENCY** | Độ trễ toàn pipeline | $\le 25\text{ ms}$ | MF Capture (4ms) + DirectML (10ms) + SIMD Render (1.5ms) + IPC (1ms) |
| **NFR-MEMORY** | Tổng dung lượng RAM | $\le 70\text{ MB}$ | Pre-allocated buffers, ImGui DX11 SwapChain nhẹ, giải phóng texture thừa |
| **NFR-CPU** | Mức sử dụng CPU | $\le 5\%$ (i5 Gen 10 4-core) | Đẩy toàn bộ suy luận AI sang iGPU qua DirectML; Lock-free IPC không Mutex |
| **NFR-STABILITY** | Độ ổn định (Crash-Free) | 0 crash / rò rỉ sau 8 giờ | Decoupled IPC: Core Process crash không kéo sập tab Google Meet / Zoom |
| **NFR-PACKAGE** | Kích thước bộ cài | $\le 30\text{ MB}$ | Inno Setup nén LZMA2 (Core binary + ONNX models INT8 + Driver DLL) |
| **NFR-COST** | Bản quyền & Chi phí | 0 VNĐ | 100% mã nguồn mở và thư viện miễn phí |

---

## 5. THIẾT KẾ CẤU TRÚC DỮ LIỆU BỘ NHỚ CHIA SẺ (LOCK-FREE IPC SPEC)

```cpp
#pragma pack(push, 1)

// Kích thước chuẩn: 1280 x 720 x 3 bytes (RGB24) = 2,764,800 bytes
constexpr uint32_t FRAME_WIDTH   = 1280;
constexpr uint32_t FRAME_HEIGHT  = 720;
constexpr uint32_t FRAME_CHANNELS = 3;
constexpr uint32_t FRAME_SIZE    = FRAME_WIDTH * FRAME_HEIGHT * FRAME_CHANNELS;

// Slot dữ liệu frame
struct SharedBufferSlot {
    uint64_t frame_index;
    uint64_t timestamp_us;
    uint8_t  pixel_data[FRAME_SIZE];
};

// Cấu trúc Shared Memory Triple-Buffering Lock-Free
struct SharedMemoryState {
    uint32_t magic;                    // 0x4D454D45 ("MEME")
    uint32_t version;                  // 0x00010001
    uint32_t width;                    // 1280
    uint32_t height;                   // 720
    uint32_t stride;                   // 1280 * 3 = 3840
    uint32_t format;                   // 0: RGB24, 1: NV12
    
    // Quản lý trạng thái client đang kết nối (để Core kích hoạt Idle Sleep)
    uint32_t active_readers;           // Số lượng app đang mở VirtualCam
    
    // Index của slot hoàn chỉnh mới nhất để Consumer đọc (0, 1, hoặc 2)
    // Core ghi xong slot nào thì Atomic Store index đó vào đây
    volatile uint32_t ready_slot_idx;  
    
    // Triple Buffer: Slot 0, 1, 2
    SharedBufferSlot slots[3];
};

#pragma pack(pop)
```

---

## 6. KẾ HOẠCH TRIỂN KHAI THEO SPRINT (ROADMAP OVERVIEW)

> [!TIP]
> Để tài liệu kiến trúc tổng thể gọn gàng và dễ dàng quản lý thực thi, toàn bộ thông số kỹ thuật chuyên sâu, checklist tác vụ và tiêu chí nghiệm thu (DoD) của 5 Sprint đã được tách riêng thành **5 file độc lập** nằm ngay tại thư mục gốc của dự án:

| Sprint | Tài liệu đặc tả chi tiết | Trọng tâm kỹ thuật | Tiêu chí nghiệm thu cốt lõi (DoD) |
| :--- | :--- | :--- | :--- |
| **Sprint 1** | 📄 [Sprint_1.md](file:///d:/GestCam/Sprint_1.md) | Media Foundation Capture + IPC Shared Memory + VirtualCam DLL | Google Meet stream được video camera thô, pass Chrome Sandbox |
| **Sprint 2** | 📄 [Sprint_2.md](file:///d:/GestCam/Sprint_2.md) | ONNX Runtime DirectML + UltraFace + Two-Stage Hands + Head Tilt | Inference AI $\le 12\text{ ms}$, CPU $\le 5\%$, bắt 3 cử chỉ |
| **Sprint 3** | 📄 [Sprint_3.md](file:///d:/GestCam/Sprint_3.md) | SIMD AVX2 Blend + Premultiplied Alpha + One Euro Filter + Rotate | Render $\le 1.5\text{ ms}$, meme xoay nghiêng theo đầu, triệt tiêu rung |
| **Sprint 4** | 📄 [Sprint_4.md](file:///d:/GestCam/Sprint_4.md) | Finite State Machine (Debounce, Hold, Passthrough) + Safe Fallback | Chuyển cảnh mượt mà, kill app không crash tab Google Meet |
| **Sprint 5** | 📄 [Sprint_5.md](file:///d:/GestCam/Sprint_5.md) | Dear ImGui (DX11) + System Tray + Idle Sleep + Inno Setup | Giao diện tiện ích, 0% CPU khi nhàn rỗi, bộ cài One-Click $\le 30\text{ MB}$ |

> [!IMPORTANT]
> **Chiến lược Kiểm thử Tự động (Continuous Automation):** Toàn bộ dự án tuân theo quy chuẩn tự động hóa 100% không cần manual test. Xem chi tiết kiến trúc test suites, cơ chế Mock Camera và kịch bản chạy 1-Click tại 📄 [AUTOMATION_TESTING.md](file:///d:/GestCam/AUTOMATION_TESTING.md).

---

## 7. BẢNG TỔNG HỢP DANH MỤC CÔNG NGHỆ & BẢN QUYỀN

| Thành phần | Công nghệ / Thư viện | Giấy phép (License) | Chi phí | Lưu ý kiến trúc |
| :--- | :--- | :--- | :--- | :--- |
| **Ngôn ngữ lập trình** | C++20 (MSVC Build Tools / Clang) | Tiêu chuẩn ISO | 0 VNĐ | Bật `/O2`, `/arch:AVX2` |
| **AI Inference Engine** | Microsoft ONNX Runtime + DirectML | MIT License | 0 VNĐ | Tăng tốc qua iGPU (Intel/AMD) |
| **Mô hình AI** | UltraFace + BlazePalm + MP Hands INT8 | Apache 2.0 | 0 VNĐ | Lượng tử hóa INT8 kích thước siêu nhẹ |
| **Video Capture** | Windows Media Foundation API | Win32 System API | 0 VNĐ | Hỗ trợ NV12, YUY2, MJPEG |
| **Virtual Cam Driver** | OBS Studio VirtualCam Filter DLL | GNU GPL v2.0 | 0 VNĐ | Cách ly IPC, mã nguồn DLL mở |
| **Core Process** | GestCam Native Core Client | MIT License | 0 VNĐ | Giao tiếp qua Shared Memory với Driver |
| **Đồ họa & UI** | `stb_image` + `Dear ImGui` (DirectX 11) | Public Domain / MIT | 0 VNĐ | Giao diện chiếm $< 20\text{ MB}$ RAM |
| **Bộ cài đặt** | Inno Setup 6 | Inno Setup License | 0 VNĐ | Tự động hóa đăng ký Driver DLL |