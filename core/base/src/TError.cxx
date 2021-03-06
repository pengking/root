// @(#)root/base:$Id$
// Author: Fons Rademakers   29/07/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/**
Error handling routines.

This file defines a number of global error handling routines:
Warning(), Error(), SysError() and Fatal(). They all take a
location string (where the error happened) and a printf style format
string plus vararg's. In the end these functions call an
errorhandler function. Initially the MinimalErrorHandler, which is supposed
to be replaced by the proper DefaultErrorHandler()
*/

#include "snprintf.h"
#include "Varargs.h"
#include "TError.h"
#include "ThreadLocalStorage.h"

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <string>

// Deprecated
TVirtualMutex *gErrorMutex = nullptr;

Int_t  gErrorIgnoreLevel     = kUnset;
Int_t  gErrorAbortLevel      = kSysError+1;
Bool_t gPrintViaErrorHandler = kFALSE;

const char *kAssertMsg = "%s violated at line %d of `%s'";
const char *kCheckMsg  = "%s not true at line %d of `%s'";

static ErrorHandlerFunc_t gErrorHandler = ROOT::Internal::MinimalErrorHandler;


static ROOT::Internal::ErrorSystemMsgHandlerFunc_t &GetErrorSystemMsgHandlerRef()
{
   static ROOT::Internal::ErrorSystemMsgHandlerFunc_t h;
   return h;
}


namespace ROOT {
namespace Internal {

ErrorSystemMsgHandlerFunc_t GetErrorSystemMsgHandler()
{
   return GetErrorSystemMsgHandlerRef();
}

ErrorSystemMsgHandlerFunc_t SetErrorSystemMsgHandler(ErrorSystemMsgHandlerFunc_t h)
{
   auto oldHandler = GetErrorSystemMsgHandlerRef();
   GetErrorSystemMsgHandlerRef() = h;
   return oldHandler;
}

/// A very simple error handler that is usually replaced by the TROOT default error handler.
/// The minimal error handler is not serialized across threads, so that output of multi-threaded programs
/// can get scrambled
void MinimalErrorHandler(Int_t level, Bool_t abort_bool, const char *location, const char *msg)
{
   if (level < gErrorIgnoreLevel)
      return;

   if (level >= kBreak)
      fprintf(stderr, "\n *** Break *** ");
   fprintf(stderr, "<%s>: %s\n", location ? location : "unspecified location", msg);
   fflush(stderr);
   if (abort_bool) {
      fprintf(stderr, "aborting\n");
      fflush(stderr);
      abort();
   }
}

} // namespace Internal
} // namespace ROOT


////////////////////////////////////////////////////////////////////////////////
/// Set an errorhandler function. Returns the old handler.

