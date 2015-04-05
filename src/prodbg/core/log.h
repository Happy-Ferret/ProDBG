#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum
{
    LOG_DEBUG,
    LOG_INFO,
    LOG_ERROR,
    LOG_NONE
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__clang__) || defined(__gcc__)
void pd_log(int logLevel, const char* filename, int line, const char* format, ...) __attribute__((format(printf, 4, 5)));
#else
void pd_log(int logLevel, const char* filename, int line, const char* format, ...);
#endif

void log_set_level(int logLevel);
void log_level_push();
void log_level_pop();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef log_error
#define log_error(...) pd_log(LOG_ERROR,  __FILE__, __LINE__, __VA_ARGS__);
#endif

#ifndef log_debug
#define log_debug(...) pd_log(LOG_DEBUG,  __FILE__, __LINE__, __VA_ARGS__);
#endif

#ifndef log_info
#define log_info(...)  pd_log(LOG_INFO,   __FILE__, __LINE__, __VA_ARGS__);
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


