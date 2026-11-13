# VideoDL

VideoDL 是一个手机和电脑都能打开的在线视频解析下载网页。

**使用方式：打开 Cloudflare 分配的网址 → 粘贴视频页面链接 → 点击“解析视频” → 点击“下载最佳画质”。**

## 新架构

```text
浏览器
  │
  ▼
Cloudflare Worker（统一网址、API 路由）
  ├── HTML / CSS / JavaScript（静态前端）
  │
  └── Durable Object
         │
         ▼
      Cloudflare Container（Linux / amd64）
         ├── C++17 HTTP API
         ├── yt-dlp 独立 Linux 可执行文件（解析网站）
         └── FFmpeg + Deno（媒体合并和部分站点提取支持）
```

后端 HTTP 服务使用 C++17。没有 Python Web 服务，也不再使用 GitHub Issues 进行解析。C++ 通过安全的子进程参数调用 yt-dlp 独立可执行文件，避免自己维护数十个网站的解析规则。yt-dlp 的独立 Linux 发布文件由 Docker 构建时下载。

## 功能

- 网页直接输入 Bilibili、YouTube、抖音等视频页面链接。
- 返回标题、封面、作者、时长和可用格式信息。
- 点击下载按钮，由 C++ 后端调用 yt-dlp 获取最佳可用画质。
- FFmpeg 在需要时合并分离的视频和音频流。
- 下载文件通过 HTTP 流式返回浏览器，不需要在网页里粘贴媒体直链。
- 基于 Cloudflare Worker + Durable Object + Container 部署。
- 前端和 API 使用同一个网址，不需要单独填写 API 地址。

## 目前支持范围

项目使用 yt-dlp 的站点提取器。实际可解析性取决于 yt-dlp 支持情况、源站变化、地区限制、账号权限和视频本身的访问设置。公开视频优先；本项目不提供 Cookie 注入、不绕过 DRM、付费墙或其他访问控制。

## 部署到 Cloudflare

**注意费用：Cloudflare Containers 不是 Workers Free 免费功能。** 需要 Workers Paid（最低 5 美元/月）；容器活跃运行时间、CPU、内存、磁盘和容器出站流量可能继续计费。下载由容器流式传回，也会使用容器出站流量。部署前先检查 Cloudflare 官方价格：

- <https://developers.cloudflare.com/workers/platform/pricing/>
- <https://developers.cloudflare.com/containers/platform/pricing/>

### 1. Cloudflare 准备

1. 登录 Cloudflare，启用 Workers Paid。
2. 在 Cloudflare Dashboard → My Profile → API Tokens 创建 API Token。
3. 授权部署所需的 Workers Scripts、Containers 和 Account Settings 读取权限。
4. 复制 Cloudflare Account ID。

### 2. GitHub Secrets

打开本仓库 Settings → Secrets and variables → Actions，添加：

| Name | Value |
|---|---|
| `CLOUDFLARE_API_TOKEN` | 上一步创建的 API Token |
| `CLOUDFLARE_ACCOUNT_ID` | Cloudflare Account ID |

不要把 Token 写进源码或前端。

### 3. 发布

将本次重构 PR 合并到 `main`。GitHub Actions 会自动构建 C++ 容器镜像并部署 Worker。也可以在 Actions 页面手动运行 **Deploy VideoDL to Cloudflare**。

首次部署后 Cloudflare 需要配置容器镜像和实例，可能要等几分钟才可解析。部署成功后，在 Cloudflare Workers & Pages 中打开 `videodl`，使用它分配的 `workers.dev` 地址访问网页。

## API

- `GET /api/health`：服务健康检查。
- `POST /api/resolve`：JSON 请求体 `{"url":"https://视频页面链接"}`，返回视频元数据与格式信息。
- `GET /api/download?url=...`：调用 yt-dlp 下载最佳可用画质，并以附件流返回。

## 本地构建容器

需要 Docker：

```bash
docker build --platform linux/amd64 -t videodl .
docker run --rm -p 10000:10000 videodl
```

本地 API 地址：`http://localhost:10000/api/health`。

## 安全和限制

- 后端只接受代码中列出的常见视频站点域名；不会把任意 URL 当成通用代理。
- 输入的页面 URL 会在服务器端检查协议和主机名。
- 解析和下载有超时、请求大小限制和基础 IP 频率限制。
- 下载任务会在临时目录生成文件，响应结束后清理。
- 不要提交私人视频链接、Cookie、访问令牌或其他敏感信息。
- 仅下载你拥有权利或获得许可的内容，并遵守来源网站条款及当地法律。

## 技术栈与许可

- C++17、cpp-httplib、nlohmann/json
- HTML、CSS、原生 JavaScript
- Cloudflare Workers、Durable Objects、Containers
- yt-dlp、FFmpeg、Deno

VideoDL 主项目采用 MIT License。yt-dlp、FFmpeg、Deno 及构建依赖均为独立第三方项目，分别遵循其各自许可证。
