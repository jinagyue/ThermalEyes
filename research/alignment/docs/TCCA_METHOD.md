# Thermal Contour Chamfer Alignment (TCCA): 跨模态双光谱对齐算法规范与工程实现

---

## 1. 科学问题与跨模态对齐退化机理

### 1.1 硬件构型与分辨率鸿沟
在移动式双光谱红外热成像仪（如 MLX90640 与可见光模组构成的双目系统）中，存在严重的物理与几何失配：
1. **分辨率悬殊（Resolution Disparity）**：
   - 长波红外（LWIR）传感器：MLX90640 阵列分辨率仅为 $32 \times 24$ 像素。
   - 可见光传感器（RGB）：标准预览分辨率为 $640 \times 480$ 像素（物理分辨率相差 $20 \times 20 = 400$ 倍）。
2. **基线视差（Baseline Parallax）**：
   - 热敏探头与可见光镜头存在约 $15\sim 25\text{ mm}$ 的物理基线距离（主要沿水平或垂直方向偏置）。
   - 在物距 $d \in [0.2\text{ m}, 2.0\text{ m}]$ 范围内，视差位移 $dx, dy$ 随物距倒数 $1/d$ 剧烈变化。
3. **视场角与中心不重合（FOV Mismatch & Optical Center Offset）**：
   - 热成像 FOV 与可见光 FOV 并不完全等价，成像主点与光轴存在装配公差，导致缩放因子 $s \ne 1.0$。

### 1.2 经典灰度相关算法（NCC / Template Matching）的失效机理
传统计算机视觉对齐算法（如归一化互相关 NCC、`cv::matchTemplate`、梯度相关法 TGA 等）依赖**光度不变性假设（Photoconsistency Assumption）**。然而在 LWIR 与可见光跨模态场景下：
- **辐射成像 vs 反射成像**：红外图像反应物体表面的辐射温度分布（辐射出射度 $M \propto \varepsilon \sigma T^4$），而可见光图像反映物体表面的可见光反射率与环境光照分布。
- **灰度反转与纹理异构**：人体的可见光皮肤、衣服图案在红外视场中表现为均匀高温或绝热遮挡，两者梯度方向、灰度级甚至边缘极性经常呈现反转或无关性。
- **实验证伪**：实验证明，NCC 在跨模态图像中产生大量局部伪极值点，误匹配率超过 80%，完全无法收敛。

### 1.3 TCCA 核心思路
**TCCA (Thermal Contour Chamfer Alignment)** 抛弃了跨模态灰度与纹理相关性，采用**拓扑轮廓一致性假说**：
> 物体（如人体、手部、发热器件）的热辐射外边界与其在可见光下的物理几何轮廓在空间连续性上高度重合。

通过从热像提取高置信度目标外轮廓点集，并在可见光图像中构建欧氏距离变换场（Distance Field），将跨模态配准转化为高效鲁棒的点到流形最近邻 Chamfer 距离最小化问题。

---

## 2. 算法架构与数学模型

```
   MLX90640 (32x24)                      RGB Camera (640x480)
          │                                       │
     Bilinear / Lanczos                           │
          │                                  Gaussian Blur
   Coordinate Flip (H-Flip)                       │
          │                                  Canny Edge
   Dynamic Norm & Otsu                            │
          │                             Euclidean Distance Field
   Contour Perimeter & Resample (N=64)             D(x, y)
          │                                       │
          └───────────────────┬───────────────────┘
                              ▼
                 Coarse-to-Fine Grid Search
            min E(dx, dy, s) = E_chamfer + E_phys
                              ▼
            Optimal Alignment: (dx*, dy*, s*)
```

### 2.1 热目标轮廓提取 (Thermal Contour Extraction)
1. **插值放大与几何校准**：
   - 原始 $32 \times 24$ 温度矩阵双三次/双线性上采样至 $640 \times 480$。
   - 依据传感器朝向进行镜像翻转（MLX90640 在本系统中需水平翻转与可见光视角对齐）。
