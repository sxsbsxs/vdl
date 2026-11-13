#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;
extern char **environ;

namespace {
constexpr std::size_t kMaxInputBytes = 16 * 1024;
constexpr std::size_t kMaxOutputBytes = 6 * 1024 * 1024;
constexpr int kProcessTimeoutSeconds = 48;
constexpr int kRateLimitPerMinute = 5;

struct ProcessResult {
    std::string output;
    std::string error;
    int exit_code = -1;
    bool timed_out = false;
    bool output_too_large = false;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool ends_with_domain(const std::string &host, const std::string &domain) {
    return host == domain ||
           (host.size() > domain.size() &&
            host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
            host[host.size() - domain.size() - 1] == '.');
}

std::string get_allowed_host(const std::string &url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return {};
    const std::string scheme = lower(url.substr(0, scheme_end));
    if (scheme != "https" && scheme != "http") return {};

    const std::size_t authority_start = scheme_end + 3;
    const auto authority_end = url.find_first_of("/?#", authority_start);
    std::string authority = url.substr(
        authority_start,
        authority_end == std::string::npos ? std::string::npos : authority_end - authority_start
    );
    if (authority.empty() || authority.find('@') != std::string::npos ||
        authority.find_first_of(" \t\r\n") != std::string::npos) return {};

    std::string host = authority;
    const auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
        if (authority.find(':') != colon) return {};
        const std::string port = authority.substr(colon + 1);
        if ((scheme == "https" && port != "443") ||
            (scheme == "http" && port != "80")) return {};
        host = authority.substr(0, colon);
    }
    host = lower(host);
    if (!host.empty() && host.back() == '.') host.pop_back();

    static const std::array<const char *, 24> allowed = {
        "youtube.com", "youtu.be", "youtube-nocookie.com",
        "bilibili.com", "bilibili.tv", "b23.tv",
        "tiktok.com", "douyin.com", "vimeo.com",
        "twitter.com", "x.com", "instagram.com",
        "facebook.com", "fb.watch", "twitch.tv",
        "dailymotion.com", "reddit.com", "soundcloud.com",
        "nicovideo.jp", "weibo.com", "acfun.cn",
        "ixigua.com", "youku.com", "iqiyi.com"
    };
    for (const char *domain : allowed) {
        if (ends_with_domain(host, domain)) return host;
    }
    return {};
}

bool is_http_url(const std::string &url) {
    return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
}

std::string client_ip(const httplib::Request &req) {
    // Render's trusted edge proxy appends the connecting client address to this chain.
    std::string forwarded = req.get_header_value("X-Forwarded-For");
    if (!forwarded.empty()) {
        const auto comma = forwarded.rfind(',');
        if (comma != std::string::npos) forwarded = forwarded.substr(comma + 1);
        forwarded = trim(forwarded);
        if (!forwarded.empty()) return forwarded;
    }
    return req.remote_addr.empty() ? "unknown" : req.remote_addr;
}

bool allow_request(const std::string &ip) {
    using Clock = std::chrono::steady_clock;
    static std::mutex mutex;
    static std::unordered_map<std::string, std::pair<Clock::time_point, int>> visits;
    std::lock_guard<std::mutex> lock(mutex);
    const auto now = Clock::now();
    auto &entry = visits[ip];
    if (entry.second == 0 || now - entry.first >= std::chrono::minutes(1)) {
        entry = {now, 0};
    }
    if (entry.second >= kRateLimitPerMinute) return false;
    ++entry.second;
    if (visits.size() > 2048) {
        for (auto it = visits.begin(); it != visits.end();) {
            if (now - it->second.first >= std::chrono::minutes(2)) it = visits.erase(it);
            else ++it;
        }
    }
    return true;
}

void set_json(httplib::Response &res, int status, const json &body) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

void append_from_fd(int &fd, std::string &target, std::size_t limit,
                    bool &too_large, pid_t pid) {
    if (fd < 0) return;
    std::array<char, 8192> buffer{};
    while (true) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            target.append(buffer.data(), static_cast<std::size_t>(count));
            if (target.size() > limit) {
                too_large = true;
                ::kill(pid, SIGKILL);
                return;
            }
        } else if (count == 0) {
            ::close(fd);
            fd = -1;
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
            ::close(fd);
            fd = -1;
            return;
        }
    }
}

