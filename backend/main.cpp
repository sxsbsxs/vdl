#include "httplib.h"
#include "json.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <regex>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
constexpr const char* YT_DLP = "/usr/local/bin/yt-dlp";
constexpr std::size_t MAX_JSON_BYTES = 16 * 1024 * 1024;
constexpr std::size_t MAX_DOWNLOAD_BYTES = 7ULL * 1024 * 1024 * 1024;
constexpr int RESOLVE_TIMEOUT_SECONDS = 75;
constexpr int DOWNLOAD_TIMEOUT_SECONDS = 900;

struct ProcessResult {
    int exit_code = -1;
    bool timed_out = false;
    bool output_too_large = false;
    std::string output;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool is_supported_url(const std::string& url) {
    if (url.empty() || url.size() > 4096 || url.find('@') != std::string::npos ||
        url.find('\\') != std::string::npos) return false;

    static const std::regex pattern(R"(^(https?)://([A-Za-z0-9.-]+)(?::[0-9]+)?(?:[/\?#]|$))",
                                    std::regex::icase);
    std::smatch match;
    if (!std::regex_search(url, match, pattern)) return false;

    const std::string scheme = lower(match[1].str());
    if (scheme != "http" && scheme != "https") return false;

    std::string host = lower(match[2].str());
    if (host.empty() || host.front() == '.' || host.back() == '.' ||
        host.find("..") != std::string::npos || host.find('.') == std::string::npos) return false;

    // Allow only known video providers. This is deliberately not an open URL proxy.
    static const std::vector<std::string> allowed = {
        "youtube.com", "youtu.be", "youtube-nocookie.com",
        "bilibili.com", "bilibili.tv", "b23.tv",
        "tiktok.com", "douyin.com", "vimeo.com",
        "twitter.com", "x.com", "instagram.com", "facebook.com", "fb.watch",
        "twitch.tv", "dailymotion.com", "reddit.com", "soundcloud.com",
        "nicovideo.jp", "weibo.com", "acfun.cn", "ixigua.com",
        "youku.com", "iqiyi.com"
    };

    for (const auto& domain : allowed) {
        if (host == domain ||
            (host.size() > domain.size() &&
             host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
             host[host.size() - domain.size() - 1] == '.')) return true;
    }
    return false;
}

ProcessResult run_process(const std::vector<std::string>& args,
                          int timeout_seconds,
                          std::size_t output_limit = MAX_JSON_BYTES) {
    ProcessResult result;
    if (args.empty()) return result;

    int pipefd[2];
    if (pipe(pipefd) != 0) return result;

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        if (devnull >= 0) close(devnull);
        execv(YT_DLP, argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    const int old_flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, old_flags | O_NONBLOCK);

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(timeout_seconds);
    bool pipe_closed = false;
    bool child_finished = false;
    int status = 0;

    while (!pipe_closed || !child_finished) {
        if (std::chrono::steady_clock::now() >= deadline && !child_finished) {
            result.timed_out = true;
            kill(pid, SIGKILL);
        }

        pollfd pfd{pipefd[0], static_cast<short>(POLLIN | POLLHUP), 0};
        poll(&pfd, 1, 100);

        if (!pipe_closed && (pfd.revents & (POLLIN | POLLHUP))) {
            std::array<char, 65536> buffer{};
            for (;;) {
                const ssize_t count = read(pipefd[0], buffer.data(), buffer.size());
                if (count > 0) {
                    if (result.output.size() + static_cast<std::size_t>(count) > output_limit) {
                        result.output_too_large = true;
                        kill(pid, SIGKILL);
                        pipe_closed = true;
                        break;
                    }
                    result.output.append(buffer.data(), static_cast<std::size_t>(count));
                } else if (count == 0) {
                    pipe_closed = true;
                    break;
                } else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) pipe_closed = true;
                    break;
                }
            }
        }

        if (!child_finished) {
            const pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) child_finished = true;
        }

        if (child_finished && !pipe_closed) {
            // Drain remaining bytes after the child exits.
            std::array<char, 65536> buffer{};
            for (;;) {
                const ssize_t count = read(pipefd[0], buffer.data(), buffer.size());
                if (count > 0) {
                    if (result.output.size() + static_cast<std::size_t>(count) <= output_limit)
                        result.output.append(buffer.data(), static_cast<std::size_t>(count));
                    else
                        result.output_too_large = true;
                } else {
                    if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                        pipe_closed = true;
                    break;
                }
            }
        }
    }

    close(pipefd[0]);
    if (result.timed_out || result.output_too_large) {
        if (!child_finished) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    }
    if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
    return result;
}

std::string string_value(const json& obj, const char* key, const std::string& fallback = "") {
    if (!obj.is_object()) return fallback;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return fallback;
    if (it->is_string()) return it->get<std::string>();
    return fallback;
}