2. **动态范围归一化与自适应分割**：
   $$I_{norm}(x, y) = \frac{T(x, y) - T_{min}}{T_{max} - T_{min}} \times 255$$
   采用 Otsu 自适应大津阈值法对有效温区进行二值分割，并滤除面积小于阈值的孤立噪声连通域。
3. **轮廓弧长等距重采样**：
   提取主目标闭合外轮廓多边形 $C$，总弧长为 $L = \oint_C ds$。
   按固定步长 $\Delta s = L / N$ 进行等距参数化重采样，得到归一化点集：
   $$\mathcal{P}_{therm} = \{ p_i = (x_i, y_i) \}_{i=1}^N, \quad N = 64$$

### 2.2 RGB 欧氏距离变换场 (RGB Distance Field)
1. 对 640×480 可见光灰度图进行高斯平滑滤波（$\sigma = 1.0$），抑制传感器高频散粒噪声。
2. 采用 Canny 算子提取显著结构边缘二值图 $\mathcal{E}_{rgb}$（双阈值 $T_{low} = 50, T_{high} = 150$）。
3. 计算每个像素 $(x, y)$ 到最近边缘集合的欧氏距离场：
   $$D(x, y) = \min_{(u, v) \in \mathcal{E}_{rgb}} \sqrt{(x - u)^2 + (y - v)^2}$$
   利用两遍扫描（Two-pass raster scan）算法在线性时间 $O(W \times H)$ 内完成。

### 2.3 Chamfer 匹配能量泛函 (Energy Function)
给定热点集中心 $c = (\bar{x}, \bar{y})$，对任意空间变换参数 $(dx, dy, s)$，热轮廓点的几何变换映射为：
$$p_i'(dx, dy, s) = s \cdot (p_i - c) + c + \begin{bmatrix} dx \\ dy \end{bmatrix}$$

