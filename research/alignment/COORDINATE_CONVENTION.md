# ThermalEyes 坐标系与变换约定规范 (Coordinate Convention Standard)

本文档正式确立并规范 **ThermalEyes** 系统中跨模态配准（TCCA）、C++ NDK 图像融合管线（`image_fusion.cpp`）与 Java 测温/交互图层（`ImageFusion.java`, `MainActivity.java`）的完整坐标转换链条。

---

## 一、硬件与图像基准空间定义

| 模态 / 传感器 | 原生分辨率 | 物理特性 | 处理基准空间 | 放大映射比例 |
| :--- | :--- | :--- | :--- | :--- |
| **RGB 可见光相机** | 640 × 480 | 标准光学透镜，正常朝向 | 640 × 480 ($W=640, H=480$) | 1.0 (基准坐标系) |
| **MLX90640 远红外** | 32 × 24 | 硬件阵列水平镜像 (Mirrored) | 640 × 480 ($W=640, H=480$) | $S_x = 20.0, S_y = 20.0$ |

### 笛卡尔坐标方向定义
- **原点 $(0, 0)$**：图像左上角 (Top-Left corner)。
- **$+X$ 方向**：水平向右 (Image Right)。
- **$+Y$ 方向**：垂直向下 (Image Down)。
- **图像中心点 $c$**：$c = (c_x, c_y) = (320.0, 240.0)$。

---

## 二、8 项关键坐标链验证与一致性结论

### 1. MLX90640 硬件水平镜像归一化 (Hardware Mirror Normalization)
- **物理机理**：MLX90640 传感器阵列读出的原始数据在水平轴向上与可见光摄像头成镜像相反关系。
- **Python** (`thermal_contour.py`):
  ```python
  therm = cv2.flip(thermal_raw, 1)
  ```
- **C++ NDK** (`image_fusion.cpp`):
  ```cpp
  flip(im_therm, im_therm_prep, 1);
  ```
- **Java 测温点** (`MainActivity.java`):
  ```java
  int mx = (ThermalDevice.IMAGE_WIDTH - 1) - (maxLoc % ThermalDevice.IMAGE_WIDTH);
  int my = maxLoc / ThermalDevice.IMAGE_WIDTH;
  thermInfo.maxLoc = new Point(mx, my);
  ```
- **一致性审计**：完全一致。三端均在原生 32×24 网格上首先进行水平镜像矫正，无二次翻转 (Double Mirror)。

---

### 2. 空间放大顺序 (Upscaling Order)
- **Python**：将 32×24 提取的轮廓点坐标乘以下采样系数比 $S_x = 640/32 = 20.0, S_y = 480/24 = 20.0$，直接映射到 640×480 空间。
- **C++ NDK**：
  ```cpp
  resize(im_therm_prep, im_therm_scale, Size(640, 480), 0, 0, INTER_LINEAR);
  ```
  先等比放大至 640×480，随后在 640×480 空间执行仿射变换。
- **Java**：
  ```java
  float x0 = pt.x * ((float) camW / thermW); // 20.0
  float y0 = pt.y * ((float) camH / thermH); // 20.0
  ```
- **一致性审计**：完全一致。统一在 640×480 空间定义后续变换。

---

### 3. 尺度缩放中心 (Scale Center Definition)
- **定义**：尺度缩放必须严格**围绕图像几何中心 $c = (320, 240)$** 进行，而不是以左上角 $(0, 0)$ 为原点。
- **数学表达式**：
  $$p_{\text{scaled}} = c + s \cdot (p - c) = \begin{bmatrix} 320 + s \cdot (x - 320) \\ 240 + s \cdot (y - 240) \end{bmatrix}$$
- **Python / C++ / Java**：三端一致采用中心缩放。

---

### 4. 旋转中心定义 (Rotation Center Definition)
- **定义**：旋转中心为中心点 $c = (320, 240)$。
- **TCCA 约定**：由于可见光模组与 MLX90640 采用精密 3D 打印外壳水平刚性卡槽固定，当前阶段两光轴平行且共面，旋转角度冻结为 $\theta = 0.0^\circ$。

---

### 5. 平移与缩放的执行顺序 (Order of Translation and Scaling)
- **变换模型**：
  $$p' = c + s \cdot (p - c) + \begin{bmatrix} dx \\ dy \end{bmatrix}$$
- **先后关系**：热像图/点集首先围绕图像中心 $c$ 进行缩放 $s$，**随后**直接叠加平移向量 $[dx, dy]^T$。
- **重要特征**：$dx, dy$ 不乘以缩放因子 $s$，直接代表 640×480 可见光全尺寸画面下的像素物理位移。

---

### 6. 水平位移 $dx$ 符号与物理意义 (dx Sign Convention)
- **定义**：
  $$dx > 0 \implies \text{热图像向右移动 (Move Right)}$$
  $$dx < 0 \implies \text{热图像向左移动 (Move Left)}$$
- **真机物理实测结论**：
  在 ThermalEyes 双目结构中，MLX90640 相较于可见光镜头需要补偿：
  $$dx = -23.0\text{ px}$$
  表示经过硬件镜像与中心缩放后，**热图需向左平移 23 个可见光像素**以抵消视差。
  *(历史代码中硬编码 $+25$ 导致了反向 $48\text{ px}$ 巨大错位)*。

---

### 7. 垂直位移 $dy$ 符号与物理意义 (dy Sign Convention)
- **定义**：
  $$dy > 0 \implies \text{热图像向下移动 (Move Down)}$$
  $$dy < 0 \implies \text{热图像向上移动 (Move Up)}$$
- **真机物理实测结论**：
  机械结构微小俯仰公差要求：
  $$dy = -10.0\text{ px}$$
  表示热图需**向上平移 10 个可见光像素**。

---

### 8. 测温坐标（MAX / MIN / CENTER）与热图的完全一致性
- **映射公式**：
  对任意原生热坐标 $(x_t, y_t) \in [0, 31] \times [0, 23]$：
  1. 镜像矫正：$x_m = 31 - x_t, \quad y_m = y_t$
  2. 放大至可见光尺寸：$x_0 = x_m \times 20.0, \quad y_0 = y_m \times 20.0$
  3. 仿射变换：
     $$x_{\text{aligned}} = 320.0 + s \cdot (x_0 - 320.0) + dx$$
     $$y_{\text{aligned}} = 240.0 + s \cdot (y_0 - 240.0) + dy$$
- **中心十字准星 (CENTER)**：
  原生中心 $x_t = 16, y_t = 12 \implies x_0 = 320.0, y_0 = 240.0$。
  中心映射公式退化为：
  $$x_{\text{center\_aligned}} = 320.0 + dx$$
  $$y_{\text{center\_aligned}} = 240.0 + dy$$
  与画面渲染热图的中心像素位置严格重合，杜绝准星偏离热区现象。
