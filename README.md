# Honkai Toon Renderer

面向 Unreal Engine 的卡通角色渲染插件，提供多色阶 Toon Ramp、双层彩色高光、面部阈值阴影和带距离补偿的视空间描边。原创代码采用 [MIT](LICENSE)。当前插件版本为 **0.1.1**，目标引擎为 **UE 5.8**。

公开接入接口为 C++，当前没有现成的 Blueprint 函数节点；纯蓝图宿主需要自行增加桥接。插件只接收 UE 网格、相机与纹理对象。使用它不需要安装 HonkaiRendering、VTuber、VRM4U 或动捕服务，也不需要角色名、模型文件路径或特定导入器。它是独立的渲染组件，不包含角色资产，也不承诺自动还原某款游戏的美术效果。

## 能力与边界

| 能力 | 输入与限制 |
|---|---|
| 身体卡通阴影 | 在延迟渲染后处理中连续采样作者色带，缺图时使用暗、过渡、亮三色回退；保留基础贴图 |
| Toon Ramp | 可使用作者提供的竖向 Ramp；缺失或无法读取时使用内置通用色带 |
| 彩色高光 | 使用宿主提供的高光颜色和指数生成宽、窄两层高光；缺失颜色时关闭 |
| 面部阴影 | 可选的、适配当前模型 UV 的 RGBA 阈值图；插件不会自动生成 FaceMap/SDF |
| 描边 | 可选的第二份网格沿视平面法线外扩，带有界透视距离补偿；宿主同步骨骼姿态 |
| 透明与双面 | 按材质槽选择；半透明表面保留作者颜色和透明度，不经过 GBuffer 色带照明 |
| 辉光与色调 | 可选相机预设，包含手动曝光、辉光和色调设置 |
| 抗锯齿与放大 | 可选 NVIDIA DLAA/DLSS Quality 接口；无 SDK 时仍可构建基础渲染 |

当前实现面向**桌面延迟渲染、非金属表面、中性主光、一个相机中的一个主体**。固定使用 Custom Stencil 值 `1`，并将 GBuffer Material AO 字节用于色带索引。Forward、Mobile、Substrate、多主体合成、任意复杂灯光以及独立 SceneCapture 的效果需要另行适配。插件不处理模型导入、动画、物理、动捕、UI 或输出串流。

## 安装

在已配置 C++ 工具链的 UE 5.8 项目根目录中，将插件放到 `Plugins/HonkaiToonRenderer`。使用 Git 子模块时：

```sh
git submodule add https://github.com/furinasdog/HonkaiToonRenderer.git Plugins/HonkaiToonRenderer
git submodule update --init --recursive
```

发布应用时固定一个提交或版本，避免未经验证地自动更新依赖。其他 UE 版本需要重新构建和验证，不能直接复用已有编辑器 DLL。

在 `.uproject` 的 `Plugins` 数组中启用插件：

```json
{ "Name": "HonkaiToonRenderer", "Enabled": true }
```

在使用插件的模块 `.Build.cs` 中添加依赖；如果公共头文件使用插件类型，应使用公共依赖：

```csharp
PublicDependencyModuleNames.Add("HonkaiToonRenderer");
```

在宿主 `Config/DefaultEngine.ini` 中配置：

```ini
[/Script/Engine.RendererSettings]
r.CustomDepth=3
r.AllowStaticLighting=False
r.Substrate=False

[ConsoleVariables]
r.EarlyZPassOnlyMaterialMasking=0
```

这些配置是渲染通道的约定，不是通用画质推荐。静态光照会影响 Material AO 的解码，Substrate/Forward 不符合当前 GBuffer 假设；遮罩需在基础通道正确裁切。修改渲染设置后重启编辑器并按 UE 提示重编译着色器。

在宿主 `Config/DefaultGame.ini` 中保留插件材质的烘焙目录：

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysCook=(Path="/HonkaiToonRenderer/Materials")
```

随后构建宿主项目。仓库包含运行所需的八个通用材质/纹理资产，正常接入无需先运行 Python。

## 快速接入

集成分为四步：准备材质槽输入、保留渲染实例、绑定网格和相机、在销毁前解绑。所有公开渲染调用都应在游戏线程执行。

以下示例接入一个**已有 Actor**，示例类名 `ACharacterShowcase` 需替换为宿主自己的类名。在该 Actor 的头文件中包含 `HonkaiRenderInstance.h`，并声明：

```cpp
UPROPERTY(Transient)
TObjectPtr<UHonkaiRenderInstance> Renderer;

bool ApplyToon(USkeletalMeshComponent* Surface,
               USkinnedMeshComponent* Outline,
               UCameraComponent* Camera,
               const TArray<FHonkaiSurfaceInput>& Inputs);

virtual void EndPlay(const EEndPlayReason::Type Reason) override;
```

在 `.cpp` 中实现：

```cpp
#include "HonkaiRenderInstance.h"

