#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <io.h>      // _isatty / _fileno
#else
#include <unistd.h>  // isatty / fileno
#endif

namespace DMSAlgo {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// 极简 header-only 日志器：分级 + 时间戳 + tag + 可选颜色 + 可选落盘 + 线程安全。
// 只依赖标准库，mock 构建保持「cmake + g++」零外部依赖。
class Logger {
 public:
  static Logger& instance() {
    static Logger logger;
    return logger;
  }

  void setLevel(LogLevel level) { level_ = level; }

  // 空路径 = 只写控制台；非空 = 追加写文件（文件不带颜色）。
  void setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (file_) {
      std::fclose(file_);
      file_ = nullptr;
    }
    filePath_.clear();
    if (path.empty()) return;
    file_ = std::fopen(path.c_str(), "a");
    if (!file_) {
      std::fprintf(stderr, "[log] cannot open log file '%s'\n", path.c_str());
      return;
    }
    filePath_ = path;
  }

  void write(LogLevel level, const std::string& tag, const std::string& msg) {
    if (level < level_) return;  // 级别过滤
    std::lock_guard<std::mutex> lk(mtx_);

    const std::string body =
        "[" + timestamp() + "] [" + levelName(level) + "] [" + tag + "] " + msg;

    // 控制台：INFO/DEBUG -> stdout，WARN/ERROR -> stderr（保持旧行为）。
    std::FILE* out = (level >= LogLevel::Warn) ? stderr : stdout;
    if (isTty(out)) {
      std::fprintf(out, "%s%s%s\n", levelColor(level), body.c_str(), "\033[0m");
    } else {
      std::fprintf(out, "%s\n", body.c_str());
    }
    std::fflush(out);

    // 文件：所有级别都写，不带颜色。
    if (file_) {
      std::fprintf(file_, "%s\n", body.c_str());
      std::fflush(file_);
    }
  }

 private:
  Logger() = default;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  static const char* levelName(LogLevel level) {
    switch (level) {
      case LogLevel::Debug: return "DEBUG";
      case LogLevel::Info:  return "INFO ";
      case LogLevel::Warn:  return "WARN ";
      case LogLevel::Error: return "ERROR";
    }
    return "?????";
  }

  static const char* levelColor(LogLevel level) {
    switch (level) {
      case LogLevel::Debug: return "\033[90m";  // 暗灰
      case LogLevel::Info:  return "";          // 默认
      case LogLevel::Warn:  return "\033[33m";  // 黄
      case LogLevel::Error: return "\033[31m";  // 红
    }
    return "";
  }

  static bool isTty(std::FILE* stream) {
#ifdef _WIN32
    return _isatty(_fileno(stream)) != 0;
#else
    return ::isatty(::fileno(stream)) != 0;
#endif
  }

  // 本地时区时间戳，毫秒精度：YYYY-MM-DD HH:MM:SS.mmm
  static std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    char out[48];
    std::snprintf(out, sizeof(out), "%s.%03lld", buf,
                  static_cast<long long>(ms.count()));
    return out;
  }

  LogLevel level_ = LogLevel::Info;
  std::string filePath_;
  std::FILE* file_ = nullptr;
  std::mutex mtx_;
};

// 流式包装：临时对象在表达式结束析构，把累积的内容交给 Logger 落盘。
class LogLine {
 public:
  LogLine(LogLevel level, std::string tag) : level_(level), tag_(std::move(tag)) {}
  ~LogLine() { Logger::instance().write(level_, tag_, ss_.str()); }

  LogLine(const LogLine&) = delete;
  LogLine& operator=(const LogLine&) = delete;

  template <typename T>
  LogLine& operator<<(const T& value) {
    ss_ << value;
    return *this;
  }

 private:
  LogLevel level_;
  std::string tag_;
  std::ostringstream ss_;
};

} // namespace DMSAlgo

#define LOG_DEBUG(tag) DMSAlgo::LogLine(DMSAlgo::LogLevel::Debug, (tag))
#define LOG_INFO(tag)  DMSAlgo::LogLine(DMSAlgo::LogLevel::Info,  (tag))
#define LOG_WARN(tag)  DMSAlgo::LogLine(DMSAlgo::LogLevel::Warn,  (tag))
#define LOG_ERROR(tag) DMSAlgo::LogLine(DMSAlgo::LogLevel::Error, (tag))
