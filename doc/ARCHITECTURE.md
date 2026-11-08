# 渲染实现与扩展

本文说明当前代码的约定，供移植渲染、准备贴图或修改材质时使用。接入步骤与 API 示例见[README](../README.md)。以下机制不依赖角色名，但仍依赖输入贴图、UV、法线和灯光质量。

## 数据路径

```text
宿主的网格 / 材质槽数据 / 相机 / 可选面部图
    ↓ UHonkaiRenderInstance::Bind
表面动态材质 + 可选描边动态材质 + Toon Ramp 图集
    ↓
基础通道：颜色、遮罩、法线、色带索引 / 面部自发光
    ↓
相机后处理 M_CelLighting：主体筛选、多色 Ramp、双层高光、接触阴影、窄边缘光
    ↓
宿主的曝光 / 色调映射 / 辉光 / 抗锯齿与最终输出
```

`FHonkaiRenderOptions::bBaseColorOnly` 可用于基础贴图对照：不添加两段照明后处理、不启用面部阈值着色，隐藏描边并关闭球面图强度。正常卡通效果保持该值为默认 `false`。

`Bind` 先准备新对象和资源，失败时保留当前主体。成功后保存并替换指定网格材质，把主体写入 Custom Depth/Stencil，并给指定相机添加自己的后处理实例。`Detach` 只恢复由该实例接管的状态。

源文件入口：

| 文件 | 职责 |
|---|---|
| [HonkaiRenderInstance.h](../Source/HonkaiToonRenderer/Public/HonkaiRenderInstance.h) | 输入结构、绑定和运行时更新 API |
| [HonkaiRenderInstance.cpp](../Source/HonkaiToonRenderer/Private/HonkaiRenderInstance.cpp) | 校验、纹理准备、Ramp 图集、动态材质、状态恢复 |
| [build_materials.py](../Scripts/build_materials.py) | 在 UE 编辑器中生成材质图并嵌入 HLSL |
| [HonkaiToon.hlsl](../Shaders/Material/HonkaiToon.hlsl) | 身体两段照明、短距离面部接触阴影、窄边缘光 |
| [HonkaiFace.hlsl](../Shaders/Material/HonkaiFace.hlsl) | 面部光角阈值、亮暗色与鼻部阴影 |
| [HonkaiColor.hlsl](../Shaders/Material/HonkaiColor.hlsl) | 面部与身体共用的基础色、色带及亮暗色处理 |
| [HonkaiUpscaling.cpp](../Source/HonkaiToonRenderer/Private/HonkaiUpscaling.cpp) | 可选 NVIDIA 放大的支持检查、启用和恢复 |

## 身体色带与高光

普通不透明/遮罩槽使用 Default Lit 材质，粗糙度为 1、镜面反射为 0。材质把基础贴图、Diffuse Tint 和可选球面图合成，保留原始颜色信息。球面图模式约定为 `0` 关闭、`1` 相乘、`2` 相加；插件不读取模型文件来推断这些值。

后处理只对 stencil 为 `1`、且 Custom Depth 与当前可见深度相符的像素生效，避免把角色后的场景一起处理。其照明估计将去除预曝光后的场景亮度除以 GBuffer 基础色亮度，将展示灯光范围映射到色带坐标，连续采样作者图中的中间颜色。缺图时使用暗、中过渡、亮三色回退。

这不是全新的 UE Shading Model。该照明估计依赖中性主光与非金属表面，复杂彩色灯、GI、反射或其他后处理会改变结果。因此专用展示相机可启用 `bApplyViewPreset`：手动曝光、关闭曲线型色调扩展、设置辉光，并关闭该相机的场景 AO/GI/反射覆盖。已有游戏应自行决定如何满足这些假设，不能直接把它当作任意灯光环境的材质替换。

亮暗色来自运行时 256×256 Ramp 图集：

- 第 0 行为通用回退，最多再容纳 255 个独立的 Ramp、高光颜色和指数组合；超出时明确拒绝绑定，保留当前主体。
- 源 Ramp 按**竖向、顶部亮、底部暗**读取中间一列，重排成横向亮暗索引。
- 当前 CPU 读取要求 `PF_B8G8R8A8` 和可访问的 mip 数据；不支持的 Ramp 使用回退，不修改源纹理。
- 材质将图集行号写入 Material AO 字节。因此该字节不能同时承担普通环境遮蔽，宿主必须关闭静态光照并满足 README 的设置。

身体暗色有通用对比/色度处理；近中性色不因微小 RGB 差异被放大成暖色条带。高亮浅色、亮部辉光种子和深度轮廓边缘光也由同一后处理处理。这些规则不能替代作者专门绘制的 LightMap、皮肤色或衣料阴影贴图。

