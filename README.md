# VideoDL

VideoDL 是一个开源在线视频链接解析网站。项目采用 **HTML/CSS/JavaScript 前端 + C++17 API + yt-dlp 解析引擎**。服务只解析视频信息并向浏览器返回源站媒体直链，不在 VideoDL 服务器上保存或中转完整视频文件。

## 功能

- 粘贴视频页面链接并解析标题、封面、时长和上传者。
- 展示源站提供的 HTTP/HTTPS 直连格式、分辨率、编码和文件大小。
- 复制直链或在新标签页打开源站地址。
- C++ API 使用参数数组启动 yt-dlp，不通过 shell 拼接用户输入。
- 内置站点域名白名单、单 IP 简单速率限制、单任务超时和输出大小上限。
- Docker 一键构建；Render Blueprint 可从 GitHub 自动部署。
- GitHub Actions 在 push / pull request 时构建 Docker 镜像。

## 技术栈

- C++17、cpp-httplib 0.27.0、nlohmann/json
- yt-dlp（Python 包，安装 default extras）
- Deno（为 YouTube 的 JavaScript challenge 解析提供运行时）
- HTML、CSS、原生 JavaScript
- Docker、Render、GitHub Actions

## 本地运行（Ubuntu / Debian）

需要 CMake、C++ 编译器、Git、Python 3、Deno 和 `nlohmann-json3-dev`。

安装依赖并创建 yt-dlp 环境：

```bash
sudo apt update
sudo apt install -y build-essential cmake git nlohmann-json3-dev python3 python3-venv
python3 -m venv .venv
. .venv/bin/activate
pip install -U "yt-dlp[default]"
```

安装 Deno：参阅 <https://docs.deno.com/runtime/getting_started/installation/>。

编译运行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
YTDLP_BIN="$PWD/.venv/bin/yt-dlp" PORT=10000 ./build/vdl-server
```

浏览器打开 <http://localhost:10000>。健康检查：<http://localhost:10000/api/health>。

## Docker 部署

```bash
docker build -t videodl .
docker run --rm -p 10000:10000 -e PORT=10000 videodl
```

浏览器打开 <http://localhost:10000>。

## Render 免费部署（无需主动添加银行卡）

1. 将本仓库推送到 GitHub，并确保仓库为 Public 或 Render 账号有权限访问。
2. 登录 <https://dashboard.render.com/>，使用 GitHub 登录或连接 GitHub。
3. 选择 **New → Blueprint**，选择 `sxsbsxs/vdl` 仓库。
4. Render 读取根目录的 `render.yaml`，确认创建名为 `videodl` 的 Web Service。
5. 选择 Free 方案并部署。首次 Docker 构建需要一段时间。
6. 部署成功后打开 Render 分配的 `https://...onrender.com` 地址。

后续向 GitHub 的 `main` 分支提交代码会自动触发 Render 部署。

**免费服务限制：** Render 免费 Web Service 空闲 15 分钟后会休眠；下一次请求会唤醒，通常约需一分钟。免费实例可能重启，文件系统不持久化，且有 CPU、内存、构建时间和出站流量限制。它适合原型和低流量测试，不是 24 小时生产 VPS。请查看 <https://render.com/docs/free> 和 <https://render.com/docs/outbound-bandwidth> 的最新规则。

## API

### 健康检查

```http
GET /api/health
```

### 解析视频

```http
POST /api/resolve
Content-Type: application/json

{"url":"https://www.youtube.com/watch?v=..."}
```

成功时返回视频元数据和直链格式。失败时返回 `{"error":"..."}`。

## 当前限制

- 当前版本只允许一组常见视频站点域名，域名白名单在 `backend/main.cpp` 的 `allowed` 数组中维护；并不意味着 yt-dlp 上游列出的每个站点都已开放。
- 每次只解析一个视频，不处理播放列表。
- 不提供登录 Cookie 上传，不解析需要账号授权的私人内容。
- 仅展示源站提供的 HTTP/HTTPS 直连格式。部分网站采用 DASH 分离音视频、HLS、临时签名 URL、Referer 校验或防盗链，浏览器可能无法直接保存；本版本不会在服务器上下载、合并或转码这些媒体。
- YouTube 完整解析需要 yt-dlp 的 EJS 组件与兼容 JavaScript 运行时；Docker 镜像安装 `yt-dlp[default]` 与 Deno。参阅 <https://github.com/yt-dlp/yt-dlp/wiki/EJS>。
- 站点策略、地区限制和 yt-dlp 更新都可能影响解析结果。yt-dlp 官方也说明，支持站点会变化，列表并不保证每个站点始终可用：<https://github.com/yt-dlp/yt-dlp/blob/master/supportedsites.md>。
- 公开服务的速率限制是轻量保护，不等同于完整的反滥用系统。上线前请继续添加缓存、队列、监控和更严格的访问控制。
- 请只下载你拥有权利或获得许可的内容，并遵守来源网站条款及当地法律。

## 许可证

MIT，详见 [LICENSE](LICENSE)。
