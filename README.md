# VideoDL

VideoDL 是一个完全基于 GitHub 的开源在线视频链接解析工具。网站由 **GitHub Pages** 托管，解析任务由 **GitHub Actions + yt-dlp** 执行，不需要购买服务器、租用 VPS 或接入第三方后端平台。

## 在线使用

网站：<https://sxsbsxs.github.io/vdl/>

1. 打开网站，粘贴公开视频页面链接。
2. 点击“解析视频”，页面会打开 GitHub 的新 Issue 页面。
3. 登录 GitHub，确认视频链接后创建 Issue。
4. GitHub Actions 会自动启动 yt-dlp 解析任务。
5. 解析完成后，结果会作为评论出现在该 Issue 下。

**重要：** 使用者必须登录 GitHub 才能提交请求。Issue、视频页面链接和解析结果都是公开的，任何人都能查看。请勿提交私人视频、Cookie、访问令牌或其他敏感链接。

## 工作原理

```text
手机/电脑浏览器
      |
      v
GitHub Pages（静态网站）
      |
      v
GitHub Issues（提交视频链接）
      |
      v
GitHub Actions（临时运行环境）
      |
      v
yt-dlp + Deno（解析）
      |
      v
Issue 评论（返回标题、格式和源站直链）
      |
      v
用户浏览器直接访问源站媒体
```

- GitHub Pages 只负责展示前端。
- GitHub Actions 按需运行解析任务，不是常驻 API 服务，因此任务可能需要排队，不保证实时返回。
- VideoDL 不在 GitHub Actions 中下载、保存或中转完整视频文件，只返回 yt-dlp 提供的源站 HTTP/HTTPS 直链。
- 前端不包含 GitHub Token 或其他密钥。
- 解析结果评论公开可见，部分直链可能带有临时签名或访问参数。

## 技术栈

- HTML、CSS、原生 JavaScript
- GitHub Pages
- GitHub Issues
- GitHub Actions
- Python、yt-dlp（default extras）、Deno
- C++17 后端源码仍保留在 `backend/`，可用于未来自行部署常驻 API；当前在线网站不依赖该后端。

## 支持站点

当前工作流只接受以下站点域名，且实际解析能力取决于 yt-dlp 上游支持情况：

- YouTube、Bilibili
- TikTok、抖音
- Vimeo、X / Twitter
- Instagram、Facebook、Twitch
- Dailymotion、Reddit、SoundCloud
- Niconico、微博、AcFun、西瓜视频
- 优酷、爱奇艺

## 项目文件

- `frontend/`：GitHub Pages 网站。
- `.github/workflows/pages.yml`：自动发布静态网站。
- `.github/workflows/resolve-issue.yml`：监听 VideoDL Issue，运行 yt-dlp 并回写解析结果。
- `.github/ISSUE_TEMPLATE/videodl-request.md`：解析请求模板。
- `backend/`、`Dockerfile`、`CMakeLists.txt`：可选的 C++ 常驻 API 后端源码。

## GitHub Actions 设置

解析工作流需要仓库允许 Actions 运行，并允许工作流使用内置令牌写入 Issue 评论：

1. 打开仓库 **Settings → Actions → General**。
2. 在 **Workflow permissions** 中选择 **Read and write permissions**，以允许工作流使用内置令牌发布 Issue 评论；解析工作流在 YAML 中仅申请 `contents: read` 和 `issues: write`。
3. 确认 Actions 没有被仓库设置禁用。

如果工作流运行失败，请打开仓库 **Actions**，进入对应的 VideoDL GitHub issue parser 运行记录查看日志。

## 当前限制与安全说明

- 这是 GitHub 驱动的异步解析工具，不是实时 API。Actions 可能排队、失败或受到 GitHub 使用限制。
- 提交者需要 GitHub 账号；匿名访客不能直接创建 Issue。
- Issue 和评论公开可见，源视频 URL 与直链可能包含临时访问参数。不要提交私人或敏感链接。
- 仅允许工作流中列出的站点域名。不要将该工作流视为通用 URL 抓取器。
- 仅展示可识别的 HTTP/HTTPS 直链。部分网站使用分离音视频、HLS/DASH、Referer 校验或防盗链，可能无法通过普通浏览器直接下载。
- yt-dlp 支持站点和解析结果会随源站策略及上游版本变化。
- 请只下载你拥有权利或获得许可的内容，并遵守来源网站条款及当地法律。

## 许可证

MIT，详见 [LICENSE](LICENSE)。