`SpecularColor` 和 `Shininess` 由宿主提供；缺失颜色默认黑色，不凭材质名添加高光。身体从同一行索引的小型线性浮点参数图读取它们，面部用对应材质参数，两者调用同一高光函数。宽、窄两层由世界空间法线、主光与观察方向计算，并受明暗和背光抑制；指数计算限制在稳定范围。高光在最终配色后添加，不参与 Scene/Diffuse 的照明估计。它是通用风格化实现，不是参考程序完整 shader 的复刻，也不补出缺失的作者高光遮罩。

## 面部阈值图与缺图情况

面部用专用阈值图控制大片明暗，避免直接放大鼻、唇、脸颊几何法线造成的写实碎影。它常被称为 FaceMap/SDF，但此处接口需要的是**已经适配 UV 的角度阈值数据**，并不对任意 SDF 文件提供自动格式识别。

`FHonkaiFaceMaps` 的键是表面材质槽索引，值是 `UTexture2D`。图必须 `SRGB=false`、X/Y 地址模式均为 Clamp，尺寸与资源有效。使用下列通道：

| 通道 | 当前含义 |
|---|---|
| R / G | 左右相反光向下的归一化水平光角阈值；按光在局部 X 方向的符号选通道 |
| B | 面部描边的颜色/宽度控制；不是基础色或普通 Alpha |
| A | 鼻部阴影控制，较低值表示鼻影区域，较高值抑制鼻影；不代表表面透明度 |

材质把 `SetLighting` 的面部世界光向转成网格局部坐标，以水平 XY 光向计算相对局部 +Y 的归一化角度，并与 R/G 阈值比较。阴影保持两段，边缘使用屏幕导数做过滤。若模型正前方不是该基底，宿主应转换/补偿光向或准备符合约定的数据，不能只把另一个工具输出的贴图任意塞入。

面部着色通过自发光输出，普通身体照明通道关闭；面部与身体读取同一 Toon Ramp 图集和行索引（包括缺图回退），调用同一颜色处理函数。增强图只改变面部明暗分区与鼻部阴影，不再额外套用面部专属提亮和色调。后处理还进行短距离屏幕空间接触检查，让较近的非面部表面投下局部阴影。它看不到视口外遮挡物，也不能代替完整几何投影阴影。

**没有配套图时**：

1. 可以传空 `FHonkaiFaceMaps`，使用普通表面路径完成基础卡通展示，不需要伪造一张“万能 SDF”。
2. 希望获得面部专用阴影时，由宿主或美术工具根据当前模型 UV 与期望光向生成/绘制阈值数据，并按上表打包。这个插件只消费并校验数据，不包含生成器、UV 分析或模型识别。
3. 缺失的官方 LightMap、Ramp 或 FaceMap 无法仅靠开启一个参数恢复。插件没有声称这些辅助数据等同官方资产。

运行中 `PrepareFaceMaps` 只验证、准备纹理；`SetFaceMaps` 才切换效果。传空图会关闭面部阈值着色，描边回到该槽**绑定时**的基线：如果绑定时已有面部描边，基线仍可能是该版本，而不是丢失原控制。无效图不会替换当前映射。

## 描边、透明和可见性

描边使用宿主提供的第二个网格，将法线转到视空间，在视平面沿法线 XY 外扩，再转回世界位移，只保留背面。宽度以距相机 3 m 时的 UE 世界单位输入，透视相机按深度平方根补偿，倍率限在 0.5–4；正交相机不做距离补偿。近零 XY 法线安全收敛到零。它仍受法线质量和视场角影响，不是恒定像素宽度；不假设导入网格的顶点 RGB 已存有描边权重。

`EHonkaiOutlineRegion` 的 Default/Skin/Hair 对作者提供的描边颜色使用不同处理。面部版本还结合基础色、原始 Tint 和 B 通道，宽度以原始 `OutlineWidth` 的 75% 为起点并进一步受 B 控制。因此不能假定所有区域都输出相同纯黑色。宿主负责决定槽分区和作者描边输入，插件不按角色名分支。

透明度为基础贴图 Alpha × `Tint.A` × 顶点 Alpha。遮罩槽使用裁切阈值 0.1；半透明槽走 Unlit 颜色路径，因为其没有所需 GBuffer。半透明槽的描边透明度设为 0，避免透明覆盖层生成实心轮廓。逐部件显隐可由宿主使用顶点 Alpha 实现；插件不重写顶点位置或识别“需要隐藏的部件”。

## 材质资产与重建

仓库包含以下运行时资产，不依赖宿主的 `/Game` 内容：

- 四个表面父材质：`M_Toon`、`M_ToonTwoSided`、`M_ToonTranslucent`、`M_ToonTranslucentTwoSided`。
- 两个描边父材质：`M_Outline`、`M_FaceOutline`。
- 一个相机后处理：`M_CelLighting`，位置为 Scene Color Before DOF。
- 一个线性采样白色纹理：`T_LinearWhite`，其引擎来源见[第三方声明](../THIRD_PARTY_NOTICES.md)。

