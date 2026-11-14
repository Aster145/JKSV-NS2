# JKSV-NS2

Switch 2 / kaiHEN 版本的 JKSV，当前版本 **1.0.0**。

- 原版作者：[J-D-K（JK）](https://github.com/J-D-K/JKSV)
- NS2 移植与维护：**果冻扭扭**
- 哔哩哔哩账号：**果冻扭扭扭**

## 下载与使用

在本仓库 [Releases](https://github.com/Aster145/JKSV-NS2/releases) 下载 NRO 或 SD 卡安装包。
安装包解压到 SD 卡根目录，程序位于 `sdmc:/switch/JKSV-NS2/JKSV-NS2.nro`。
日志 `JKSV-NS2.log` 保存到 NRO 同目录。备份仍位于 `sdmc:/JKSV/`。

- 默认简体中文，可在设置中切换 English / 日本語，重启生效。
- 工具菜单提供 FTP 服务：端口 **1337**，匿名登录、被动模式，根目录为 SD 卡。
- FTP 仅在该页面打开时运行，按 B 停止并返回；支持中文和带空格目录、上传下载、续传、改名及文件管理。
- 关于页面保留原作者、维护者、B 站账号和原项目致谢。

## 构建与验证

本仓库直接包含修改后的 `Libraries/FsLib` 和 `Libraries/SDLLib`，无需初始化 submodule。
必须使用匹配当前 newlib ABI 的重编 `libounce_core.a`，详见 [构建说明](BUILDING-NS2.txt)。
发行构建关闭 curl/remote；FTP 服务独立于这两个编译开关。

前版启动、界面、备份和恢复已由用户进行实机验证。本版通过交叉编译、资源打包检查、
18 个日志路径用例及本机 FTP 协议传输测试；新增 FTP、关于页面和日志路径尚待 Switch 2 实机确认。

## 许可与来源

保留原版 GPL-3.0 许可与第三方素材致谢，见 [LICENSE](LICENSE)。
这是基于原项目源码的独立 NS2 版本；上游基线记录在 [UPSTREAM.json](UPSTREAM.json)。

---

## 原版项目说明

# JKSV Rewrite
<p align="center">
  <a href="https://github.com/J-D-K/JKSV">
    <img alt="Language" src="https://img.shields.io/github/languages/top/J-D-K/JKSV?style=for-the-badge">
  </a>
  <a href="https://github.com/J-D-K/JKSV/stargazers">
    <img alt="GitHub stars" src="https://img.shields.io/github/stars/J-D-K/JKSV?style=for-the-badge">
  </a>
  <a href="https://github.com/J-D-K/JKSV/network/members">
    <img alt="GitHub forks" src="https://img.shields.io/github/forks/J-D-K/JKSV?style=for-the-badge">
  </a>
  <a href="https://github.com/J-D-K/JKSV/releases">
    <img alt="GitHub downloads" src="https://img.shields.io/github/downloads/J-D-K/JKSV/total?style=for-the-badge">
  </a>
  <a href="https://github.com/J-D-K/JKSV/releases/latest">
    <img alt="GitHub release" src="https://img.shields.io/github/v/release/J-D-K/JKSV?style=for-the-badge">
  </a>
  <a href="https://github.com/J-D-K/JKSV/blob/master/LICENSE">
    <img alt="License" src="https://img.shields.io/github/license/J-D-K/JKSV?style=for-the-badge">
  </a>
</p>

---

<p align="center">
	If you appreciate my work, or if JKSM or JKSV has gotten you out of a jam, please consider supporting development through a donation. Any contribution is appreciated, but never required! Creating and maintaining these tools takes a lot of time and effort.
</p>
<p align="center">
	<a href='https://ko-fi.com/W7W51JF9W6' target='_blank'><img height='42' style='border:px;height:42px;' src='https://storage.ko-fi.com/cdn/kofi3.png?v=6' border='0' alt='Support me for the countless hours I poured into JKSV' /></a>
</p>

## Rewritten from the ground up:

JKSV's rewrite makes improvements on the following:
- **Clean up and modernize the codebase:** The original master branch code became a mess over time and is extremely difficult to navigate and maintain. 	
- **Fully encapsulated design:** Everything is encapsulated via interfaces. This alone makes the rewrite easier to work with and **much**  less fragile in comparison to the original.
- **Improved error logging:** The master branch did an extremely poor job at logging errors. The rewrite corrects this greatly. Each error is logged with the file, line, and column making errors and bugs easier to track down.
- **Designed with translations in mind:** Unlike the original, translations weren't a feature that were tacked on later. This also made the original difficult to maintain.
- **Use standard formats:** JKSV originally used a custom file parser for data files. This has been removed in favor of JSON using libjson-c.
- **Uses [FsLib](https://github.com/J-D-K/FsLib):**
	- All file operations are handled using FsLib. FsLib is a C++ wrapper I wrote around libnx's FS API.
	- FsLib also handles SD card redirection so libraries that depend on C standard I/O can read and write files to and from the SD card.
- **Streamlined Google Drive login.**
- **Improve WebDav Support and code.**
- **Title Cache:** Titles found on the system are cached the same way [JKSM](https://github.com/J-D-K/JKSM) was. This improves boot time greatly on higher Switch firmware versions.
	- The title cache is also **auto-invalidating**. No need to manually update it like JKSM on 3DS.
- **Modern C++ design patterns and resource management:**
	- **Polymorphism and state patterns** instead of scattered global variables and logic.
	- **Smart pointers** to enure no memory leaks can occur.
	- **C APIs wrapped in smart pointers and classes** to ensure proper cleanup.
	- **Thread-pooled task system** Reduces overhead by reusing threads for tasks rather than creating and destroying them repeatedly.
	- **SDL textures** are all wrapped and managed by a central texture manager.

JKSV's rewrite is a ground-up rewrite. It shares none of the original's code **at all**. **Everything** has been reworked and rewritten.

## Features:
- **Supports all save data types used on Nintendo Switch:**
	- **Account** (User)
	- **System**
	- **BCAT**
	- **Device / Shared**
	- **Temporary**
	- **Cache**
	- **System BCAT**
- **Export Options:**
	- Standard, unpacked folders for editors.
	- ZIP for better compatibility.
		* **NOTE: ZIP is required and enforced for cloud backup!**
- **All At Once:**
	- JKSV can backup all the save data on the system or for individual users at once.
- **Cloud Support:**
	- If configured, JKSV currently supports:
		- **Google Drive**
		- **WebDav**
	- JKSV can be configured to upload these automatically.
- **Create Save Data:**
	- JKSV can create save data for any title on your system  by **itself**. No need to start a game to import data for it.
	- JKSV can create save data for every title found on your system **at once** for a single user.
- **SVI File Support:**
	- Import and create save data for titles not installed on your system. 
		- **NOTE: These are not properly installed to the system. Use at your own discretion!**
- **Resize save data:**
	- JKSV can extend save data to any size you would like. This is useful for games such as Minecraft.
- **Cleanup Tools**
	- JKSV features functions and tools to make cleanup less of a chore compared to Nintendo's Data Management and confirmation prompts:
		- Just hold A for three seconds to:
			- Delete save data for individual games.
			- Delete all save data for a single user at once.
- **Statistics:**
	- View basic data and statistics about the save and title.
- **Customizable:**
	- JKSV features many options to customize it to your liking.
- **Safety:**
	- JKSV can require holding to complete destructive operations or be set to make quick automatic backups upon restoring backups.

## Remote Storage / Upload instructions.
Detailed instructions for how to use JKSV's Google Drive and WebDav features can be found **[here](https://switch.hacks.guide/homebrew/jksv.html)**.

## Building for Switch 2 (kaiHEN):

1. Requires [devkitPro](https://devkitpro.org/), [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) (20261114), and Python 3. Install ounce-devkit into the same `DEVKITPRO` used by the compiler. The three application/library Makefiles use `libounce/switch2_rules`; `-lnx` selects libounce and the Switch 2 overlay supplies OunceVK for SDL2's EGL renderer.

2. Select Switch Development when installing or follow the instructions [here](https://devkitpro.org/wiki/Getting_Started) if you're not running Windows.

3. Install the following libraries using pacman and/or devkitpro's MSYS2 installation:
	- **switch-bzip2**
	- **switch-curl**
	- **switch-freetype**
	- **switch-harfbuzz**
	- **switch-libjpeg-turbo**
	- **switch-libjson-c**
	- **switch-libpng**
	- **switch-libwebp**
	- **switch-sdl2**
	- **switch-sdl2_image**
	- **switch-tinyxml2**
	- **switch-zlib**
4. Clone this private repository: `git clone https://github.com/Aster145/JKSV-NS2.git`. The modified FsLib and SDLLib sources are included directly.

5. From a devkitPro shell (MSYS2 on Windows), run one of the following in the repository root. Ensure `python` is Python 3 and `DEVKITPRO` points to your SDK installation.

```sh
# Prepare the ABI-matched SDK directory as described in BUILDING-NS2.txt first.
make -j6 ENABLE_CURL=0 ENABLE_REMOTE=0 LIBNX=/path/to/libounce-abi-overlay
```

The output is `JKSV-NS2.nro` (version 1.0.0). Load it through the kaiHEN homebrew environment. Switching the feature flags automatically rebuilds the application's objects; `ENABLE_CURL=0 ENABLE_REMOTE=1` is rejected. The build without curl/remote does not invoke `curl-config`, include curl headers, or link curl/tinyxml2. The SD-card FTP server is independent of those switches. `json-c` remains required for local configuration.

Rendering tries accelerated + vsync, then accelerated, then software, and requires render-target textures in each mode. Startup and application messages share `JKSV-NS2.log` beside the NRO, using the launch path supplied by the loader.

The Extras menu includes an FTP page and an About page. FTP starts on port 1337 while its page is open and stops when returning with B. Its root is the SD card; use anonymous login and passive mode. About credits original author J-D-K (JK), Switch 2 maintainer 果冻扭扭, and Bilibili account 果冻扭扭扭. The launcher title is JKSV-NS2 and the author field is 果冻扭扭. Simplified Chinese remains the default interface language, with English and Japanese available in settings after restarting.

The original online updater is disabled on Switch 2 because it downloads Switch 1 releases and overwrites the running NRO. This private repository includes both modified library source trees directly; no submodule initialization is required.

Compilation and linking do not establish runtime compatibility. Validate startup, rendering, accounts, save enumeration and backup on kaiHEN; use a test save for restore/create/resize/delete checks.

## Credits and Thanks:
* [Iguniisu](https://github.com/igniscitrinus) for the icon.
	* Uses graphics from [Twemoji](https://github.com/twitter/twemoji) covered by the [Creative Commons License](https://creativecommons.org/licenses/by/4.0/legalcode)

* [shared-font](https://github.com/switchbrew/switch-portlibs-examples) example by yellows8 for loading system font with Freetype.

* Everyone that has contributed to translating and fixing the translations for JKSV. 
