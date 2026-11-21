# QingMod-PBZ

《影之刃零》(Phantom Blade Zero / PBZ) 的「晴 · 霆光 剑意逍遥」MMD 角色替换 Mod。
## 内容

- **release/** — 已打包的 Mod 目录（`QingMMD_PBZ_v1/`），含 `manifest.json`、`SHA256SUMS.txt`、安装说明与模型原始说明。
- **output/** — 构建出的发布 ZIP。
- **runtime/** — UE4SS C++ 运行库源码（`src/`、`include/`、`CMakeLists.txt`、`build_core.cmd`）。
- **ProjectPBZ/** — UE 工程源码（`Source/`、`Config/`、`Plugins/`、`ProjectPBZ.uproject`）。
- **model/** — 模型源 PMX 与贴图。
- **data/** — 转换流程的主资源（Blender 源文件、FBX、JSON 配置等）。

测试脚本、诊断输出、日志、隔离备份与构建中间产物未纳入本仓库。

## 安装
1. 完全退出游戏，确认已安装与本包兼容的 UE4SS（已验证加载器 SHA-256 见安装说明）。
2. 从本仓库 [Releases](../../releases) 下载 `晴Mod_v1.1.zip`。
3. 将 ZIP 内全部内容**直接解压到游戏根目录**（同时含 `ProjectPBZ` 与 `platform` 两个文件夹的那一层），保留目录结构。
4. 若已安装旧瑟瑞斯 MMD 替换包，先按安装说明移出冲突文件。
5. 启动游戏。诊断日志位于 `platform/steam/ue4ss/Mods/QingFullMMD/runtime.log`。
详细步骤与卸载回退见 [`安装说明.txt`](release/QingMMD_PBZ_v1/安装说明.txt)。

## 从源码构建

- **运行库**：安装 Visual Studio C++ 工具链，进入 `runtime/`，运行 `build_core.cmd`（或直接 `cmake -B build` 后构建），产出 `main.dll`。第三方依赖许可证见 `release/QingMMD_PBZ_v1/platform/steam/ue4ss/Mods/QingFullMMD/licenses/`。
- **UE 资源**：用 Unreal Editor 打开 `ProjectPBZ/ProjectPBZ.uproject`，`ProjectPBZ/Source/QingBuilder` 提供资源构建库。Cook 后的 Pak 位于 `release/.../ProjectPBZ/Content/Paks/~mods/`。

## 校验

`release/QingMMD_PBZ_v1/SHA256SUMS.txt` 列出了发布包内每个文件的 SHA-256，可用 `sha256sum -c SHA256SUMS.txt`（在解压后的发布目录内）核对。