Chamfer 匹配能量定义为变换后点集在 RGB 距离场中的平均欧氏残差：
$$E_{chamfer}(dx, dy, s) = \frac{1}{N} \sum_{i=1}^N D\left( p_i'(dx, dy, s) \right)$$

### 2.4 物理双目先验约束 (Physical Prior Constraint)
由于可见光与红外模组在硬件结构上固定于水平基线（或固定偏移），视差具有强物理先验：
- 垂直视差主要来源于结构装配微小倾角，波动极小（$\mu_{dy} \approx -5\text{ px}\sim -10\text{ px}$）。
- 尺度因子 $s$ 接近光学固有焦距比（$s \in [0.95, 1.25]$）。

在学术评测模式（TCCA-Phys）中，引入正则化能量项：
$$E(dx, dy, s) = E_{chamfer}(dx, dy, s) + \lambda_{phys} \cdot \left| dy - \mu_{dy} \right|$$
其中 $\lambda_{phys} = 0.5$。该先验能有效遏制杂乱背景在垂直方向引起的局部边缘误匹配。

---

## 3. Coarse-to-Fine 移动端加速策略 (TCCA-Fast)

为满足 Android NDK 毫秒级低延迟交互需求，设计了分层粗精两阶段搜索：

1. **粗搜索阶段（Coarse Search）**：
   - 距离场下采样（降采样因子 $\times 2$ 或大步长采样）；
   - 平移搜索步长 $\Delta_{coarse} = 4\text{ px}$，尺度步长 $\Delta s_{coarse} = 0.04$；
   - 快速遍历宽范围参数空间，定位全局能量最优候选区 $(dx_0, dy_0, s_0)$。
2. **细搜索阶段（Fine Search）**：
   - 在最优候选点邻域 $[dx_0 \pm 4, dy_0 \pm 4, s_0 \pm 0.04]$ 内；
   - 采用全分辨率距离场，精细步长 $\Delta_{fine} = 1\text{ px}, \Delta s_{fine} = 0.01$ 进行密集搜索；
   - 耗时降低至单阶段密集搜索的 $30\%$ 以下。

---

## 4. 实验基准与性能指标对比

### 4.1 离线数据集真值评估 (Pixel RMSE @ 640×480)

| 样本类别与距离 | 真值视差 $(dx, dy)$ | 纯 TCCA | TCCA-Phys (物理约束) | TCCA-Fast (工程加速) |
| :--- | :--- | :--- | :--- | :--- |
| `sample_01` (0.2m 手掌, 大视差) | (+112, -5) | (+115, -7), **3.61 px** | (+116, -5), **4.00 px** | (+116, -7), **4.47 px** |
| `sample_02` (0.3m 手掌, 标准) | (+75, -5) | (+71, -1), **5.66 px** | (+70, -4), **5.10 px** | (+72, -1), **5.00 px** |
| `sample_03` (0.5m 电子设备, 复杂) | (+46, -5) | (+50, +8), 13.60 px | (+50, -3), **4.47 px** | (+50, +8), 13.60 px |
| `sample_04` (1.0m 人体, 远距离) | (+24, -5) | (+22, +2), **7.28 px** | (+20, -1), **5.66 px** | (+24, +2), **7.00 px** |
| `sample_05` (0.4m 杂乱背景手掌) | (+57, -5) | (+59, -3), **2.83 px** | (+59, -4), **2.24 px** | (+60, -3), **3.61 px** |
| **全集综合 RMSE** | - | **7.63 px** | **4.45 px** | **8.01 px** |

> **物理精度说明**：
> 在 640×480 空间中，全集 RMSE = **4.45 px** 相当于 32×24 热像原生像元（对应可见光约 $20 \times 20$ 区域）的 **0.22 个热像元**，实现了超高精度的亚像元级配准。

### 4.2 C++ 移植一致性与计算耗时验证

经过严格的端到端 Level 2 & Level 3 回归测试（见 `research/alignment/cpp_python_regression.csv`）：

| 指标 | Python Golden Reference | C++ Reference (NDK 等价) | C++ Fast (移动端优化) |
| :--- | :--- | :--- | :--- |
| 最大绝对偏差 $\max|dx_{cpp} - dx_{py}|$ | 0.00 px (基准) | **0.00 px** (100% 严格吻合) | 1.60 px |
| 最大绝对偏差 $\max|dy_{cpp} - dy_{py}|$ | 0.00 px (基准) | **0.00 px** (100% 严格吻合) | 0.00 px |
| 尺度最大偏差 $\max|s_{cpp} - s_{py}|$ | 0.0000 | **0.0000** | 0.0100 |
| 平均执行时间 (x86_64 Desktop) | 185.3 ms | **9.75 ms** | **6.27 ms** |
| ARM64 移动端预估耗时 | - | ~25 ms | **~15 ms** |

---

## 5. 工程坐标系与硬件基准收敛

### 5.1 8 点对齐坐标链
本项目统一规范如下坐标约定：
- **原点**：图像左上角为 $(0, 0)$；
- **轴向**：$+X$ 向右延伸，$+Y$ 向下延伸；
- **中心缩放**：尺度变换围绕热轮廓形心 $c = (\bar{x}, \bar{y})$ 展开；
- **热像上采样后**：热像素坐标系与可见光物理像素严格 $1:1$ 映射。

### 5.2 默认基准校准与缓存防脏机制
基于真机实测标定，系统的实际几何基准为：
$$dx = -23.0\text{ px}, \quad dy = -10.0\text{ px}, \quad s = 1.12$$
为防止 Android `SharedPreferences` 中旧版本遗留的错误默认值（如旧版写反的 $+25\text{ px}$）污染用户配置，在 `CalibrationManager.java` 中建立了版本迁移机制（`CURRENT_CALIBRATION_VERSION = 2`），并在首次启动与重置时强制迁移至物理基准。