bool ACharacterShowcase::ApplyToon(
    USkeletalMeshComponent* Surface,
    USkinnedMeshComponent* Outline,
    UCameraComponent* Camera,
    const TArray<FHonkaiSurfaceInput>& Inputs)
{
    if (!Renderer)
        Renderer = NewObject<UHonkaiRenderInstance>(this);

    FHonkaiRenderTargets Targets{Surface, Outline, Camera};
    FHonkaiRenderOptions Options;
    Options.bApplyViewPreset = true; // 适用于宿主独占的展示相机。
    FHonkaiFaceMaps FaceMaps;       // 无适配的面部图时可以为空。
    FString Error;
    if (!Renderer->Bind(Targets, Inputs, FaceMaps, Options, Error))
    {
        UE_LOG(LogTemp, Warning, TEXT("Toon bind: %s"), *Error);
        return false;              // 已有绑定不会因本次失败而被替换。
    }

    const FVector TowardLight = FVector(0.82, -0.38, 0.42).GetSafeNormal();
    Renderer->SetLighting(TowardLight, TowardLight);
    return true;
}

void ACharacterShowcase::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Renderer) Renderer->Detach();
    Super::EndPlay(Reason);
}
```

`Inputs.Num()` 必须等于 `Surface->GetNumMaterials()`，顺序与网格材质槽一致。每一项由宿主从自己的导入结果或资产配置中提供，例如：

```cpp
FHonkaiSurfaceInput Input;
Input.BaseTexture = YourBaseTexture;
Input.Tint = AuthoredDiffuseColor; // RGB 为线性颜色，A 为 [0,1] 透明度。
Input.ToonRamp = YourOptionalRamp;
Input.SpecularColor = FLinearColor(.15f, .18f, .22f); // 作者高光颜色；缺失时默认黑色关闭。
Input.Shininess = 32.f;            // 作者高光指数，必须有限且非负。
Input.bTwoSided = false;
Input.bTranslucent = false;
Input.OutlineWidth = 0.12f;        // 3 m 处的 UE 单位；透视距离补偿有上下界。
Input.OutlineColor = FLinearColor::Black;
Input.OutlineOpacity = 1.f;       // 默认是 0；希望描边可见时显式设置。
```

这里的纹理和颜色是宿主变量，不是插件提供的角色素材。未指定基础纹理时使用材质默认白纹理。默认 `Tint` 为白色，错误地遗漏原材质的 Diffuse/Alpha 会改变模型外观。

`Outline` 可以是 `nullptr`。提供时必须与主体是不同组件、材质槽数一致，并由宿主同步当前骨骼/形变；插件不会创建复制网格或执行动画。还需确认复制网格的顶点 Alpha 不为零，因为该通道参与可见性裁切。

## 第三方项目集成约定

- **对象生命周期**：用 `UPROPERTY` 持有 `UHonkaiRenderInstance`；换模型可重新调用 `Bind`。先验证新输入，成功后再替换绑定；销毁网格/相机前调用 `Detach`。
- **相机所有权**：`bApplyViewPreset` 默认为 `false`。启用时只修改给定相机的预设字段，解绑时恢复这些字段；不会修改全局画质配置。`ConfigureCamera(Camera)` 可在加载角色前设置独占展示相机，此调用本身不提供自动恢复。
- **光向与动画**：`SetLighting` 的两个参数都是指向光源的世界方向。静态展示可传同一个向量；头部动画的基底补偿由宿主提供给第二个参数，宜在骨骼姿态更新后调用。
- **材质所有权**：绑定期间由插件管理表面/描边材质和 stencil。解绑恢复绑定前的覆盖材质、描边可见性及相关深度设置，保留宿主的逐槽 overlay 和不相关相机后处理。不要同时让另一系统接管同一字段。
- **纹理**：传入有效、非虚拟的 2D 纹理。已有 GPU 资源的流送纹理不要求 CPU mip；缺少资源时会检查元数据并尝试创建资源。输入无效时返回错误，宿主应向用户报告。
- **数据更新**：`SetTint` 只更新一个表面槽的颜色/透明度。`SetFaceMaps` 更新面部图；`PrepareFaceMaps` 仅预检和准备纹理，不切换当前效果。具体通道约定见[实现说明](doc/ARCHITECTURE.md)。
- **打包**：运行时不能依赖编辑器 Python 重建材质。修改材质生成脚本后先生成并验证资产，再将更新后的 `.uasset` 纳入发行构建。

## 可选 NVIDIA DLSS

基础插件不附带 NVIDIA SDK。宿主可自行选择是否启用 DLAA 或 DLSS Quality；接口和准备方法见[可选放大集成](doc/ARCHITECTURE.md#可选抗锯齿与放大)。没有官方 SDK 或硬件不支持时接口返回原因，宿主应保留原有抗锯齿方案。

## 开发与许可

- [实现说明](doc/ARCHITECTURE.md)：渲染通道、FaceMap、描边、资产构建、DLSS 约定与限制。
- [贡献指南](CONTRIBUTING.md)：修改、验证和问题报告。
- [安全问题](SECURITY.md)与[社区行为准则](CODE_OF_CONDUCT.md)。
- [MIT 许可](LICENSE)与[第三方声明](THIRD_PARTY_NOTICES.md)。UE 引擎及引擎派生资产、可选 NVIDIA SDK 不因本项目开源而改为 MIT。

本项目不是 HoYoverse、Epic Games 或 NVIDIA 的官方产品，名称不表示获得其背书。导入素材及生成图片、视频的使用仍须遵循原素材许可。
