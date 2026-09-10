#include "Logger.hpp"

#include <stdio.h>
#include <stdarg.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace casioemu
{
	namespace logger
	{
		void Info(const char *format, ...)
		{
			// * TODO may introduce race condition
			
			va_list args;
			va_start(args, format);
#ifdef __ANDROID__
			__android_log_vprint(ANDROID_LOG_INFO, "CasioEmuNeo", format, args);
#else
			vprintf(format, args);
#endif
			va_end(args);
		}
	}
}

