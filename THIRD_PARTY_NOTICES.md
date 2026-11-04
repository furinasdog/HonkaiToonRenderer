# 第三方声明

## 本项目原创内容

本插件的原创 C++、Python 和着色器代码源于 HonkaiRendering，版权为 `Copyright (c) 2026 HonkaiRendering contributors`，保留 [LICENSE](LICENSE) 中的 MIT 许可及声明。插件名称不表示 HoYoverse、Epic Games 或 NVIDIA 对本项目的授权或背书。

## Unreal Engine 与引擎派生资产

构建和使用需要取得适用的 Unreal Engine 许可。引擎代码、引擎接口与引擎派生资产仍按 Epic 的原始条款提供，本仓库的 MIT 不替代该许可。

`Content/Materials/T_LinearWhite.uasset` 由 Unreal Engine 的 `/Engine/EngineResources/WhiteSquareTexture` 复制并调整为线性采样，保留其 Epic 内容条款。材质生成脚本也引用该引擎白纹理作为默认输入。不要将这些引擎来源内容重新声明为纯 MIT 资产。

引擎许可原文见 [Unreal Engine EULA](https://www.unrealengine.com/eula/unreal)。使用时以所安装/取得的引擎及素材所附条款为准。

## 可选 NVIDIA 集成

本仓库的 `FHonkaiUpscaling` 及其适配代码采用 MIT。另行取得的 NVIDIA 官方 UE DLSS 插件、NGX 库、生产 DLL 和 SDK 文档保留 NVIDIA 版权与其各自许可，不包含在本仓库中，也不因适配代码调用公开 API 而改为 MIT。

[准备脚本](Scripts/Prepare-NvidiaDlss.ps1)锁定官方包并校验来源。它把 SDK 安装到宿主的 `Plugins/NVIDIA`，不是本插件源码目录。宿主应将该目录排除出开源 Git 提交。

发布包含可选 NVIDIA 运行时的应用时，应保留并交付原包要求的许可与署名，遵守适用的应用分发和发布通知要求；具体义务以取得的官方 SDK 所附 NVIDIA RTX SDK / NGX 原文为准。脚本不会代应用发布者完成这些事项，也不授权再分发任意 SDK 文件。

## 外部模型与生成内容

本仓库不附带角色模型、角色贴图、游戏截图、立绘、摄像头或录像数据。宿主提供的 BaseTexture、Ramp、FaceMap 和其他素材保留各自许可。渲染图片、视频、直播或派生控制图不会自动免除源模型的使用规则。

## 渲染阅读资料

以下为原项目参考资料的链接，不是本仓库随附的代码或可再分发素材：

- [Unity 卡通渲染学习笔记：崩铁角色渲染](https://zhuanlan.zhihu.com/p/711037262)
- [崩坏 3 的卡通渲染实现方式拆解](https://zhuanlan.zhihu.com/p/120908528)
- [Unity URP：复刻原神三渲二效果学习笔记](https://zhuanlan.zhihu.com/p/699682089)

本文仅保留出处；不在仓库中复制文章正文或图片。第三方项目若需复用其中内容，应自行核对相应作者许可。
