const API_BASE = String(window.VIDEODL_API_BASE || "").trim().replace(/\/+$/, "");
const IS_GITHUB_PAGES = window.location.hostname.endsWith("github.io");

const form = document.getElementById("resolve-form");
const input = document.getElementById("video-url");
const resolveButton = document.getElementById("resolve-button");
const buttonLabel = document.getElementById("button-label");
const notice = document.getElementById("notice");
const resultSection = document.getElementById("result");
const formatList = document.getElementById("format-list");
const toast = document.getElementById("toast");

function showNotice(message, type) {
  notice.textContent = message;
  notice.className = "notice" + (type ? " " + type : "");
  notice.hidden = false;
}
function hideNotice() {
  notice.hidden = true;
  notice.textContent = "";
}
function notify(message) {
  toast.textContent = message;
  toast.classList.add("show");
  window.clearTimeout(notify.timer);
  notify.timer = window.setTimeout(function () {
    toast.classList.remove("show");
  }, 2200);
}
function formatBytes(bytes) {
  if (bytes === null || bytes === undefined || Number(bytes) <= 0) return "大小未知";
  const units = ["B", "KB", "MB", "GB"];
  let value = Number(bytes);
  let index = 0;
  while (value >= 1024 && index < units.length - 1) {
    value /= 1024;
    index += 1;
  }
  return value.toFixed(index === 0 ? 0 : 1) + " " + units[index];
}
function formatDuration(seconds) {
  if (seconds === null || seconds === undefined || !Number.isFinite(Number(seconds))) return "";
  const total = Math.max(0, Math.floor(Number(seconds)));
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const secs = total % 60;
  return hours > 0
    ? hours + ":" + String(minutes).padStart(2, "0") + ":" + String(secs).padStart(2, "0")
    : minutes + ":" + String(secs).padStart(2, "0");
}
function makeBadge(label) {
  const span = document.createElement("span");
  span.className = "stream-badge";
  span.textContent = label;
  return span;
}
function createFormatRow(format) {
  const row = document.createElement("div");
  row.className = "format-row";
  const main = document.createElement("div");
  main.className = "format-main";
  const title = document.createElement("div");
  title.className = "format-title";
  const resolution = format.height ? String(format.height) + "p" : (format.format_note || "原始格式");
  title.textContent = resolution + " · " + (format.ext || "媒体") + " · " + (format.format_id || "格式");
  main.appendChild(title);

  const meta = document.createElement("div");
  meta.className = "format-meta";
  const details = [];
  if (format.fps) details.push(String(format.fps) + " FPS");
  if (format.vcodec && format.vcodec !== "none") details.push(format.vcodec);
  if (format.acodec && format.acodec !== "none") details.push(format.acodec);
  meta.textContent = details.join(" · ") || "源站格式";
  main.appendChild(meta);

  const badges = document.createElement("div");
  badges.className = "stream-badges";
  if (format.has_video) badges.appendChild(makeBadge("视频"));
  if (format.has_audio) badges.appendChild(makeBadge("音频"));
  if (format.has_video && !format.has_audio) badges.appendChild(makeBadge("需要单独音轨"));
  if (!format.has_video && format.has_audio) badges.appendChild(makeBadge("纯音频"));
  main.appendChild(badges);

  const size = document.createElement("div");
  size.className = "format-size";
  size.textContent = formatBytes(format.filesize);

  const actions = document.createElement("div");
  actions.className = "format-actions";
  const open = document.createElement("a");
  open.className = "action-button";
  open.href = format.url;
  open.target = "_blank";
  open.rel = "noopener noreferrer";
  open.textContent = "打开直链";
  open.title = "在新标签页打开源站媒体地址";
  actions.appendChild(open);

  const copy = document.createElement("button");
  copy.className = "copy-button";
  copy.type = "button";
  copy.textContent = "复制链接";
  copy.addEventListener("click", async function () {
    try {
      await navigator.clipboard.writeText(format.url);
      notify("直链已复制");
    } catch (_) {
      const helper = document.createElement("textarea");
      helper.value = format.url;
      document.body.appendChild(helper);
      helper.select();
      document.execCommand("copy");
      helper.remove();
      notify("直链已复制");
    }
  });
  actions.appendChild(copy);
  row.append(main, size, actions);
  return row;
}
function renderResult(data) {
  document.getElementById("video-title").textContent = data.title || "未命名视频";
  document.getElementById("video-uploader").textContent = data.uploader || "上传者信息不可用";
  document.getElementById("video-source").textContent = (data.extractor || "VIDEO").toUpperCase();

  const thumbnail = document.getElementById("video-thumbnail");
  if (data.thumbnail) {
    thumbnail.src = data.thumbnail;
    thumbnail.hidden = false;
  } else {
    thumbnail.removeAttribute("src");
    thumbnail.hidden = true;
  }

  const duration = document.getElementById("video-duration");
  duration.textContent = formatDuration(data.duration);
  duration.hidden = !duration.textContent;
  const sourceLink = document.getElementById("source-link");
  sourceLink.href = data.webpage_url || input.value.trim();

  formatList.replaceChildren();
  const formats = Array.isArray(data.formats) ? data.formats : [];
  formats.forEach(function (format) { formatList.appendChild(createFormatRow(format)); });
  document.getElementById("format-count").textContent = formats.length + " 个格式";
  resultSection.hidden = false;
  resultSection.scrollIntoView({ behavior: "smooth", block: "start" });
}
form.addEventListener("submit", function (event) {
  event.preventDefault();
  const url = input.value.trim();
  if (!url) {
    showNotice("请先粘贴一个视频链接。", "error");
    return;
  }
  if (!/^https?:\/\//i.test(url)) {
    showNotice("请输入以 http:// 或 https:// 开头的视频页面链接。", "error");
    return;
  }

  const title = "VideoDL 视频解析请求";
  const body = "视频链接：\n" + url + "\n\n请只提交你有权访问和下载的公开视频链接。";
  const issueUrl = "https://github.com/sxsbsxs/vdl/issues/new"
    + "?template=videodl-request.md"
    + "&title=" + encodeURIComponent(title)
    + "&body=" + encodeURIComponent(body);
  showNotice("即将打开 GitHub 提交页面。登录 GitHub 并创建 Issue 后，Actions 会自动解析；完成后结果会回复在该 Issue 下。", "success");
  window.open(issueUrl, "_blank", "noopener,noreferrer");
});
document.getElementById("paste-button").addEventListener("click", async function () {
  try {
    input.value = await navigator.clipboard.readText();
    input.focus();
  } catch (_) {
    input.focus();
    notify("请长按输入框并粘贴链接");
  }
});
document.getElementById("clear-button").addEventListener("click", function () {
  resultSection.hidden = true;
  formatList.replaceChildren();
  hideNotice();
  input.focus();
});
