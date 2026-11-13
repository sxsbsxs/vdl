# VideoDL

VideoDL 是一个基于 **Cloudflare Workers + Cloudflare Containers** 的在线视频链接解析工具。前端、API 和解析引擎由同一套 Cloudflare 部署提供。Linux 容器内运行 C++17 API、Python、yt-dlp 和 Deno。

## 在线部署架构

\`\`\`text
Browser
  |
  v
Cloudflare Worker
  |
  v
Durable Object (container lifecycle / request routing)
  |
  v
Cloudflare Container (Linux / amd64)
  |-- C++17 HTTP API
  |-- Python virtualenv
  |-- yt-dlp[default]
  |-- Deno JavaScript runtime
  |-- frontend static files
  |
  v
Source website media URLs (browser downloads directly)
\`\`\`

## 功能

- 粘贴公开视频页面链接并直接解析，无需 GitHub 登录或创建 Issue。
- 返回标题、上传者、封面、时长和可用 HTTP/HTTPS 格式。
- 显示分辨率、帧率、编码、文件大小，提供直链打开和复制。
- C++ API 校验允许的站点域名，并提供基础 IP 频率限制。
- Cloudflare Durable Object 管理容器启动、就绪检查和空闲休眠。
- 视频文件不经过 VideoDL 容器中转；浏览器直接访问源站媒体 URL。

## 支持站点

YouTube、Bilibili、TikTok、抖音、Vimeo、X/Twitter、Instagram、Facebook、Twitch、Dailymotion、Reddit、SoundCloud、Niconico、微博、AcFun、西瓜视频、优酷、爱奇艺。实际支持情况取决于 yt-dlp 上游版本、源站策略、地区和登录限制。

## 部署前的重要说明

**Cloudflare Containers 不属于 Workers Free 免费额度。** 当前需要 Workers Paid 计划（最低 5 美元/月），容器的运行时间、CPU、内存、磁盘和出站流量还可能产生额外费用。请先查看 [Cloudflare Workers 定价](https://developers.cloudflare.com/workers/platform/pricing/) 和 [Containers 定价](https://developers.cloudflare.com/containers/platform/pricing/)。本项目不会承诺零成本或无限免费。

Cloudflare 容器镜像目标架构为 Linux/amd64。首次部署后需要等待容器完成配置，第一次解析会有冷启动延迟。

## Cloudflare Dashboard 部署（推荐）

1. 在 Cloudflare 账户中启用 Workers Paid 计划。
2. 在 GitHub 仓库 Settings → Secrets and variables → Actions 中添加：
   - \`CLOUDFLARE_API_TOKEN\`：具有 Workers Scripts Edit、Account Settings Read、Containers Edit 权限的 API Token。
   - \`CLOUDFLARE_ACCOUNT_ID\`：Cloudflare Account ID。
3. 在仓库 Actions 页面运行 **Deploy VideoDL to Cloudflare Containers**，或向 \`main\` 推送代码。
4. 等待 Docker 镜像构建、上传和容器部署完成。
5. 在 Cloudflare Workers & Pages 中打开 \`videodl\`，使用分配的 \`workers.dev\` 域名访问。

API Token 不要提交到仓库，也不要写入前端配置文件。

## 本地部署

需要 Node.js 20+、npm、Docker（或兼容 Docker 的引擎）和 Cloudflare API Token：

\`\`\`bash
npm install
npx wrangler login
npm run deploy
\`\`\`

查看容器：

\`\`\`bash
npx wrangler containers list
npx wrangler containers images list
\`\`\`

本地开发：

\`\`\`bash
npm run dev
\`\`\`

Wrangler 部署 Dockerfile 镜像时需要 Docker 正在运行。GitHub Actions 部署使用 GitHub runner 上的 Docker。

## API

- \`GET /api/health\`：容器健康检查。
- \`POST /api/resolve\`：请求体为 JSON，例如：

\`\`\`json
{ "url": "https://www.youtube.com/watch?v=..." }
\`\`\`

成功时返回视频元数据和格式数组；失败时返回带有 \`error\` 字段的 JSON。

## 说明与限制

- 解析结果中的媒体直链可能有时效性，且可能需要 Referer、Cookie、地区权限或专用下载器。
- 分离音视频流、HLS/DASH 或防盗链资源不一定能由浏览器直接下载。
- 本项目不提供账号 Cookie 输入，也不尝试绕过付费墙、DRM 或访问控制。
- 仅下载你拥有权利或获得许可的内容，并遵守来源网站条款及当地法律。

## 技术栈

C++17、Python 3、yt-dlp、Deno、Docker、Cloudflare Workers、Durable Objects、Cloudflare Containers、HTML/CSS/JavaScript。

## License

MIT，详见 [LICENSE](LICENSE)。