long long number_value(const json& obj, const char* key, long long fallback = 0) {
    if (!obj.is_object()) return fallback;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null() || !it->is_number()) return fallback;
    return it->get<long long>();
}

json error_json(const std::string& message) {
    return json{{"error", message}};
}

std::mutex rate_mutex;
struct RateEntry {
    std::chrono::steady_clock::time_point window = std::chrono::steady_clock::now();
    int count = 0;
};
std::unordered_map<std::string, RateEntry> rate_entries;

bool rate_allowed(const httplib::Request& req, httplib::Response& res, int limit) {
    std::string ip = req.get_header_value("X-Forwarded-For");
    if (ip.empty()) ip = "unknown";
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(rate_mutex);
    auto& entry = rate_entries[ip];
    if (now - entry.window >= std::chrono::minutes(1)) {
        entry.window = now;
        entry.count = 0;
    }
    if (++entry.count > limit) {
        res.status = 429;
        res.set_content(error_json("请求太频繁，请一分钟后再试。").dump(), "application/json; charset=utf-8");
        return false;
    }
    return true;
}

std::string format_size(long long bytes) {
    if (bytes <= 0) return "大小未知";
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mib >= 1024.0) return std::to_string(static_cast<int>(mib / 1024.0 * 10.0) / 10.0) + " GB";
    return std::to_string(static_cast<int>(mib * 10.0) / 10.0) + " MB";
}

bool parse_video(const std::string& url, json& response, std::string& error, int& status_code) {
    const auto proc = run_process({
        YT_DLP, "--dump-single-json", "--no-warnings", "--no-playlist",
        "--skip-download", "--no-progress", "--no-color", "--no-cache-dir",
        "--socket-timeout", "20", "--retries", "1",
        "--js-runtimes", "deno", "--remote-components", "ejs:github", url
    }, RESOLVE_TIMEOUT_SECONDS);

    if (proc.timed_out) {
        error = "解析超时，请检查视频链接后重试。";
        status_code = 504;
        return false;
    }
    if (proc.output_too_large) {
        error = "解析结果过大，暂时无法处理。";
        status_code = 502;
        return false;
    }
    if (proc.exit_code != 0 || proc.output.empty()) {
        error = "解析失败。视频可能需要登录、受地区限制，或源站暂不支持。";
        status_code = 422;
        return false;
    }

    try {
        const json info = json::parse(proc.output);
        json formats = json::array();

        if (info.contains("formats") && info["formats"].is_array()) {
            for (const auto& f : info["formats"]) {
                if (!f.is_object()) continue;
                const std::string direct_url = string_value(f, "url");
                const std::string protocol = string_value(f, "protocol");
                if (direct_url.rfind("http://", 0) != 0 && direct_url.rfind("https://", 0) != 0) continue;
                if (protocol != "http" && protocol != "https") continue;

                json item;
                item["format_id"] = string_value(f, "format_id");
                item["ext"] = string_value(f, "ext", "media");
                item["format_note"] = string_value(f, "format_note");
                item["height"] = number_value(f, "height");
                item["fps"] = number_value(f, "fps");
                item["filesize"] = number_value(f, "filesize", number_value(f, "filesize_approx"));
                item["vcodec"] = string_value(f, "vcodec");
                item["acodec"] = string_value(f, "acodec");
                formats.push_back(std::move(item));
                if (formats.size() >= 30) break;
            }
        }

        response = {
            {"id", string_value(info, "id")},
            {"title", string_value(info, "title", "未命名视频")},
            {"uploader", string_value(info, "uploader", string_value(info, "channel", "未知作者"))},
            {"thumbnail", string_value(info, "thumbnail")},
            {"duration", number_value(info, "duration")},
            {"webpage_url", string_value(info, "webpage_url", url)},
            {"extractor", string_value(info, "extractor")},
            {"formats", formats}
        };
        return true;
    } catch (...) {
        error = "视频站点返回了无法识别的数据，请稍后重试。";
        status_code = 502;
        return false;
    }
}

std::string ascii_filename(const fs::path& path) {
    std::string ext = path.extension().string();
    if (ext.empty() || ext.size() > 12) ext = ".mp4";
    for (char& c : ext) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.') c = '_';
    }
    return "VideoDL" + ext;
}
} // namespace

