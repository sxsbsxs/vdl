(() => {
  const form = document.getElementById("resolve-form");
  const input = document.getElementById("video-url");
  const pasteButton = document.getElementById("paste-button");
  const resolveButton = document.getElementById("resolve-button");
  const resolveLabel = document.getElementById("resolve-label");
  const notice = document.getElementById("notice");
  const resultSection = document.getElementById("result-section");
  const formatList = document.getElementById("format-list");
  const formatCount = document.getElementById("format-count");
  const downloadButton = document.getElementById("download-button");
  const downloadLabel = document.getElementById("download-label");
  const toast = document.getElementById("toast");
  let activeUrl = "";
  let toastTimer;

  function showNotice(message, type = "") {
    notice.textContent = message;
    notice.className = "notice" + (type ? " " + type : "");
    notice.hidden = false;
  }
  function hideNotice() { notice.hidden = true; }
  function showToast(message) {
    toast.textContent = message;
    toast.classList.add("visible");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toast.classList.remove("visible"), 2400);
  }
  function durationText(seconds) {
    if (!seconds || seconds < 0) return "时长未知";
    const n = Math.floor(seconds);
    const h = Math.floor(n / 3600);
    const m = Math.floor((n % 3600) / 60);
    const s = n % 60;
    return h ? `${h}:${String(m).padStart(2,"0")}:${String(s).padStart(2,"0")}` :
      `${m}:${String(s).padStart(2,"0")}`;
  }
  function sizeText(bytes) {
    if (!bytes || bytes <= 0) return "大小未知";
    const mb = bytes / 1024 / 1024;
    return mb >= 1024 ? (mb / 1024).toFixed(1) + " GB" : mb.toFixed(1) + " MB";
  }
  function formatTitle(item) {
    const quality = item.height ? item.height + "p" : (item.format_note || "原始格式");
    return item.fps ? quality + " · " + item.fps + " FPS" : quality;
  }
  function renderFormats(formats) {
    formatList.replaceChildren();
    const list = Array.isArray(formats) ? formats : [];
    formatCount.textContent = list.length + " 个格式";
    if (!list.length) {
      const empty = document.createElement("div");
      empty.className = "format-item";
      empty.textContent = "源站没有返回可单独列出的直连格式，仍可尝试下载最佳画质。";
      formatList.appendChild(empty);
      return;
    }
    list.slice(0, 12).forEach((item) => {
      const card = document.createElement("div");
      card.className = "format-item";
      const main = document.createElement("div");
      main.className = "format-main";
      const quality = document.createElement("span");
      quality.className = "format-quality";
      quality.textContent = formatTitle(item);
      const ext = document.createElement("span");
      ext.className = "format-ext";
      ext.textContent = item.ext || "media";
      main.append(quality, ext);
      const sub = document.createElement("div");
      sub.className = "format-sub";
      const details = [];
      if (item.filesize) details.push(sizeText(item.filesize));
      if (item.vcodec && item.vcodec !== "none") details.push(item.vcodec);
      if (item.acodec && item.acodec !== "none") details.push(item.acodec);
      sub.textContent = details.join(" · ") || "格式可用";
      card.append(main, sub);
      formatList.appendChild(card);
    });
  }
  function renderResult(data) {
    document.getElementById("video-title").textContent = data.title || "未命名视频";
    document.getElementById("video-uploader").textContent = data.uploader || "未知作者";
    document.getElementById("video-duration").textContent = durationText(data.duration);
    document.getElementById("video-platform").textContent = data.extractor || "VIDEO";
    const source = document.getElementById("source-link");
    source.href = data.webpage_url || activeUrl;

    const image = document.getElementById("video-thumbnail");
    const fallback = document.getElementById("thumbnail-fallback");
    image.onerror = () => { image.hidden = true; fallback.hidden = false; };
    image.onload = () => { image.hidden = false; fallback.hidden = true; };
    if (data.thumbnail) {
      image.hidden = false;
      fallback.hidden = true;
      image.src = data.thumbnail;
    } else {
      image.hidden = true;
      fallback.hidden = false;
    }
    renderFormats(data.formats);
    resultSection.hidden = false;
    resultSection.scrollIntoView({ behavior: "smooth", block: "start" });
  }

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const url = input.value.trim();
    if (!url) { showNotice("先粘贴一个视频链接。", "error"); return; }

    activeUrl = url;
    resolveButton.disabled = true;
    resolveLabel.textContent = "正在解析…";
    hideNotice();
    resultSection.hidden = true;
    try {
      const response = await fetch("/api/resolve", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ url })
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok) throw new Error(data.error || "解析失败（HTTP " + response.status + "）");
      renderResult(data);
      showNotice("解析完成，可以下载最佳画质。", "success");
    } catch (error) {
      showNotice(error.message || "网络错误，请稍后重试。", "error");
    } finally {
      resolveButton.disabled = false;
      resolveLabel.textContent = "解析视频";
    }
  });

  pasteButton.addEventListener("click", async () => {
    try {
      input.value = await navigator.clipboard.readText();
      input.focus();
    } catch (_) {
      input.focus();
      showToast("请长按输入框并粘贴链接");
    }
  });

  downloadButton.addEventListener("click", () => {
    if (!activeUrl) return;
    const target = "/api/download?url=" + encodeURIComponent(activeUrl);
    downloadButton.disabled = true;
    downloadLabel.textContent = "正在准备下载…";
    window.location.assign(target);
    window.setTimeout(() => {
      downloadButton.disabled = false;
      downloadLabel.textContent = "下载视频";
    }, 5000);
  });

  document.getElementById("clear-button").addEventListener("click", () => {
    resultSection.hidden = true;
    formatList.replaceChildren();
    activeUrl = "";
    input.value = "";
    hideNotice();
    window.scrollTo({ top: 0, behavior: "smooth" });
  });
})();