ErrorHandlerFunc_t SetErrorHandler(ErrorHandlerFunc_t newhandler)
{
   ErrorHandlerFunc_t oldhandler = gErrorHandler;
   gErrorHandler = newhandler;
   return oldhandler;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the current error handler function.

ErrorHandlerFunc_t GetErrorHandler()
{
   return gErrorHandler;
}


////////////////////////////////////////////////////////////////////////////////
/// General error handler function. It calls the user set error handler.

void ErrorHandler(Int_t level, const char *location, const char *fmt, va_list ap)
{
   TTHREAD_TLS(Int_t) buf_size(256);
   TTHREAD_TLS(char*) buf_storage(0);

   char small_buf[256];
   char *buf = buf_storage ? buf_storage : small_buf;

   int vc = 0;
   va_list sap;
   R__VA_COPY(sap, ap);

again:
   if (!buf) {
      buf_storage = buf = new char[buf_size];
   }

   if (!fmt)
      fmt = "no error message provided";

   Int_t n = vsnprintf(buf, buf_size, fmt, ap);
   // old vsnprintf's return -1 if string is truncated new ones return
   // total number of characters that would have been written
   if (n == -1 || n >= buf_size) {
      if (n == -1)
         buf_size *= 2;
      else
         buf_size = n+1;
      if (buf != &(small_buf[0])) delete [] buf;
      buf = 0;
      va_end(ap);
      R__VA_COPY(ap, sap);
      vc = 1;
      goto again;
   }
   va_end(sap);
   if (vc)
      va_end(ap);

   std::string bp = buf;
   if (level >= kSysError && level < kFatal) {
      bp.push_back(' ');
      if (GetErrorSystemMsgHandlerRef())
         bp += GetErrorSystemMsgHandlerRef()();
      else
         bp += std::string("(errno: ") + std::to_string(errno) + ")";
   }

   if (level != kFatal)
      gErrorHandler(level, level >= gErrorAbortLevel, location, bp.c_str());
   else
      gErrorHandler(level, kTRUE, location, bp.c_str());
}

////////////////////////////////////////////////////////////////////////////////
/// This function can be used in abstract base classes in case one does
/// not want to make the class a "real" (in C++ sense) ABC. If this
/// function is called it will warn the user that the function should
/// have been overridden.

void AbstractMethod(const char *method)
{
   Warning(method, "this method must be overridden!");
}

////////////////////////////////////////////////////////////////////////////////
/// This function can be used in classes that should override a certain
/// function, but in the inherited class the function makes no sense.

void MayNotUse(const char *method)
{
   Warning(method, "may not use this method");
}

////////////////////////////////////////////////////////////////////////////////
/// Use this function to declare a function obsolete. Specify as of which version
/// the method is obsolete and as from which version it will be removed.

void Obsolete(const char *function, const char *asOfVers, const char *removedFromVers)
{
   Warning(function, "obsolete as of %s and will be removed from %s", asOfVers, removedFromVers);
}

////////////////////////////////////////////////////////////////////////////////
/// Use this function in case an error occurred.

void Error(const char *location, const char *va_(fmt), ...)
{
   va_list ap;
   va_start(ap,va_(fmt));
   ErrorHandler(kError, location, va_(fmt), ap);
   va_end(ap);
}

////////////////////////////////////////////////////////////////////////////////
/// Use this function in case a system (OS or GUI) related error occurred.

void SysError(const char *location, const char *va_(fmt), ...)
{
   va_list ap;
   va_start(ap, va_(fmt));
   ErrorHandler(kSysError, location, va_(fmt), ap);
   va_end(ap);
}

////////////////////////////////////////////////////////////////////////////////
/// Use this function in case an error occurred.

void Break(const char *location, const char *va_(fmt), ...)
{
   va_list ap;
   va_start(ap,va_(fmt));
   ErrorHandler(kBreak, location, va_(fmt), ap);
   va_end(ap);
}

////////////////////////////////////////////////////////////////////////////////
/// Use this function for informational messages.

void Info(const char *location, const char *va_(fmt), ...)
{
   va_list ap;
   va_start(ap,va_(fmt));
   ErrorHandler(kInfo, location, va_(fmt), ap);
   va_end(ap);
}

////////////////////////////////////////////////////////////////////////////////
/// Use this function in warning situations.

void Warning(const char *location, const char *va_(fmt), ...)
{
   va_list ap;
   va_start(ap,va_(fmt));
   ErrorHandler(kWarning, location, va_(fmt), ap);
   va_end(ap);
}

////////////////////////////////////////////////////////////////////////////////
/// Use this function in case of a fatal error. It will abort the program.

// Fatal() *might* not abort the program (if gAbortLevel > kFatal) - but for all
// reasonable settings it *will* abort. So let's be reasonable wrt Coverity:
// coverity[+kill]
void Fatal(const char *location, const char *va_(fmt), ...)
{
   va_list ap;
   va_start(ap,va_(fmt));
   ErrorHandler(kFatal, location, va_(fmt), ap);
   va_end(ap);
}