ProcessResult run_ytdlp(const std::string &url) {
    ProcessResult result;
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        if (out_pipe[0] >= 0) { ::close(out_pipe[0]); ::close(out_pipe[1]); }
        if (err_pipe[0] >= 0) { ::close(err_pipe[0]); ::close(err_pipe[1]); }
        result.error = "Could not create process pipes.";
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[1]);

    const char *configured_bin = std::getenv("YTDLP_BIN");
    const std::string binary = configured_bin && *configured_bin ? configured_bin : "yt-dlp";
    std::vector<std::string> args = {
        binary, "--no-config", "--no-cache-dir", "--no-warnings", "--no-progress",
        "--no-playlist", "--skip-download", "--dump-single-json",
        "--socket-timeout", "12", "--retries", "1", "--fragment-retries", "1", url
    };
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int spawn_status = ::posix_spawnp(
        &pid, binary.c_str(), &actions, nullptr, argv.data(), environ
    );
    posix_spawn_file_actions_destroy(&actions);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    if (spawn_status != 0) {
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        result.error = "Could not start yt-dlp: " + std::string(std::strerror(spawn_status));
        return result;
    }

    int out_fd = out_pipe[0];
    int err_fd = err_pipe[0];
    ::fcntl(out_fd, F_SETFL, ::fcntl(out_fd, F_GETFL) | O_NONBLOCK);
    ::fcntl(err_fd, F_SETFL, ::fcntl(err_fd, F_GETFL) | O_NONBLOCK);
    bool child_exited = false;
    int child_status = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(kProcessTimeoutSeconds);

    while (out_fd >= 0 || err_fd >= 0 || !child_exited) {
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            ::kill(pid, SIGKILL);
        }
        pollfd descriptors[2] = {
            {out_fd, static_cast<short>(POLLIN | POLLHUP | POLLERR), 0},
            {err_fd, static_cast<short>(POLLIN | POLLHUP | POLLERR), 0}
        };
        ::poll(descriptors, 2, 100);
        if (out_fd >= 0 && descriptors[0].revents) {
            append_from_fd(out_fd, result.output, kMaxOutputBytes,
                           result.output_too_large, pid);
        }
        if (err_fd >= 0 && descriptors[1].revents) {
            bool ignored_limit = false;
            append_from_fd(err_fd, result.error, 32 * 1024, ignored_limit, pid);
        }
        if (!child_exited && ::waitpid(pid, &child_status, WNOHANG) == pid) {
            child_exited = true;
        }
        if (result.timed_out || result.output_too_large) {
            if (!child_exited) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, &child_status, 0);
                child_exited = true;
            }
            if (out_fd >= 0) { ::close(out_fd); out_fd = -1; }
            if (err_fd >= 0) { ::close(err_fd); err_fd = -1; }
            break;
        }
    }
    if (!child_exited) ::waitpid(pid, &child_status, 0);
    if (WIFEXITED(child_status)) result.exit_code = WEXITSTATUS(child_status);
    else if (WIFSIGNALED(child_status)) result.exit_code = 128 + WTERMSIG(child_status);
    return result;
}

std::string json_string(const json &object, const char *key,
                        const std::string &fallback = "") {
    if (!object.is_object() || !object.contains(key) || object[key].is_null()) return fallback;
    if (object[key].is_string()) return object[key].get<std::string>();
    return fallback;
}

json nullable_number(const json &object, const char *key) {
    if (object.is_object() && object.contains(key) && object[key].is_number()) return object[key];
    return nullptr;
}

json filter_info(const json &info, const std::string &source_url) {
    json result;
    result["id"] = json_string(info, "id");
    result["title"] = json_string(info, "title", "Untitled video");
    result["webpage_url"] = json_string(info, "webpage_url", source_url);
    result["thumbnail"] = json_string(info, "thumbnail");
    result["duration"] = nullable_number(info, "duration");
    result["uploader"] = json_string(info, "uploader");
    result["extractor"] = json_string(info, "extractor_key", json_string(info, "extractor"));
    result["formats"] = json::array();

    const json formats = info.value("formats", json::array());
    for (const auto &format : formats) {
        if (!format.is_object()) continue;
        const std::string media_url = json_string(format, "url");
        if (!is_http_url(media_url)) continue;
        const std::string protocol = json_string(format, "protocol");
        if (protocol != "http" && protocol != "https") continue;

        json item;
        item["format_id"] = json_string(format, "format_id");
        item["ext"] = json_string(format, "ext");
        item["format_note"] = json_string(format, "format_note");
        item["width"] = nullable_number(format, "width");
        item["height"] = nullable_number(format, "height");
        item["fps"] = nullable_number(format, "fps");
        item["filesize"] = nullable_number(format, "filesize");
        if (item["filesize"].is_null()) item["filesize"] = nullable_number(format, "filesize_approx");
        item["vcodec"] = json_string(format, "vcodec");
        item["acodec"] = json_string(format, "acodec");
        item["has_video"] = !item["vcodec"].get<std::string>().empty() && item["vcodec"] != "none";
        item["has_audio"] = !item["acodec"].get<std::string>().empty() && item["acodec"] != "none";
        item["url"] = media_url;
        result["formats"].push_back(std::move(item));
        if (result["formats"].size() >= 100) break;
    }

    std::sort(result["formats"].begin(), result["formats"].end(),
              [](const json &a, const json &b) {
        const int ah = a.contains("height") && a["height"].is_number() ? a["height"].get<int>() : 0;
        const int bh = b.contains("height") && b["height"].is_number() ? b["height"].get<int>() : 0;
        if (ah != bh) return ah > bh;
        if (a.value("has_audio", false) != b.value("has_audio", false)) {
            return a.value("has_audio", false);
        }
        return a.value("format_id", "") < b.value("format_id", "");
    });
    return result;
}
} // namespace

