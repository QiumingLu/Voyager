#include "voyager/util/logging.h"

#include <functional>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "voyager/util/status.h"
#include "voyager/util/slice.h"

namespace voyager {

void DefaultLogHandler(LogLevel level, const char* filename, int line,
                       const std::string& message) {
  static const char* loglevel_names[] = {
     "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL" };

  char log_time[64];
  struct timeval now_tv;
  gettimeofday(&now_tv, NULL);
  const time_t seconds = now_tv.tv_sec;
  struct tm t;
  localtime_r(&seconds, &t);
  snprintf(log_time, sizeof(log_time),
           "%04d/%02d/%02d-%02d:%02d:%02d.%06d",
           t.tm_year + 1900,
           t.tm_mon + 1,
           t.tm_mday,
           t.tm_hour,
           t.tm_min,
           t.tm_sec,
           static_cast<int>(now_tv.tv_usec));

  if (level >= LOGLEVEL_WARN) {
    fprintf(stderr, "[%s][%s %s:%d] %s\n",
            log_time, loglevel_names[level], filename, line, 
            message.c_str());
  }
}

void NullLogHandler(LogLevel /* level */, const char* /* filename */,
                    int /* line */, const std::string& /* message */) {
}

static LogHandler* log_handler_ = &DefaultLogHandler;

Logger::Logger(LogLevel level, const char* filename, int line)
    : level_(level),
      filename_(filename),
      line_(line)
{ }

Logger& Logger::operator<<(const char* value) {
  message_ += value;
  return *this;
}

Logger& Logger::operator<<(const Slice& value) {
  message_ += value.ToString();
  return *this;
}

Logger& Logger::operator<<(const std::string& value) {
  message_ += value;
  return *this;
}

Logger& Logger::operator<<(std::string&& value) {
  message_ += std::move(value);
  return *this;
}

Logger& Logger::operator<<(const Status& value) {
  message_ += value.ToString();
  return *this;
}

#undef DECLARE_STREAM_OPERATOR
#define DECLARE_STREAM_OPERATOR(TYPE, FORMAT)                            \
  Logger& Logger::operator<<(TYPE value) {                               \
    char buffer[128];                                                    \
    snprintf(buffer, sizeof(buffer), FORMAT, value);                     \
    buffer[sizeof(buffer) - 1] = '\0';                                   \
    message_ += buffer;                                                  \
    return *this;                                                        \
  }

DECLARE_STREAM_OPERATOR(char              , "%c"  )
DECLARE_STREAM_OPERATOR(short             , "%d"  )
DECLARE_STREAM_OPERATOR(unsigned short    , "%u"  )
DECLARE_STREAM_OPERATOR(int               , "%d"  )
DECLARE_STREAM_OPERATOR(unsigned int      , "%u"  )
DECLARE_STREAM_OPERATOR(long              , "%ld" )
DECLARE_STREAM_OPERATOR(unsigned long     , "%lu" )
DECLARE_STREAM_OPERATOR(long long         , "%lld")
DECLARE_STREAM_OPERATOR(unsigned long long, "%llu")
DECLARE_STREAM_OPERATOR(double            , "%g"  )
DECLARE_STREAM_OPERATOR(void*             , "%p"  )
#undef DECLARE_STREAM_OPERATOR

void Logger::Finish() {
  //FIXME
  log_handler_(level_, filename_, line_, message_);

  if (level_ == LOGLEVEL_FATAL) {
#if USE_EXCEPTIONS
    throw FatalException(filename_, line_, message_);
#else
    abort();
#endif
  }
}

void LogFinisher::operator=(Logger& logger) {
  logger.Finish();
}

LogHandler* SetLogHandler(LogHandler* new_func) {
  LogHandler* old = log_handler_;
  if (old == &NullLogHandler) {
    old = NULL;
  }
  if (new_func == NULL) {
    log_handler_ = &NullLogHandler;
  } else {
    log_handler_ = new_func;
  }
  return old;
}

}  // namespace voyager