它们位于挂载路径 `/HonkaiToonRenderer/Materials`。HLSL 文本由脚本读入材质 Custom Expression，因此**只编辑 `.hlsl` 不会自动改写已有 `.uasset`**。修改着色器或材质图逻辑后，需在启用本插件、Python Editor Script Plugin 和 Editor Scripting Utilities 的编辑器宿主中运行：

```python
import runpy
from pathlib import Path
import unreal

base = unreal.PluginBlueprintLibrary.get_plugin_base_dir("HonkaiToonRenderer")
# 部分 UE Python 接口返回 (success, path)。
if isinstance(base, tuple):
    assert base[0]
    base = base[-1]
runpy.run_path(str(Path(base) / "Scripts" / "build_materials.py"), run_name="__main__")
```

脚本会重建并保存资产，随后调用 `verify_materials.py` 检查 Alpha 连接、骨骼/形变用途、嵌入代码和包依赖。只执行验证时，将上例的文件名改为 `verify_materials.py`。修改前提交或备份资产；生成脚本会重建其管理的材质图。Python 仅在编辑器制作阶段使用，不随运行时启动执行。

## 可选抗锯齿与放大

`FHonkaiUpscaling` 与某个模型的渲染实例无关，由宿主在应用级持有一个所有者，并在游戏线程、PostEngineInit 之后调用。用户界面和是否启用的选择由宿主提供。

```cpp
#include "HonkaiUpscaling.h"

// 作为宿主类成员持有，不能复制；不要每帧创建临时所有者。
FHonkaiUpscaling Upscaling;

FString Error;
if (!Upscaling.SetMode(EHonkaiUpscalingMode::DLSSQuality, OutputSizeInPixels, Error))
{
    UE_LOG(LogTemp, Warning, TEXT("Upscaling: %s"), *Error);
}
// 需要原生分辨率抗锯齿时，使用 EHonkaiUpscalingMode::DLAA。
// 关闭或在 EndPlay / 销毁视口前：
Upscaling.Restore();
```

`TSR` 枚举值表示移除本接口的临时覆盖、恢复宿主原有设置；它并非无条件强制把宿主切成 TSR。NVIDIA 启用前会检查 SDK、硬件、输出尺寸和设置所有权；失败时保留/恢复原设置。最小有效输出尺寸为 32×32，最小化或尚未准备好的窗口应由宿主延迟重试。

构建时仅在宿主 `Plugins/NVIDIA/DLSS` 与 `Plugins/NVIDIA/StreamlineNGXCommon` 同时存在官方插件描述文件且目标为 Win64 时启用适配器。基础渲染与公开头文件没有 NVIDIA 类型依赖。没有 SDK 时 `IsSupported`/`SetMode` 返回不支持原因。

准备脚本固定获取 NVIDIA 官方 **UE5.8 DLSS4.5 Plugin 8.9.0 / NGX 310.9.16** 包；这是脚本锁定的集成版本，不是本仓库重新发布的 SDK。先检查并接受 NVIDIA 的原始许可，给**宿主** `.gitignore` 添加 `/Plugins/NVIDIA/`，然后从宿主根目录运行：

```powershell
./Plugins/HonkaiToonRenderer/Scripts/Prepare-NvidiaDlss.ps1 `
  -ProjectRoot . `
  -ArchivePath ./Downloads/2026.10.16_UE5.8_DLSS4.5Plugin_v8.9.0.zip
```

也可将 `-ArchivePath ...` 换为 `-Download`，从脚本固定的 NVIDIA 地址下载。`ProjectRoot` 必须恰有一个 `.uproject`。脚本验证包哈希和生产 DLL 签名，保留许可证/来源信息；遇到现有 SDK 文件不一致时拒绝覆盖。

接口请求 DLAA 的 100% 分辨率，或由官方 API 查询 DLSS Quality 比例，并请求预设 M。它暂时接管 AA、屏幕比例、temporal upscaler、动态分辨率及 DLSS 开关等 CVar，关闭时移除自己的临时层，保留其他来源的后续控制台设置。多个系统不能同时持有这组全局配置。

该适配器不启用帧生成或光线重构；已有外部光线重构会被拒绝。独立 SceneCapture/串流输出不自动使用主视口 DLSS。Slate UI 仍按输出分辨率绘制，不能用 DLSS 代替字体/DPI 设置。成功返回表示设置请求已生效，实际 GPU 特性、驱动兼容和图像质量仍须检查 NVIDIA 运行日志及真实画面。

SDK 及其 DLL、许可文档不属于本仓库的 MIT 授权；分发要求见[第三方声明](../THIRD_PARTY_NOTICES.md)及取得的官方包原文。