int main() {
    httplib::Server server;
    server.set_payload_max_length(kMaxInputBytes);
    server.set_read_timeout(10, 0);
    server.set_write_timeout(15, 0);
    server.set_default_headers({
        {"X-Content-Type-Options", "nosniff"},
        {"Referrer-Policy", "no-referrer"},
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    server.Options(R"(.*)", [](const httplib::Request &, httplib::Response &res) {
        res.status = 204;
    });
    server.Get("/api/health", [](const httplib::Request &, httplib::Response &res) {
        set_json(res, 200, {
            {"online", true}, {"service", "VideoDL"},
            {"engine", "yt-dlp"}, {"api", "v1"}
        });
    });

    server.Post("/api/resolve", [](const httplib::Request &req, httplib::Response &res) {
        if (!allow_request(client_ip(req))) {
            set_json(res, 429, {{"error", "请求过于频繁，请稍后再试。"}, {"retry_after_seconds", 60}});
            return;
        }
        json input;
        try {
            input = json::parse(req.body);
        } catch (...) {
            set_json(res, 400, {{"error", "请求不是有效的 JSON。"}});
            return;
        }
        if (!input.is_object() || !input.contains("url") || !input["url"].is_string()) {
            set_json(res, 400, {{"error", "请提供视频 URL。"}});
            return;
        }

        const std::string url = trim(input["url"].get<std::string>());
        if (url.empty() || url.size() > 2048 ||
            url.find_first_of("\r\n\t ") != std::string::npos) {
            set_json(res, 400, {{"error", "视频链接为空或格式无效。"}});
            return;
        }
        if (get_allowed_host(url).empty()) {
            set_json(res, 400, {
                {"error", "暂不支持这个链接域名。当前版本仅接受常见视频站点链接。"},
                {"supported_domains", {
                    "YouTube", "Bilibili", "TikTok", "Douyin", "Vimeo",
                    "X / Twitter", "Instagram", "Facebook", "Twitch",
                    "Dailymotion", "Reddit", "SoundCloud", "Niconico",
                    "Weibo", "AcFun", "Ixigua", "Youku", "iQIYI"
                }}
            });
            return;
        }

        const ProcessResult process = run_ytdlp(url);
        if (process.timed_out) {
            set_json(res, 504, {{"error", "解析超时。请稍后重试，或检查视频链接。"}});
            return;
        }
        if (process.output_too_large) {
            set_json(res, 502, {{"error", "解析结果过大，已停止本次任务。"}});
            return;
        }
        if (process.exit_code != 0) {
            std::string message = trim(process.error);
            if (message.empty()) {
                message = "yt-dlp 无法解析该视频。视频可能不受支持、需要登录，或受到地区限制。";
            }
            if (message.size() > 500) message.resize(500);
            set_json(res, 422, {{"error", message}});
            return;
        }

        json info;
        try {
            info = json::parse(process.output);
        } catch (...) {
            set_json(res, 502, {{"error", "解析引擎返回的数据无法读取。"}});
            return;
        }

        json result = filter_info(info, url);
        if (result["formats"].empty()) {
            set_json(res, 422, {
                {"error", "已取得视频信息，但没有可供浏览器直接打开的 HTTP/HTTPS 格式。该视频可能只有分离音视频流或受防盗链保护。"}
            });
            return;
        }
        set_json(res, 200, result);
    });

    server.set_mount_point("/", "./frontend");
    const char *port_env = std::getenv("PORT");
    int port = 10000;
    if (port_env && *port_env) {
        try { port = std::stoi(port_env); } catch (...) { port = 10000; }
    }
    if (port < 1 || port > 65535) port = 10000;
    server.new_task_queue = [] { return new httplib::ThreadPool(2); };
    return server.listen("0.0.0.0", port) ? 0 : 1;
}