int main() {
    httplib::Server server;
    server.set_payload_max_length(1024 * 1024);
    server.set_read_timeout(15, 0);
    server.set_write_timeout(120, 0);
    server.set_keep_alive_max_count(20);

    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"ok":true,"service":"videodl-cpp"})", "application/json; charset=utf-8");
    });

    server.Post("/api/resolve", [](const httplib::Request& req, httplib::Response& res) {
        if (!rate_allowed(req, res, 10)) return;

        try {
            const json body = json::parse(req.body);
            const std::string url = body.value("url", std::string{});
            if (!is_supported_url(url)) {
                res.status = 400;
                res.set_content(error_json("链接无效或暂不支持该视频站点。").dump(), "application/json; charset=utf-8");
                return;
            }

            json data;
            std::string error;
            int status = 422;
            if (!parse_video(url, data, error, status)) {
                res.status = status;
                res.set_content(error_json(error).dump(), "application/json; charset=utf-8");
                return;
            }
            res.set_content(data.dump(), "application/json; charset=utf-8");
        } catch (...) {
            res.status = 400;
            res.set_content(error_json("请求内容格式错误。").dump(), "application/json; charset=utf-8");
        }
    });

    server.Get("/api/download", [](const httplib::Request& req, httplib::Response& res) {
        if (!rate_allowed(req, res, 3)) return;

        const std::string url = req.get_param_value("url");
        if (!is_supported_url(url)) {
            res.status = 400;
            res.set_content("Invalid or unsupported video URL", "text/plain; charset=utf-8");
            return;
        }

        std::string temp_template = "/tmp/vdl-XXXXXX";
        std::vector<char> temp_buffer(temp_template.begin(), temp_template.end());
        temp_buffer.push_back('\0');
        char* created = mkdtemp(temp_buffer.data());
        if (!created) {
            res.status = 500;
            res.set_content("Unable to create temporary download directory", "text/plain; charset=utf-8");
            return;
        }
        const fs::path temp_dir(created);
        const std::string output_template = (temp_dir / "%(title).100B.%(ext)s").string();

        const auto proc = run_process({
            YT_DLP, "--no-warnings", "--no-playlist", "--no-progress", "--no-color",
            "--no-cache-dir", "--restrict-filenames", "--format", "bestvideo+bestaudio/best",
            "--merge-output-format", "mp4", "--output", output_template,
            "--socket-timeout", "20", "--retries", "1",
            "--js-runtimes", "deno", "--remote-components", "ejs:github", url
        }, DOWNLOAD_TIMEOUT_SECONDS, 1024 * 1024);

        if (proc.timed_out || proc.exit_code != 0) {
            fs::remove_all(temp_dir);
            res.status = proc.timed_out ? 504 : 422;
            res.set_content(proc.timed_out ? "Download timed out. Please try again." :
                                             "Download failed. The source may require login or block access.",
                            "text/plain; charset=utf-8");
            return;
        }

        fs::path output_file;
        try {
            for (const auto& entry : fs::directory_iterator(temp_dir)) {
                if (entry.is_regular_file()) {
                    output_file = entry.path();
                    break;
                }
            }
        } catch (...) {}

        if (output_file.empty()) {
            fs::remove_all(temp_dir);
            res.status = 500;
            res.set_content("The downloader did not produce a media file.", "text/plain; charset=utf-8");
            return;
        }

        std::error_code ec;
        const auto file_size = fs::file_size(output_file, ec);
        if (ec || file_size == 0 || file_size > MAX_DOWNLOAD_BYTES) {
            fs::remove_all(temp_dir);
            res.status = 413;
            res.set_content("File is empty or exceeds the 7 GB temporary storage limit.", "text/plain; charset=utf-8");
            return;
        }

        auto stream = std::make_shared<std::ifstream>(output_file, std::ios::binary);
        if (!stream->is_open()) {
            fs::remove_all(temp_dir);
            res.status = 500;
            res.set_content("Unable to open the downloaded file.", "text/plain; charset=utf-8");
            return;
        }

        res.set_header("Content-Disposition", "attachment; filename=\"" + ascii_filename(output_file) + "\"");
        res.set_header("Cache-Control", "no-store");
        res.set_content_provider(
            static_cast<std::size_t>(file_size),
            "application/octet-stream",
            [stream, file_size](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
                if (offset >= file_size) return true;
                stream->clear();
                stream->seekg(static_cast<std::streamoff>(offset));
                std::array<char, 64 * 1024> buffer{};
                std::size_t remaining = std::min<std::size_t>(length, static_cast<std::size_t>(file_size - offset));
                while (remaining > 0) {
                    const auto chunk = std::min(remaining, buffer.size());
                    stream->read(buffer.data(), static_cast<std::streamsize>(chunk));
                    const auto read_count = static_cast<std::size_t>(stream->gcount());
                    if (read_count == 0 || !sink.write(buffer.data(), read_count)) return false;
                    remaining -= read_count;
                }
                return true;
            },
            [temp_dir](bool) {
                std::error_code remove_error;
                fs::remove_all(temp_dir, remove_error);
            });
    });

    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::atoi(port_env) : 10000;
    if (port <= 0 || port > 65535) port = 10000;

    std::cout << "VideoDL C++ API listening on 0.0.0.0:" << port << std::endl;
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Failed to start HTTP server" << std::endl;
        return 1;
    }
    return 0;
}
